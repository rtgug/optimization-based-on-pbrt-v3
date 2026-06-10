
/*
    pbrt source code is Copyright(c) 1998-2016
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

// integrators/adaptive.cpp*
#include "integrators/adaptive.h"
#include "bssrdf.h"
#include "camera.h"
#include "film.h"
#include "interaction.h"
#include "paramset.h"
#include "scene.h"
#include "stats.h"
#include "parallel.h"
#include "progressreporter.h"
#include "sampler.h"

#include <cmath>
#include <vector>

namespace pbrt {

STAT_COUNTER("Integrator/Camera rays traced (adaptive)", nAdaptiveCameraRays);

// AdaptiveIntegrator Method Definitions
AdaptiveIntegrator::AdaptiveIntegrator(
    int maxDepth, int minSamples, int totalSampleBudget,
    std::shared_ptr<const Camera> camera, std::shared_ptr<Sampler> sampler,
    const Bounds2i &pixelBounds, Float rrThreshold,
    const std::string &lightSampleStrategy)
    : PathIntegrator(maxDepth, camera, sampler, pixelBounds, rrThreshold,
                     lightSampleStrategy),
      minSamples(minSamples),
      totalSampleBudget(totalSampleBudget) {}

void AdaptiveIntegrator::Render(const Scene &scene) {
    // ─── Preprocess (sets up light distribution for path tracing) ───
    Preprocess(scene, *sampler);

    Bounds2i sampleBounds = camera->film->GetSampleBounds();
    Vector2i sampleExtent = sampleBounds.Diagonal();
    int width = sampleExtent.x;
    int height = sampleExtent.y;
    int nPixels = std::max(1, width * height);

    LOG(INFO) << "AdaptiveIntegrator: rendering " << width << "x" << height
              << " = " << nPixels << " pixels"
              << ", minSamples=" << minSamples
              << ", totalBudget=" << totalSampleBudget;

    // The effective uniform SPP equivalent
    int uniformSpp = totalSampleBudget / nPixels;
    LOG(INFO) << "  Equivalent uniform SPP = " << uniformSpp;

    // ────────────────────────────────────────────────────────────────
    // PHASE 1: Pilot render — estimate per-pixel variance
    // ────────────────────────────────────────────────────────────────
    std::vector<PixelStats> pixelStats(nPixels);

    {
        const int tileSize = 16;
        Point2i nTiles((sampleExtent.x + tileSize - 1) / tileSize,
                        (sampleExtent.y + tileSize - 1) / tileSize);
        ProgressReporter reporter(nTiles.x * nTiles.y, "Adaptive pilot");

        ParallelFor2D(
            [&](Point2i tile) {
                MemoryArena arena;
                int seed = tile.y * nTiles.x + tile.x;
                std::unique_ptr<Sampler> tileSampler = sampler->Clone(seed);

                int x0 = sampleBounds.pMin.x + tile.x * tileSize;
                int x1 = std::min(x0 + tileSize, sampleBounds.pMax.x);
                int y0 = sampleBounds.pMin.y + tile.y * tileSize;
                int y1 = std::min(y0 + tileSize, sampleBounds.pMax.y);
                Bounds2i tileBounds(Point2i(x0, y0), Point2i(x1, y1));

                for (Point2i pixel : tileBounds) {
                    // Use minSamples for the pilot pass
                    tileSampler->samplesPerPixel = minSamples;
                    tileSampler->StartPixel(pixel);

                    if (!InsideExclusive(pixel, pixelBounds)) continue;

                    do {
                        CameraSample cameraSample =
                            tileSampler->GetCameraSample(pixel);

                        RayDifferential ray;
                        Float rayWeight = camera->GenerateRayDifferential(
                            cameraSample, &ray);
                        ray.ScaleDifferentials(
                            1 / std::sqrt((Float)tileSampler->samplesPerPixel));
                        ++nAdaptiveCameraRays;

                        Spectrum L(0.f);
                        if (rayWeight > 0)
                            L = Li(ray, scene, *tileSampler, arena);

                        // Track luminance variance via Welford's online
                        // algorithm
                        if (rayWeight > 0) {
                            int idx =
                                (pixel.y - sampleBounds.pMin.y) * width +
                                (pixel.x - sampleBounds.pMin.x);
                            PixelStats &ps = pixelStats[idx];
                            double lum = (double)L.y();
                            ps.count++;
                            double delta = lum - ps.mean;
                            ps.mean += delta / ps.count;
                            ps.M2 += delta * (lum - ps.mean);
                        }

                        arena.Reset();
                    } while (tileSampler->StartNextSample());
                }
                reporter.Update();
            },
            nTiles);
        reporter.Done();
    }

    // ────────────────────────────────────────────────────────────────
    // PHASE 2: Lagrange multiplier optimal sample allocation
    // ────────────────────────────────────────────────────────────────
    //   Minimize:  MSE = Σ σ_i² / n_i
    //   Subject to: Σ n_i ≤ totalSampleBudget
    //   Solution:  n_i = n_min + (R * σ_i) / Σ σ_j
    //
    // where R = totalSampleBudget - nPixels * minSamples

    std::vector<int> sampleAlloc(nPixels, minSamples);

    double totalSigma = 0.0;
    std::vector<double> pixelSigma(nPixels, 0.0);

    for (int i = 0; i < nPixels; ++i) {
        if (pixelStats[i].count > 1) {
            double var = pixelStats[i].M2 / (pixelStats[i].count - 1);
            pixelSigma[i] = std::sqrt(std::max(var, 0.0));
        }
        totalSigma += pixelSigma[i];
    }

    int remainingBudget =
        std::max(0, totalSampleBudget - nPixels * minSamples);

    if (remainingBudget > 0 && totalSigma > 1e-12) {
        int totalAllocatedExtra = 0;
        for (int i = 0; i < nPixels; ++i) {
            int extra = (int)(remainingBudget * pixelSigma[i] / totalSigma);
            sampleAlloc[i] = minSamples + extra;
            totalAllocatedExtra += extra;
        }

        // Distribute any remaining samples (from rounding) to pixels with
        // highest variance
        int roundingRemainder = remainingBudget - totalAllocatedExtra;
        LOG(INFO) << "  Lagrange allocation: R=" << remainingBudget
                  << ", allocated=" << totalAllocatedExtra
                  << ", rounding_remainder=" << roundingRemainder;

        if (roundingRemainder > 0 && totalSigma > 1e-12) {
            // Sort pixel indices by sigma for distributing remainders
            std::vector<int> idx(nPixels);
            for (int i = 0; i < nPixels; ++i) idx[i] = i;
            std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                return pixelSigma[a] > pixelSigma[b];
            });
            for (int i = 0; i < roundingRemainder && i < nPixels; ++i) {
                sampleAlloc[idx[i]]++;
            }
        }
    } else {
        LOG(INFO) << "  Uniform allocation: all pixels get minSamples";
    }

    // Compute statistics about the allocation
    int minAlloc = sampleAlloc[0], maxAlloc = sampleAlloc[0];
    double avgAlloc = 0;
    for (int i = 0; i < nPixels; ++i) {
        minAlloc = std::min(minAlloc, sampleAlloc[i]);
        maxAlloc = std::max(maxAlloc, sampleAlloc[i]);
        avgAlloc += sampleAlloc[i];
    }
    avgAlloc /= nPixels;
    LOG(INFO) << "  Sample allocation: min=" << minAlloc
              << ", max=" << maxAlloc << ", avg=" << avgAlloc;

    // ────────────────────────────────────────────────────────────────
    // PHASE 3: Final render with optimal per-pixel sample allocation
    // ────────────────────────────────────────────────────────────────

    // Clear the pilot image from the film
    camera->film->Clear();

    {
        const int tileSize = 16;
        Point2i nTiles((sampleExtent.x + tileSize - 1) / tileSize,
                        (sampleExtent.y + tileSize - 1) / tileSize);
        ProgressReporter reporter(nTiles.x * nTiles.y, "Adaptive render");

        ParallelFor2D(
            [&](Point2i tile) {
                MemoryArena arena;
                int seed = tile.y * nTiles.x + tile.x + 9973;  // offset seed
                std::unique_ptr<Sampler> tileSampler = sampler->Clone(seed);

                int x0 = sampleBounds.pMin.x + tile.x * tileSize;
                int x1 = std::min(x0 + tileSize, sampleBounds.pMax.x);
                int y0 = sampleBounds.pMin.y + tile.y * tileSize;
                int y1 = std::min(y0 + tileSize, sampleBounds.pMax.y);
                Bounds2i tileBounds(Point2i(x0, y0), Point2i(x1, y1));

                std::unique_ptr<FilmTile> filmTile =
                    camera->film->GetFilmTile(tileBounds);

                for (Point2i pixel : tileBounds) {
                    int idx = (pixel.y - sampleBounds.pMin.y) * width +
                              (pixel.x - sampleBounds.pMin.x);
                    int budget = sampleAlloc[idx];

                    // Set per-pixel sample count for optimal allocation
                    tileSampler->samplesPerPixel = budget;
                    tileSampler->StartPixel(pixel);

                    if (!InsideExclusive(pixel, pixelBounds)) continue;

                    do {
                        CameraSample cameraSample =
                            tileSampler->GetCameraSample(pixel);

                        RayDifferential ray;
                        Float rayWeight = camera->GenerateRayDifferential(
                            cameraSample, &ray);
                        ray.ScaleDifferentials(
                            1 / std::sqrt((Float)tileSampler->samplesPerPixel));
                        ++nAdaptiveCameraRays;

                        Spectrum L(0.f);
                        if (rayWeight > 0)
                            L = Li(ray, scene, *tileSampler, arena);

                        // Validate radiance
                        if (L.HasNaNs()) {
                            LOG(ERROR) << StringPrintf(
                                "NaN radiance at (%d,%d), sample %d.",
                                pixel.x, pixel.y,
                                (int)tileSampler->CurrentSampleNumber());
                            L = Spectrum(0.f);
                        } else if (L.y() < -1e-5) {
                            LOG(ERROR) << StringPrintf(
                                "Negative luminance %f at (%d,%d).",
                                L.y(), pixel.x, pixel.y);
                            L = Spectrum(0.f);
                        } else if (std::isinf(L.y())) {
                            LOG(ERROR) << StringPrintf(
                                "Infinite luminance at (%d,%d).",
                                pixel.x, pixel.y);
                            L = Spectrum(0.f);
                        }

                        filmTile->AddSample(cameraSample.pFilm, L, rayWeight);
                        arena.Reset();
                    } while (tileSampler->StartNextSample());
                }

                camera->film->MergeFilmTile(std::move(filmTile));
                reporter.Update();
            },
            nTiles);
        reporter.Done();
    }

    LOG(INFO) << "AdaptiveIntegrator rendering finished";
    camera->film->WriteImage();
}

AdaptiveIntegrator *CreateAdaptiveIntegrator(const ParamSet &params,
                                             std::shared_ptr<Sampler> sampler,
                                             std::shared_ptr<const Camera> camera) {
    int maxDepth = params.FindOneInt("maxdepth", 5);
    int minSamples = params.FindOneInt("minsamples", 4);
    int totalSampleBudget = params.FindOneInt("totalbudget", 65536);

    int np;
    const int *pb = params.FindInt("pixelbounds", &np);
    Bounds2i pixelBounds = camera->film->GetSampleBounds();
    if (pb) {
        if (np != 4)
            Error("Expected four values for \"pixelbounds\" parameter. Got %d.",
                  np);
        else {
            pixelBounds = Intersect(
                pixelBounds, Bounds2i{{pb[0], pb[2]}, {pb[1], pb[3]}});
            if (pixelBounds.Area() == 0)
                Error("Degenerate \"pixelbounds\" specified.");
        }
    }
    Float rrThreshold = params.FindOneFloat("rrthreshold", 1.);
    std::string lightStrategy =
        params.FindOneString("lightsamplestrategy", "spatial");

    return new AdaptiveIntegrator(maxDepth, minSamples, totalSampleBudget,
                                  camera, sampler, pixelBounds, rrThreshold,
                                  lightStrategy);
}

}  // namespace pbrt
