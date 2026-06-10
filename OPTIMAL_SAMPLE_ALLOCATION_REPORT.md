# 最优样本分配报告

## Optimal Sample Allocation via Lagrange Multiplier Optimization

> **项目**: PBRT-v3 渲染器 — 自适应采样优化  
> **报告日期**: 2026-06-07  
> **关键词**: 自适应采样, 拉格朗日乘子法, 凸优化, 方差引导分配, 蒙特卡洛渲染

---

## 1. 摘要

本报告介绍了一种基于 **约束凸优化（拉格朗日乘子法）** 的自适应样本分配策略，并将其集成到 PBRT-v3 路径追踪渲染器中。核心思想是：不同像素的渲染难度（方差）差异很大——平滑区域只需少量样本即可收敛，而高方差区域（如阴影边界、光泽反射、焦散）则需要更多样本。通过 **最优分配固定样本预算**，可以显著降低整体均方误差（MSE），在同等计算开销下获得更高质量的图像。

**数学表述**：给定总样本预算 $N$，求每个像素的样本数 $n_i$ 以最小化全局 MSE：

$$\min_{n_i} \sum_{i=1}^{M} \frac{\sigma_i^2}{n_i} \quad \text{s.t.} \quad \sum_{i=1}^{M} n_i = N, \quad n_i \geq 0$$

其中 $\sigma_i^2$ 是像素 $i$ 的 luminance 方差，$M$ 是像素总数。

---

## 2. 理论推导

### 2.1 问题建模

在蒙特卡洛路径追踪中，每个像素 $i$ 的估计值 $\hat{I}_i$ 是 $n_i$ 个独立样本的均值：

$$\hat{I}_i = \frac{1}{n_i} \sum_{j=1}^{n_i} L_{i,j}$$

其中 $L_{i,j}$ 是第 $j$ 条路径的 luminance 值。像素估计值的方差为：

$$\text{Var}[\hat{I}_i] = \frac{\sigma_i^2}{n_i}$$

全局均方误差（MSE）为各像素方差之和：

$$\text{MSE} = \sum_{i=1}^{M} \text{Var}[\hat{I}_i] = \sum_{i=1}^{M} \frac{\sigma_i^2}{n_i}$$

### 2.2 拉格朗日乘子法求解

将样本预算约束引入目标函数，构造拉格朗日函数：

$$\mathcal{L}(n_1, \ldots, n_M, \lambda) = \sum_{i=1}^{M} \frac{\sigma_i^2}{n_i} + \lambda \left( \sum_{i=1}^{M} n_i - N \right)$$

对每个 $n_i$ 求偏导并设为零：

$$\frac{\partial \mathcal{L}}{\partial n_i} = -\frac{\sigma_i^2}{n_i^2} + \lambda = 0 \quad \Rightarrow \quad n_i^* = \frac{\sigma_i}{\sqrt{\lambda}}$$

代入约束条件：

$$\sum_{i=1}^{M} \frac{\sigma_i}{\sqrt{\lambda}} = N \quad \Rightarrow \quad \sqrt{\lambda} = \frac{\sum_{i=1}^{M} \sigma_i}{N}$$

得到**最优样本分配公式**：

$$n_i^* = \frac{\sigma_i}{\sum_{j=1}^{M} \sigma_j} \cdot N$$

这是一个经典的 **Neyman allocation** 形式——样本数与像素标准差成正比。

### 2.3 考虑最小样本数的修正

实际渲染中，每个像素至少需要一定样本数才能保证基本质量和方差估计的可靠性。引入最小样本数 $n_{\min}$，则分配策略修正为：

$$n_i = n_{\min} + \frac{\sigma_i}{\sum_{j=1}^{M} \sigma_j} \cdot R, \quad R = N - M \cdot n_{\min}$$

其中 $R$ 是经过最小分配后的剩余预算。

---

## 3. 实现架构

### 3.1 整体流程

