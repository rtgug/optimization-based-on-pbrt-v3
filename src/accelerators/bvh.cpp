
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


// accelerators/bvh.cpp*
#include "accelerators/bvh.h"
#include "interaction.h"
#include "paramset.h"
#include "stats.h"
#include "parallel.h"
#include <algorithm>
#include <future>
#include <mutex>

// ---------------------------------------------------------------------------
// Phase 6: Compile-time SSE detection and cache prefetch macros
// ---------------------------------------------------------------------------
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#define PBRT_HAVE_SSE 1
#include <xmmintrin.h>   // SSE: prefetch, min, max
#include <emmintrin.h>   // SSE2
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PBRT_PREFETCH(addr) __builtin_prefetch((const void *)(addr), 0, 3)
#elif defined(_MSC_VER)
#include <intrin.h>
#define PBRT_PREFETCH(addr) _mm_prefetch((const char *)(addr), _MM_HINT_T0)
#else
#define PBRT_PREFETCH(addr) ((void)0)
#endif

// Helper: thread-safe ArenaAlloc guard for parallel builds
static std::mutex parallelBuildArenaMutex;

namespace pbrt {

STAT_MEMORY_COUNTER("Memory/BVH tree", treeBytes);
STAT_RATIO("BVH/Primitives per leaf node", totalPrimitives, totalLeafNodes);
STAT_COUNTER("BVH/Interior nodes", interiorNodes);
STAT_COUNTER("BVH/Leaf nodes", leafNodes);

// BVHAccel Local Declarations
struct BVHPrimitiveInfo {
    BVHPrimitiveInfo() {}
    BVHPrimitiveInfo(size_t primitiveNumber, const Bounds3f &bounds)
        : primitiveNumber(primitiveNumber),
          bounds(bounds),
          centroid(.5f * bounds.pMin + .5f * bounds.pMax) {}
    size_t primitiveNumber;
    Bounds3f bounds;
    Point3f centroid;
};

struct BVHBuildNode {
    // BVHBuildNode Public Methods
    void InitLeaf(int first, int n, const Bounds3f &b) {
        firstPrimOffset = first;
        nPrimitives = n;
        bounds = b;
        children[0] = children[1] = nullptr;
        ++leafNodes;
        ++totalLeafNodes;
        totalPrimitives += n;
    }
    void InitInterior(int axis, BVHBuildNode *c0, BVHBuildNode *c1) {
        children[0] = c0;
        children[1] = c1;
        bounds = Union(c0->bounds, c1->bounds);
        splitAxis = axis;
        nPrimitives = 0;
        ++interiorNodes;
    }
    Bounds3f bounds;
    BVHBuildNode *children[2];
    int splitAxis, firstPrimOffset, nPrimitives;
};

struct MortonPrimitive {
    int primitiveIndex;
    uint32_t mortonCode;
};

struct LBVHTreelet {
    int startIndex, nPrimitives;
    BVHBuildNode *buildNodes;
};

struct LinearBVHNode {
    Bounds3f bounds;
    union {
        int primitivesOffset;   // leaf
        int secondChildOffset;  // interior
    };
    uint16_t nPrimitives;  // 0 -> interior node
    uint8_t axis;          // interior node: xyz
    uint8_t pad[1];        // ensure 32 byte total size
};

// BVHAccel Utility Functions
inline uint32_t LeftShift3(uint32_t x) {
    CHECK_LE(x, (1 << 10));
    if (x == (1 << 10)) --x;
#ifdef PBRT_HAVE_BINARY_CONSTANTS
    x = (x | (x << 16)) & 0b00000011000000000000000011111111;
    // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x | (x << 8)) & 0b00000011000000001111000000001111;
    // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x | (x << 4)) & 0b00000011000011000011000011000011;
    // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x | (x << 2)) & 0b00001001001001001001001001001001;
    // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
#else
    x = (x | (x << 16)) & 0x30000ff;
    // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x | (x << 8)) & 0x300f00f;
    // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x | (x << 4)) & 0x30c30c3;
    // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x | (x << 2)) & 0x9249249;
    // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
#endif // PBRT_HAVE_BINARY_CONSTANTS
    return x;
}

inline uint32_t EncodeMorton3(const Vector3f &v) {
    CHECK_GE(v.x, 0);
    CHECK_GE(v.y, 0);
    CHECK_GE(v.z, 0);
    return (LeftShift3(v.z) << 2) | (LeftShift3(v.y) << 1) | LeftShift3(v.x);
}

static void RadixSort(std::vector<MortonPrimitive> *v) {
    std::vector<MortonPrimitive> tempVector(v->size());
    PBRT_CONSTEXPR int bitsPerPass = 6;
    PBRT_CONSTEXPR int nBits = 30;
    static_assert((nBits % bitsPerPass) == 0,
                  "Radix sort bitsPerPass must evenly divide nBits");
    PBRT_CONSTEXPR int nPasses = nBits / bitsPerPass;

    for (int pass = 0; pass < nPasses; ++pass) {
        // Perform one pass of radix sort, sorting _bitsPerPass_ bits
        int lowBit = pass * bitsPerPass;

        // Set in and out vector pointers for radix sort pass
        std::vector<MortonPrimitive> &in = (pass & 1) ? tempVector : *v;
        std::vector<MortonPrimitive> &out = (pass & 1) ? *v : tempVector;

        // Count number of zero bits in array for current radix sort bit
        PBRT_CONSTEXPR int nBuckets = 1 << bitsPerPass;
        int bucketCount[nBuckets] = {0};
        PBRT_CONSTEXPR int bitMask = (1 << bitsPerPass) - 1;
        for (const MortonPrimitive &mp : in) {
            int bucket = (mp.mortonCode >> lowBit) & bitMask;
            CHECK_GE(bucket, 0);
            CHECK_LT(bucket, nBuckets);
            ++bucketCount[bucket];
        }

        // Compute starting index in output array for each bucket
        int outIndex[nBuckets];
        outIndex[0] = 0;
        for (int i = 1; i < nBuckets; ++i)
            outIndex[i] = outIndex[i - 1] + bucketCount[i - 1];

        // Store sorted values in output array
        for (const MortonPrimitive &mp : in) {
            int bucket = (mp.mortonCode >> lowBit) & bitMask;
            out[outIndex[bucket]++] = mp;
        }
    }
    // Copy final result from _tempVector_, if needed
    if (nPasses & 1) std::swap(*v, tempVector);
}

// BVHAccel Method Definitions
BVHAccel::BVHAccel(std::vector<std::shared_ptr<Primitive>> p,
                   int maxPrimsInNode, SplitMethod splitMethod,
                   Float traversalCost, Float intersectionCost)
    : maxPrimsInNode(std::min(255, maxPrimsInNode)),
      splitMethod(splitMethod),
      traversalCost(traversalCost),
      intersectionCost(intersectionCost),
      primitives(std::move(p)) {
    ProfilePhase _(Prof::AccelConstruction);
    if (primitives.empty()) return;
    // Build BVH from _primitives_

    // Initialize _primitiveInfo_ array for primitives
    std::vector<BVHPrimitiveInfo> primitiveInfo(primitives.size());
    for (size_t i = 0; i < primitives.size(); ++i)
        primitiveInfo[i] = {i, primitives[i]->WorldBound()};

    // Build BVH tree for primitives using _primitiveInfo_
    MemoryArena arena(1024 * 1024);
    int totalNodes = 0;
    std::vector<std::shared_ptr<Primitive>> orderedPrims;
    orderedPrims.reserve(primitives.size());
    BVHBuildNode *root;
    if (splitMethod == SplitMethod::HLBVH) {
        root = HLBVHBuild(arena, primitiveInfo, &totalNodes, orderedPrims);
    } else {
        // Pre-allocate orderedPrims for thread-safe leaf creation
        // in parallel recursive build (Phase 5)
        orderedPrims.resize(primitives.size());
        std::atomic<int> totalNodesAtomic(totalNodes);
        std::atomic<int> orderedPrimsOffset(0);
        root = recursiveBuild(arena, primitiveInfo, 0, primitives.size(),
                              &totalNodesAtomic, orderedPrims,
                              &orderedPrimsOffset, 0);
        totalNodes = totalNodesAtomic;
    }
    primitives.swap(orderedPrims);
    primitiveInfo.resize(0);
    LOG(INFO) << StringPrintf("BVH created with %d nodes for %d "
                              "primitives (%.2f MB), arena allocated %.2f MB",
                              totalNodes, (int)primitives.size(),
                              float(totalNodes * sizeof(LinearBVHNode)) /
                              (1024.f * 1024.f),
                              float(arena.TotalAllocated()) /
                              (1024.f * 1024.f));

    // Compute representation of depth-first traversal of BVH tree
    treeBytes += totalNodes * sizeof(LinearBVHNode) + sizeof(*this) +
                 primitives.size() * sizeof(primitives[0]);
    nodes = AllocAligned<LinearBVHNode>(totalNodes);
    int offset = 0;
    flattenBVHTree(root, &offset);
    CHECK_EQ(totalNodes, offset);
}

Bounds3f BVHAccel::WorldBound() const {
    return nodes ? nodes[0].bounds : Bounds3f();
}

struct BucketInfo {
    int count = 0;
    Bounds3f bounds;
};

// ---------------------------------------------------------------------------
// Helper: write primitives into orderedPrims at a given offset
//         (works with both atomic-offset and sequential offsets)
// ---------------------------------------------------------------------------
static void WriteOrderedPrims(
    const std::vector<BVHPrimitiveInfo> &primitiveInfo, int start, int end,
    std::vector<std::shared_ptr<Primitive>> &orderedPrims,
    int firstPrimOffset,
    const std::vector<std::shared_ptr<Primitive>> &primitives) {
    for (int i = start; i < end; ++i) {
        int primNum = primitiveInfo[i].primitiveNumber;
        orderedPrims[firstPrimOffset + i - start] = primitives[primNum];
    }
}

// ---------------------------------------------------------------------------
// Phase 6: SSE-accelerated ray-AABB intersection test
// ---------------------------------------------------------------------------
#if defined(PBRT_HAVE_SSE)
static inline bool IntersectP_SSE(const LinearBVHNode *node,
                                  const Ray &ray,
                                  const Vector3f &invDir) {
    // SSE2 implementation of the Slab method.
    // Process all three axes simultaneously using packed SIMD.

    // Load bounding box extents (last element unused, set to 0)
    __m128 bmin = _mm_set_ps(0, node->bounds.pMin.z,
                             node->bounds.pMin.y, node->bounds.pMin.x);
    __m128 bmax = _mm_set_ps(0, node->bounds.pMax.z,
                             node->bounds.pMax.y, node->bounds.pMax.x);

    // Load ray origin and inverse direction
    __m128 orig = _mm_set_ps(0, ray.o.z, ray.o.y, ray.o.x);
    __m128 inv_dir = _mm_set_ps(0, invDir.z, invDir.y, invDir.x);

    // t0 = (bmin - orig) * inv_dir    [near-plane hit distances]
    // t1 = (bmax - orig) * inv_dir    [far-plane hit distances]
    __m128 t0 = _mm_mul_ps(_mm_sub_ps(bmin, orig), inv_dir);
    __m128 t1 = _mm_mul_ps(_mm_sub_ps(bmax, orig), inv_dir);

    // Where inv_dir < 0, the near and far slabs are swapped.
    // Emulate branchless blendv using SSE2 ops:
    // near = (~neg & t0) | (neg & t1)   i.e. min(t0,t1)
    // far  = (~neg & t1) | (neg & t0)   i.e. max(t0,t1)
    __m128 neg_mask = _mm_cmplt_ps(inv_dir, _mm_setzero_ps());
    __m128 near_t = _mm_or_ps(_mm_andnot_ps(neg_mask, t0),
                               _mm_and_ps(neg_mask, t1));
    __m128 far_t  = _mm_or_ps(_mm_andnot_ps(neg_mask, t1),
                               _mm_and_ps(neg_mask, t0));

    // Reduce: tMin = max(near.x, near.y, near.z)
    //         tMax = min(far.x,  far.y,  far.z)
    // Use SSE shuffles for horizontal min/max
    __m128 tmin = _mm_max_ps(near_t, _mm_shuffle_ps(near_t, near_t,
                                                     _MM_SHUFFLE(0, 0, 3, 2)));
    tmin = _mm_max_ps(tmin, _mm_shuffle_ps(tmin, tmin, _MM_SHUFFLE(0, 0, 0, 1)));

    __m128 tmax = _mm_min_ps(far_t, _mm_shuffle_ps(far_t, far_t,
                                                     _MM_SHUFFLE(0, 0, 3, 2)));
    tmax = _mm_min_ps(tmax, _mm_shuffle_ps(tmax, tmax, _MM_SHUFFLE(0, 0, 0, 1)));

    // Extract scalar results (w component is unused/padding)
    float tMin, tMax;
    _mm_store_ss(&tMin, tmin);
    _mm_store_ss(&tMax, tmax);

    // Robustness epsilon (matches original scalar code)
    tMax *= 1 + 2 * gamma(3);

    return (tMin <= tMax) && (tMax > 0) && (tMin < ray.tMax);
}
#endif // PBRT_HAVE_SSE

// ===========================================================================
// Phase 1-5: Optimized recursive BVH construction
// ===========================================================================
BVHBuildNode *BVHAccel::recursiveBuild(
    MemoryArena &arena, std::vector<BVHPrimitiveInfo> &primitiveInfo, int start,
    int end, std::atomic<int> *totalNodes,
    std::vector<std::shared_ptr<Primitive>> &orderedPrims,
    std::atomic<int> *orderedPrimsOffset, int depth) {
    CHECK_NE(start, end);

    // Allocate node (thread-safe via mutex in parallel builds)
    BVHBuildNode *node;
    {
        std::lock_guard<std::mutex> lock(parallelBuildArenaMutex);
        node = arena.Alloc<BVHBuildNode>();
    }
    (*totalNodes)++;

    // Compute bounds of all primitives in BVH node
    Bounds3f bounds;
    for (int i = start; i < end; ++i)
        bounds = Union(bounds, primitiveInfo[i].bounds);
    int nPrimitives = end - start;

    // ----- Leaf creation for single-primitive nodes -----
    if (nPrimitives == 1) {
        int firstPrimOffset = (orderedPrimsOffset)
            ? orderedPrimsOffset->fetch_add(1)
            : (int)orderedPrims.size();
        orderedPrims[firstPrimOffset] = primitives[primitiveInfo[start].primitiveNumber];
        node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
        return node;
    }

    // ----- Compute centroid bounds & choose split axis -----
    Bounds3f centroidBounds;
    for (int i = start; i < end; ++i)
        centroidBounds = Union(centroidBounds, primitiveInfo[i].centroid);
    int dim = centroidBounds.MaximumExtent();

    // Degenerate case: all centroids at same position → create leaf
    int mid = (start + end) / 2;
    if (centroidBounds.pMax[dim] == centroidBounds.pMin[dim]) {
        int firstPrimOffset = (orderedPrimsOffset)
            ? orderedPrimsOffset->fetch_add(nPrimitives)
            : (int)orderedPrims.size();
        WriteOrderedPrims(primitiveInfo, start, end,
                          orderedPrims, firstPrimOffset, primitives);
        node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
        return node;
    }

    // ----- Partition primitives based on chosen split method -----
    switch (splitMethod) {
    case SplitMethod::Middle: {
        Float pmid = (centroidBounds.pMin[dim] + centroidBounds.pMax[dim]) / 2;
        BVHPrimitiveInfo *midPtr = std::partition(
            &primitiveInfo[start], &primitiveInfo[end - 1] + 1,
            [dim, pmid](const BVHPrimitiveInfo &pi) {
                return pi.centroid[dim] < pmid;
            });
        mid = midPtr - &primitiveInfo[0];
        if (mid != start && mid != end) break;
        // Fall through to EqualCounts if partition failed
    }
    case SplitMethod::EqualCounts: {
        mid = (start + end) / 2;
        std::nth_element(&primitiveInfo[start], &primitiveInfo[mid],
                         &primitiveInfo[end - 1] + 1,
                         [dim](const BVHPrimitiveInfo &a,
                               const BVHPrimitiveInfo &b) {
                             return a.centroid[dim] < b.centroid[dim];
                         });
        break;
    }
    case SplitMethod::SAH:
    default: {
        // ====================================================================
        // PHASES 1-4: Optimized SAH with prefix/suffix scan + adaptive
        //             bucket count + parameterised cost + adaptive leaf
        // ====================================================================

        // --- Phase 3: Adaptive bucket count ---
        int nBuckets;
        if (nPrimitives < 16)         nBuckets = 4;
        else if (nPrimitives < 64)    nBuckets = 8;
        else if (nPrimitives < 256)   nBuckets = 12;
        else                          nBuckets = 16;
        CHECK_LE(nBuckets, 16);

        BucketInfo buckets[16];  // max 16 buckets → fixed stack array
        Float cost[16];

        // Initialize BucketInfo for SAH partition buckets
        for (int i = start; i < end; ++i) {
            int b = nBuckets *
                    centroidBounds.Offset(primitiveInfo[i].centroid)[dim];
            if (b == nBuckets) b = nBuckets - 1;
            CHECK_GE(b, 0);
            CHECK_LT(b, nBuckets);
            buckets[b].count++;
            buckets[b].bounds =
                Union(buckets[b].bounds, primitiveInfo[i].bounds);
        }

        // ---- Phase 1: Prefix scan (left → right) ----
        BucketInfo prefix[16];
        for (int i = 0; i < nBuckets; ++i) {
            prefix[i] = buckets[i];
            if (i > 0) {
                prefix[i].count += prefix[i-1].count;
                prefix[i].bounds = Union(prefix[i-1].bounds, buckets[i].bounds);
            }
        }

        // ---- Phase 1: Suffix scan (right → left) ----
        BucketInfo suffix[16];
        for (int i = nBuckets - 1; i >= 0; --i) {
            suffix[i] = buckets[i];
            if (i < nBuckets - 1) {
                suffix[i].count += suffix[i+1].count;
                suffix[i].bounds = Union(suffix[i+1].bounds, buckets[i].bounds);
            }
        }

        // ---- Phase 2: Parameterised SAH cost evaluation ----
        Float totalSA = bounds.SurfaceArea();
        Float invTotalSA = (totalSA > 0) ? (1.0f / totalSA) : 1.0f;
        for (int i = 0; i < nBuckets - 1; ++i) {
            int count0 = prefix[i].count;
            int count1 = suffix[i+1].count;
            Float SA0 = prefix[i].bounds.SurfaceArea();
            Float SA1 = suffix[i+1].bounds.SurfaceArea();

            // Standard SAH: C_trav + C_isect * (N0*SA0 + N1*SA1) / SA_total
            cost[i] = traversalCost +
                      intersectionCost * (count0 * SA0 + count1 * SA1) * invTotalSA;
        }

        // Find bucket with minimum SAH cost
        Float minCost = cost[0];
        int minCostSplitBucket = 0;
        for (int i = 1; i < nBuckets - 1; ++i) {
            if (cost[i] < minCost) {
                minCost = cost[i];
                minCostSplitBucket = i;
            }
        }

        // ---- Phase 4: Enhanced leaf-creation decision ----
        // Leaf cost = intersectionCost * N (intersect all primitives)
        Float leafCost = intersectionCost * nPrimitives;

        if (nPrimitives > maxPrimsInNode && minCost < leafCost) {
            // SAH says split is beneficial
            BVHPrimitiveInfo *pmid = std::partition(
                &primitiveInfo[start], &primitiveInfo[end - 1] + 1,
                [=](const BVHPrimitiveInfo &pi) {
                    int b = nBuckets *
                            centroidBounds.Offset(pi.centroid)[dim];
                    if (b == nBuckets) b = nBuckets - 1;
                    CHECK_GE(b, 0);
                    CHECK_LT(b, nBuckets);
                    return b <= minCostSplitBucket;
                });
            mid = pmid - &primitiveInfo[0];
        } else {
            // SAH says no benefit → create leaf
            int firstPrimOffset = (orderedPrimsOffset)
                ? orderedPrimsOffset->fetch_add(nPrimitives)
                : (int)orderedPrims.size();
            WriteOrderedPrims(primitiveInfo, start, end,
                              orderedPrims, firstPrimOffset, primitives);
            node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
            return node;
        }
        break;
    }
    }

    // ========================================================================
    // Phase 5: Parallel subtree construction for large nodes
    // ========================================================================
    if (nPrimitives >= PARALLEL_BUILD_THRESHOLD &&
        depth < MAX_RECURSION_DEPTH_SERIAL) {
        // Launch left subtree asynchronously at deeper recursion so both
        // left and right subtrees can be built concurrently.
        std::future<BVHBuildNode *> futureLeft = std::async(
            std::launch::async,
            [&]() {
                return recursiveBuild(arena, primitiveInfo, start, mid,
                                      totalNodes, orderedPrims,
                                      orderedPrimsOffset, depth + 1);
            });
        BVHBuildNode *rightChild =
            recursiveBuild(arena, primitiveInfo, mid, end,
                           totalNodes, orderedPrims,
                           orderedPrimsOffset, depth + 1);
        BVHBuildNode *leftChild = futureLeft.get();
        node->InitInterior(dim, leftChild, rightChild);
    } else {
        // Sequential subtree construction
        node->InitInterior(dim,
            recursiveBuild(arena, primitiveInfo, start, mid,
                           totalNodes, orderedPrims,
                           orderedPrimsOffset, depth + 1),
            recursiveBuild(arena, primitiveInfo, mid, end,
                           totalNodes, orderedPrims,
                           orderedPrimsOffset, depth + 1));
    }
    return node;
}

BVHBuildNode *BVHAccel::HLBVHBuild(
    MemoryArena &arena, const std::vector<BVHPrimitiveInfo> &primitiveInfo,
    int *totalNodes,
    std::vector<std::shared_ptr<Primitive>> &orderedPrims) const {
    // Compute bounding box of all primitive centroids
    Bounds3f bounds;
    for (const BVHPrimitiveInfo &pi : primitiveInfo)
        bounds = Union(bounds, pi.centroid);

    // Compute Morton indices of primitives
    std::vector<MortonPrimitive> mortonPrims(primitiveInfo.size());
    ParallelFor([&](int i) {
        // Initialize _mortonPrims[i]_ for _i_th primitive
        PBRT_CONSTEXPR int mortonBits = 10;
        PBRT_CONSTEXPR int mortonScale = 1 << mortonBits;
        mortonPrims[i].primitiveIndex = primitiveInfo[i].primitiveNumber;
        Vector3f centroidOffset = bounds.Offset(primitiveInfo[i].centroid);
        mortonPrims[i].mortonCode = EncodeMorton3(centroidOffset * mortonScale);
    }, primitiveInfo.size(), 512);

    // Radix sort primitive Morton indices
    RadixSort(&mortonPrims);

    // Create LBVH treelets at bottom of BVH

    // Find intervals of primitives for each treelet
    std::vector<LBVHTreelet> treeletsToBuild;
    for (int start = 0, end = 1; end <= (int)mortonPrims.size(); ++end) {
#ifdef PBRT_HAVE_BINARY_CONSTANTS
      uint32_t mask = 0b00111111111111000000000000000000;
#else
      uint32_t mask = 0x3ffc0000;
#endif
      if (end == (int)mortonPrims.size() ||
            ((mortonPrims[start].mortonCode & mask) !=
             (mortonPrims[end].mortonCode & mask))) {
            // Add entry to _treeletsToBuild_ for this treelet
            int nPrimitives = end - start;
            int maxBVHNodes = 2 * nPrimitives;
            BVHBuildNode *nodes = arena.Alloc<BVHBuildNode>(maxBVHNodes, false);
            treeletsToBuild.push_back({start, nPrimitives, nodes});
            start = end;
        }
    }

    // Create LBVHs for treelets in parallel
    std::atomic<int> atomicTotal(0), orderedPrimsOffset(0);
    orderedPrims.resize(primitives.size());
    ParallelFor([&](int i) {
        // Generate _i_th LBVH treelet
        int nodesCreated = 0;
        const int firstBitIndex = 29 - 12;
        LBVHTreelet &tr = treeletsToBuild[i];
        tr.buildNodes =
            emitLBVH(tr.buildNodes, primitiveInfo, &mortonPrims[tr.startIndex],
                     tr.nPrimitives, &nodesCreated, orderedPrims,
                     &orderedPrimsOffset, firstBitIndex);
        atomicTotal += nodesCreated;
    }, treeletsToBuild.size());
    *totalNodes = atomicTotal;

    // Create and return SAH BVH from LBVH treelets
    std::vector<BVHBuildNode *> finishedTreelets;
    finishedTreelets.reserve(treeletsToBuild.size());
    for (LBVHTreelet &treelet : treeletsToBuild)
        finishedTreelets.push_back(treelet.buildNodes);
    return buildUpperSAH(arena, finishedTreelets, 0, finishedTreelets.size(),
                         totalNodes);
}

BVHBuildNode *BVHAccel::emitLBVH(
    BVHBuildNode *&buildNodes,
    const std::vector<BVHPrimitiveInfo> &primitiveInfo,
    MortonPrimitive *mortonPrims, int nPrimitives, int *totalNodes,
    std::vector<std::shared_ptr<Primitive>> &orderedPrims,
    std::atomic<int> *orderedPrimsOffset, int bitIndex) const {
    CHECK_GT(nPrimitives, 0);
    if (bitIndex == -1 || nPrimitives < maxPrimsInNode) {
        // Create and return leaf node of LBVH treelet
        (*totalNodes)++;
        BVHBuildNode *node = buildNodes++;
        Bounds3f bounds;
        int firstPrimOffset = orderedPrimsOffset->fetch_add(nPrimitives);
        for (int i = 0; i < nPrimitives; ++i) {
            int primitiveIndex = mortonPrims[i].primitiveIndex;
            orderedPrims[firstPrimOffset + i] = primitives[primitiveIndex];
            bounds = Union(bounds, primitiveInfo[primitiveIndex].bounds);
        }
        node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
        return node;
    } else {
        int mask = 1 << bitIndex;
        // Advance to next subtree level if there's no LBVH split for this bit
        if ((mortonPrims[0].mortonCode & mask) ==
            (mortonPrims[nPrimitives - 1].mortonCode & mask))
            return emitLBVH(buildNodes, primitiveInfo, mortonPrims, nPrimitives,
                            totalNodes, orderedPrims, orderedPrimsOffset,
                            bitIndex - 1);

        // Find LBVH split point for this dimension
        int searchStart = 0, searchEnd = nPrimitives - 1;
        while (searchStart + 1 != searchEnd) {
            CHECK_NE(searchStart, searchEnd);
            int mid = (searchStart + searchEnd) / 2;
            if ((mortonPrims[searchStart].mortonCode & mask) ==
                (mortonPrims[mid].mortonCode & mask))
                searchStart = mid;
            else {
                CHECK_EQ(mortonPrims[mid].mortonCode & mask,
                         mortonPrims[searchEnd].mortonCode & mask);
                searchEnd = mid;
            }
        }
        int splitOffset = searchEnd;
        CHECK_LE(splitOffset, nPrimitives - 1);
        CHECK_NE(mortonPrims[splitOffset - 1].mortonCode & mask,
                 mortonPrims[splitOffset].mortonCode & mask);

        // Create and return interior LBVH node
        (*totalNodes)++;
        BVHBuildNode *node = buildNodes++;
        BVHBuildNode *lbvh[2] = {
            emitLBVH(buildNodes, primitiveInfo, mortonPrims, splitOffset,
                     totalNodes, orderedPrims, orderedPrimsOffset,
                     bitIndex - 1),
            emitLBVH(buildNodes, primitiveInfo, &mortonPrims[splitOffset],
                     nPrimitives - splitOffset, totalNodes, orderedPrims,
                     orderedPrimsOffset, bitIndex - 1)};
        int axis = bitIndex % 3;
        node->InitInterior(axis, lbvh[0], lbvh[1]);
        return node;
    }
}

BVHBuildNode *BVHAccel::buildUpperSAH(MemoryArena &arena,
                                      std::vector<BVHBuildNode *> &treeletRoots,
                                      int start, int end,
                                      int *totalNodes) const {
    CHECK_LT(start, end);
    int nNodes = end - start;
    if (nNodes == 1) return treeletRoots[start];
    (*totalNodes)++;
    BVHBuildNode *node = arena.Alloc<BVHBuildNode>();

    // Compute bounds of all nodes under this HLBVH node
    Bounds3f bounds;
    for (int i = start; i < end; ++i)
        bounds = Union(bounds, treeletRoots[i]->bounds);

    // Compute bound of HLBVH node centroids, choose split dimension _dim_
    Bounds3f centroidBounds;
    for (int i = start; i < end; ++i) {
        Point3f centroid =
            (treeletRoots[i]->bounds.pMin + treeletRoots[i]->bounds.pMax) *
            0.5f;
        centroidBounds = Union(centroidBounds, centroid);
    }
    int dim = centroidBounds.MaximumExtent();
    // FIXME: if this hits, what do we need to do?
    // Make sure the SAH split below does something... ?
    CHECK_NE(centroidBounds.pMax[dim], centroidBounds.pMin[dim]);

    // Allocate _BucketInfo_ for SAH partition buckets
    PBRT_CONSTEXPR int nBuckets = 12;
    struct BucketInfo {
        int count = 0;
        Bounds3f bounds;
    };
    BucketInfo buckets[nBuckets];

    // Initialize _BucketInfo_ for HLBVH SAH partition buckets
    for (int i = start; i < end; ++i) {
        Float centroid = (treeletRoots[i]->bounds.pMin[dim] +
                          treeletRoots[i]->bounds.pMax[dim]) *
                         0.5f;
        int b =
            nBuckets * ((centroid - centroidBounds.pMin[dim]) /
                        (centroidBounds.pMax[dim] - centroidBounds.pMin[dim]));
        if (b == nBuckets) b = nBuckets - 1;
        CHECK_GE(b, 0);
        CHECK_LT(b, nBuckets);
        buckets[b].count++;
        buckets[b].bounds = Union(buckets[b].bounds, treeletRoots[i]->bounds);
    }

    // Compute costs for splitting after each bucket
    Float cost[nBuckets - 1];
    for (int i = 0; i < nBuckets - 1; ++i) {
        Bounds3f b0, b1;
        int count0 = 0, count1 = 0;
        for (int j = 0; j <= i; ++j) {
            b0 = Union(b0, buckets[j].bounds);
            count0 += buckets[j].count;
        }
        for (int j = i + 1; j < nBuckets; ++j) {
            b1 = Union(b1, buckets[j].bounds);
            count1 += buckets[j].count;
        }
        cost[i] = .125f +
                  (count0 * b0.SurfaceArea() + count1 * b1.SurfaceArea()) /
                      bounds.SurfaceArea();
    }

    // Find bucket to split at that minimizes SAH metric
    Float minCost = cost[0];
    int minCostSplitBucket = 0;
    for (int i = 1; i < nBuckets - 1; ++i) {
        if (cost[i] < minCost) {
            minCost = cost[i];
            minCostSplitBucket = i;
        }
    }

    // Split nodes and create interior HLBVH SAH node
    BVHBuildNode **pmid = std::partition(
        &treeletRoots[start], &treeletRoots[end - 1] + 1,
        [=](const BVHBuildNode *node) {
            Float centroid =
                (node->bounds.pMin[dim] + node->bounds.pMax[dim]) * 0.5f;
            int b = nBuckets *
                    ((centroid - centroidBounds.pMin[dim]) /
                     (centroidBounds.pMax[dim] - centroidBounds.pMin[dim]));
            if (b == nBuckets) b = nBuckets - 1;
            CHECK_GE(b, 0);
            CHECK_LT(b, nBuckets);
            return b <= minCostSplitBucket;
        });
    int mid = pmid - &treeletRoots[0];
    CHECK_GT(mid, start);
    CHECK_LT(mid, end);
    node->InitInterior(
        dim, this->buildUpperSAH(arena, treeletRoots, start, mid, totalNodes),
        this->buildUpperSAH(arena, treeletRoots, mid, end, totalNodes));
    return node;
}

int BVHAccel::flattenBVHTree(BVHBuildNode *node, int *offset) {
    LinearBVHNode *linearNode = &nodes[*offset];
    linearNode->bounds = node->bounds;
    int myOffset = (*offset)++;
    if (node->nPrimitives > 0) {
        CHECK(!node->children[0] && !node->children[1]);
        CHECK_LT(node->nPrimitives, 65536);
        linearNode->primitivesOffset = node->firstPrimOffset;
        linearNode->nPrimitives = node->nPrimitives;
    } else {
        // Create interior flattened BVH node
        linearNode->axis = node->splitAxis;
        linearNode->nPrimitives = 0;
        flattenBVHTree(node->children[0], offset);
        linearNode->secondChildOffset =
            flattenBVHTree(node->children[1], offset);
    }
    return myOffset;
}

BVHAccel::~BVHAccel() { FreeAligned(nodes); }

bool BVHAccel::Intersect(const Ray &ray, SurfaceInteraction *isect) const {
    if (!nodes) return false;
    ProfilePhase p(Prof::AccelIntersect);
    bool hit = false;
    Vector3f invDir(1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z);
    int dirIsNeg[3] = {invDir.x < 0, invDir.y < 0, invDir.z < 0};
    // Follow ray through BVH nodes to find primitive intersections
    int toVisitOffset = 0, currentNodeIndex = 0;
    int nodesToVisit[64];

    while (true) {
        const LinearBVHNode *node = &nodes[currentNodeIndex];

        // Phase 6: Prefetch siblings/next nodes
        PBRT_PREFETCH(&nodes[currentNodeIndex + 1]);
        if (node->nPrimitives == 0) {
            PBRT_PREFETCH(&nodes[node->secondChildOffset]);
        }

#if defined(PBRT_HAVE_SSE)
        bool hitNode = IntersectP_SSE(node, ray, invDir);
#else
        bool hitNode = node->bounds.IntersectP(ray, invDir, dirIsNeg);
#endif

        if (hitNode) {
            if (node->nPrimitives > 0) {
                // Intersect ray with primitives in leaf BVH node
                for (int i = 0; i < node->nPrimitives; ++i)
                    if (primitives[node->primitivesOffset + i]->Intersect(
                            ray, isect))
                        hit = true;
                if (toVisitOffset == 0) break;
                currentNodeIndex = nodesToVisit[--toVisitOffset];
            } else {
                // Put far BVH node on _nodesToVisit_ stack, advance to near
                // node
                if (dirIsNeg[node->axis]) {
                    nodesToVisit[toVisitOffset++] = currentNodeIndex + 1;
                    currentNodeIndex = node->secondChildOffset;
                } else {
                    nodesToVisit[toVisitOffset++] = node->secondChildOffset;
                    currentNodeIndex = currentNodeIndex + 1;
                }
            }
        } else {
            if (toVisitOffset == 0) break;
            currentNodeIndex = nodesToVisit[--toVisitOffset];
        }
    }
    return hit;
}

bool BVHAccel::IntersectP(const Ray &ray) const {
    if (!nodes) return false;
    ProfilePhase p(Prof::AccelIntersectP);
    Vector3f invDir(1.f / ray.d.x, 1.f / ray.d.y, 1.f / ray.d.z);
    int dirIsNeg[3] = {invDir.x < 0, invDir.y < 0, invDir.z < 0};
    int nodesToVisit[64];
    int toVisitOffset = 0, currentNodeIndex = 0;
    while (true) {
        const LinearBVHNode *node = &nodes[currentNodeIndex];

        // Phase 6: Prefetch siblings/next nodes
        PBRT_PREFETCH(&nodes[currentNodeIndex + 1]);
        if (node->nPrimitives == 0) {
            PBRT_PREFETCH(&nodes[node->secondChildOffset]);
        }

#if defined(PBRT_HAVE_SSE)
        bool hitNode = IntersectP_SSE(node, ray, invDir);
#else
        bool hitNode = node->bounds.IntersectP(ray, invDir, dirIsNeg);
#endif

        if (hitNode) {
            // Process BVH node _node_ for traversal
            if (node->nPrimitives > 0) {
                for (int i = 0; i < node->nPrimitives; ++i) {
                    if (primitives[node->primitivesOffset + i]->IntersectP(
                            ray)) {
                        return true;
                    }
                }
                if (toVisitOffset == 0) break;
                currentNodeIndex = nodesToVisit[--toVisitOffset];
            } else {
                if (dirIsNeg[node->axis]) {
                    /// second child first
                    nodesToVisit[toVisitOffset++] = currentNodeIndex + 1;
                    currentNodeIndex = node->secondChildOffset;
                } else {
                    nodesToVisit[toVisitOffset++] = node->secondChildOffset;
                    currentNodeIndex = currentNodeIndex + 1;
                }
            }
        } else {
            if (toVisitOffset == 0) break;
            currentNodeIndex = nodesToVisit[--toVisitOffset];
        }
    }
    return false;
}

std::shared_ptr<BVHAccel> CreateBVHAccelerator(
    std::vector<std::shared_ptr<Primitive>> prims, const ParamSet &ps) {
    std::string splitMethodName = ps.FindOneString("splitmethod", "sah");
    BVHAccel::SplitMethod splitMethod;
    if (splitMethodName == "sah")
        splitMethod = BVHAccel::SplitMethod::SAH;
    else if (splitMethodName == "hlbvh")
        splitMethod = BVHAccel::SplitMethod::HLBVH;
    else if (splitMethodName == "middle")
        splitMethod = BVHAccel::SplitMethod::Middle;
    else if (splitMethodName == "equal")
        splitMethod = BVHAccel::SplitMethod::EqualCounts;
    else {
        Warning("BVH split method \"%s\" unknown.  Using \"sah\".",
                splitMethodName.c_str());
        splitMethod = BVHAccel::SplitMethod::SAH;
    }

    int maxPrimsInNode = ps.FindOneInt("maxnodeprims", 4);
    return std::make_shared<BVHAccel>(std::move(prims), maxPrimsInNode, splitMethod);
}

}  // namespace pbrt