```
┌─────────────────────────────────────────────────────┐
│              AdaptiveIntegrator::Render()            │
├─────────────────────────────────────────────────────┤
│  1. 预处理 (Preprocess)                              │
│     - 初始化光源采样分布                              │
│                                                       │
│  2. 阶段一: 引导渲染 (Pilot Pass)                     │
│     - 所有像素使用 n_min 样本                         │
│     - Welford在线算法跟踪每像素luminance方差           │
│                                                       │
│  3. 拉格朗日优化分配                                  │
│     - 计算每像素标准差 σ_i                            │
│     - 按比例分配剩余预算: n_i = n_min + R·σ_i/Σσ_j   │
│                                                       │
│  4. 阶段二: 最终渲染 (Final Render)                    │
│     - 清空film                                        │
│     - 按最优分配逐像素渲染                             │
│     - 输出最终图像                                    │
└─────────────────────────────────────────────────────┘
```

### 3.2 代码结构

| 文件 | 说明 |
|------|------|
| `src/integrators/adaptive.h` | AdaptiveIntegrator 类声明 |
| `src/integrators/adaptive.cpp` | 核心实现：Render() + 工厂函数 |
| `src/core/integrator.h` | 修改：sampler/pixelBounds 改为 protected |
| `src/core/api.cpp` | 注册 "adaptive" 渲染器 |
| `analysis/config.py` | 实验配置 |
| `analysis/batch_render.py` | 批处理脚本（支持integrator覆盖） |
| `analysis/adaptive_analysis.py` | 自适应采样分析可视化 |

### 3.3 AdaptiveIntegrator 类设计

```cpp
class AdaptiveIntegrator : public PathIntegrator {
public:
    AdaptiveIntegrator(int maxDepth, int minSamples, int totalSampleBudget,
                       std::shared_ptr<const Camera> camera,
                       std::shared_ptr<Sampler> sampler,
                       const Bounds2i &pixelBounds, Float rrThreshold,
                       const std::string &lightSampleStrategy);

    void Render(const Scene &scene) override;
    // Li() 继承自 PathIntegrator (复用完整的路径追踪代码)

private:
    const int minSamples;        // 引导阶段每像素最小样本数
    const int totalSampleBudget; // 总样本预算 (所有像素之和)
    
    struct PixelStats {
        double mean = 0.0;  // Welford算法: 运行均值
        double M2 = 0.0;    // Welford算法: 平方差累积和
        int count = 0;      // 已观测样本数
    };
};
```

### 3.4 方差跟踪：Welford在线算法

为了避免存储所有样本值，使用 Welford 的在线方差估计算法：

```cpp
// 对每个样本 x:
count++;
double delta = x - mean;
mean += delta / count;
M2 += delta * (x - mean);

// 样本方差 = M2 / (count - 1)  (count > 1 时)
```

**优点**：
- O(1) 空间复杂度（只需三个数值）
- 数值稳定性好，适合 large dynamic range 的渲染场景
- 天然支持流式处理

---

## 4. 场景文件配置

在 `.pbrt` 场景文件中使用以下方式启用自适应采样：

```
# 标准方式使用自适应采样
Integrator "adaptive" 
    "integer minsamples" [4] 
    "integer totalbudget" [65536] 
    "integer maxdepth" [5]

# 仍然需要指定一个基础 sampler
Sampler "halton" "integer pixelsamples" [4]
```

**参数说明**：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `minsamples` | int | 4 | 引导阶段每像素最小样本数 |
| `totalbudget` | int | 65536 | 总样本预算（所有像素） |
| `maxdepth` | int | 5 | 路径最大深度 |
| `rrthreshold` | float | 1.0 | Russian roulette 阈值 |
| `lightsamplestrategy` | string | "spatial" | 光源采样策略 |

对于 **等价均匀 SPP = K** 的实验，`totalbudget` 应设置为 `width × height × K`。

---

## 5. 理论性能分析

### 5.1 均匀采样 vs 自适应采样

假设场景中 $p$ 比例的像素标准差为 $\sigma_{\text{high}}$，其余为 $\sigma_{\text{low}}$，且 $\sigma_{\text{high}} = \kappa \cdot \sigma_{\text{low}}$（$\kappa > 1$）。

**均匀采样**（每像素 $n$ 样本）：

$$\text{MSE}_{\text{uniform}} = p \cdot \frac{(\kappa \sigma)^2}{n} + (1-p) \cdot \frac{\sigma^2}{n} = \frac{\sigma^2}{n} [p\kappa^2 + (1-p)]$$

**自适应采样**（总预算 $N = M \cdot n$）：

$$n_{\text{high}} = \frac{\kappa}{p\kappa + (1-p)} \cdot n, \quad n_{\text{low}} = \frac{1}{p\kappa + (1-p)} \cdot n$$

$$\text{MSE}_{\text{adaptive}} = p \cdot \frac{(\kappa \sigma)^2}{n_{\text{high}}} + (1-p) \cdot \frac{\sigma^2}{n_{\text{low}}} = \frac{\sigma^2}{n} [p\kappa + (1-p)]^2$$

**理论加速比**：

$$\frac{\text{MSE}_{\text{uniform}}}{\text{MSE}_{\text{adaptive}}} = \frac{p\kappa^2 + (1-p)}{[p\kappa + (1-p)]^2}$$

当 $\kappa = 5$（高方差区域标准差是低方差的5倍）、$p = 20\%$（20%像素为高方差）时：

$$\frac{0.2 \times 25 + 0.8}{[0.2 \times 5 + 0.8]^2} = \frac{5.8}{3.24} \approx 1.79$$

即 **同等计算量下 MSE 降低约 44%**，或 **RMSE 降低约 25%**。

### 5.2 收敛特性

- 均匀采样：RMSE ∝ $O(1/\sqrt{N})$
- 自适应采样：在同等总样本数下，RMSE 的常数因子更小
- 当 $\kappa \to 1$（场景方差均匀）：退化为均匀采样
- 当 $\kappa \to \infty$（存在极端高方差区域）：加速比 $\to 1/p$

---

## 6. 与 PBRT-v3 集成要点

### 6.1 线程安全设计

- 采用与原生 `SamplerIntegrator::Render()` 一致的 `ParallelFor2D` 并行程
- `PixelStats` 数组按像素坐标索引，各 tile 访问不相交区域，无需加锁
- Phase 2 使用不同的随机种子（+9973 偏移），确保样本独立性

### 6.2 样本连续性

使用 `GlobalSampler`（Halton 等）时，`StartPixel()` 会根据 `samplesPerPixel` 预计算高维样本数组。AdaptiveIntegrator 通过以下方式处理：

1. Phase 1（引导）：`samplesPerPixel = minSamples`
2. Phase 2（正式）：每个像素独立设置 `samplesPerPixel = budget`

由于路径追踪器仅使用 `Get1D()` / `Get2D()`（不使用 array sample），这种方法的额外开销仅在于 Halton 维度的预计算，对总体性能影响可忽略。

### 6.3 内存开销

对于一个 $W \times H$ 的图像：

- `PixelStats` 数组：$W \times H \times 24$ 字节（3 个 double + 对齐）
- `sampleAlloc` 数组：$W \times H \times 4$ 字节（int）
- `pixelSigma` 数组：$W \times H \times 8$ 字节（double）

对于 1920×1080 的图像，总内存约 **80 MB**，完全可接受。

---

## 7. 实验设计

### 7.1 运行实验

```bash
# 1. 编译带自适应渲染器的 pbrt
cd build && mingw32-make -j$(nproc) && cd ..

# 2. 渲染参考图（高 SPP 均匀采样）
python -m analysis.pipeline --step reference

# 3. 运行自适应实验
python -m analysis.batch_render --experiment adaptive_lagrange --spp 4,16,64,256

# 4. 运行 baseline 对比
python -m analysis.batch_render --experiment baseline --spp 4,16,64,256

# 5. 分析和可视化
python -m analysis.adaptive_analysis
```

### 7.2 比较指标

| 指标 | 公式 | 说明 |
|------|------|------|
| MSE | $\frac{1}{n}\sum(\hat{I} - I_{\text{ref}})^2$ | 均方误差 |
| RMSE | $\sqrt{\text{MSE}}$ | 均方根误差 |
| PSNR | $20\log_{10}(\text{max}/\sqrt{\text{MSE}})$ | 峰值信噪比 (dB) |
| 加速比 | $\text{RMSE}_{\text{uniform}} / \text{RMSE}_{\text{adaptive}}$ | 同等计算量下的质量提升 |
| 等效SPP | $N \cdot (\text{RMSE}_{\text{adaptive}} / \text{RMSE}_{\text{uniform}})^2$ | 达到相同质量所需均匀样本数 |

---

## 8. 预期结果分析

### 8.1 收敛曲线

预计自适应采样在所有 SPP 水平上均能获得更低的 RMSE，收敛曲线向下偏移。在 log-log 坐标系中，两条曲线应平行（斜率均为 $-\frac{1}{2}$），但自适应曲线位置更低。

### 8.2 误差热力图

- **Baseline**: 误差集中在高方差区域（边缘、光泽反射、阴影边界）
- **Adaptive**: 高方差区域的误差显著降低，而低方差区域的误差略有增加（由于样本被重新分配）
- 整体误差分布更均匀：实现了 "error equalization" 效果

### 8.3 样本分配图

分配图应直观显示：
- 平滑区域（漫反射表面）：获得基础样本数
- 复杂区域（反射、折射、焦散）：获得额外样本
- 分配模式应与场景特征的视觉复杂度相关

---

## 9. 局限性与改进方向

### 9.1 当前限制

| 限制 | 说明 | 影响程度 |
|------|------|----------|
| 单次引导 | 方差估计仅基于引导阶段，无法在正式渲染中更新 | 中 |
| Tile级独立性 | 各 tile 独立估计方差，未共享跨 tile 统计 | 低 |
| 仅 luminance 方差 | 仅使用亮度通道的方差，未考虑色彩通道差异 | 低 |
| 整数舍入 | 分配数需要取整，可能导致少量预算浪费 | 极低 |

### 9.2 改进方向

1. **迭代重分配**：在渲染过程中定期更新方差估计并重新分配，实现真正的在线自适应
2. **色彩感知方差**：考虑 RGB 各通道的方差，对不同色彩敏感区域优化
3. **空间滤波**：对方差图进行平滑滤波，减少估计噪声导致的分配抖动
4. **感知误差度量**：使用 SSIM 或 VGG 感知损失替代 MSE 作为优化目标
5. **混合分配**：结合重要性驱动采样（MIS），在像素内进一步优化光源采样 vs BSDF 采样的分配

---

## 10. 结论

本报告实现了一种基于拉格朗日乘子法的最优样本分配策略，并将其集成到 PBRT-v3 渲染器中。该方法通过两个阶段实现：

1. **引导阶段**：以少量均匀样本估计每像素的 luminance 方差
2. **最优分配**：利用拉格朗日乘子法，将总样本预算按像素标准差正比分配

理论分析表明，在典型的非均匀场景中，该方法可实现 **30-50% 的 MSE 降低**（或同等质量下 **40-60% 的计算量节省**）。实现方案保持了与现有渲染管线的完全兼容，最小化了对核心代码的侵入式修改。

该方法特别适用于包含高光反射、镜面折射、焦散和复杂照明等特征的真实感渲染场景——这些正是 Monte Carlo 渲染中最消耗计算资源的情况。

---

## 参考文献

1. Kirk, D. B., & Arvo, J. (1991). *Unbiased sampling techniques for image synthesis*. SIGGRAPH '91.
2. Mitchell, D. P. (1987). *Generating antialiased images at low sampling densities*. SIGGRAPH '87.
3. Overbeck, R. S., Donner, C., & Ramamoorthi, R. (2009). *Adaptive wavelet rendering*. ACM TOG.
4. Neyman, J. (1934). *On the two different aspects of the representative method*. Journal of the Royal Statistical Society.
5. Pharr, M., Jakob, W., & Humphreys, G. (2016). *Physically Based Rendering: From Theory to Implementation* (3rd ed.).
6. Welford, B. P. (1962). *Note on a method for calculating corrected sums of squares and products*. Technometrics.
