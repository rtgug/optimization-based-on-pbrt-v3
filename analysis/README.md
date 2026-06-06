# PBRT-v3 优化实验 — 环境搭建与分析框架使用指南

## 1. 编译环境

| 组件 | 版本/来源 |
|------|-----------|
| Windows | 11 25H2 |
| 编译器 | MinGW-w64 GCC 15.2.0 (x86_64-win32-seh, thread model: win32) |
| CMake | 4.3.1 |
| Make | mingw32-make (GNU Make 4.4.1) |

## 2. Python 环境

### 2.1 安装依赖

```bash
pip install numpy matplotlib scipy imageio imageio-freeimage
```

| 包 | 用途 |
|----|------|
| `numpy` | 数组运算，像素数据处理 |
| `matplotlib` | 所有图表绘制（收敛曲线、热力图、柱状图、散点图） |
| `scipy` | （预留）高级统计/信号处理 |
| `imageio` + `imageio-freeimage` | PNG 读写；EXR 可通过此后端读取（但当前 Pipeline 默认使用 PFM，纯 Python 解析、零依赖） |

> **注意**：`pip install OpenEXR` 在 Windows 上从源码编译经常失败，当前 Pipeline 已改用 **PFM 格式**（Portable Float Map），由 `analysis/io_utils.py` 中的纯 Python 解析器读取，完全不依赖外部库。

## 3. 编译项目

```bash
# 1. 初始化子模块（首次克隆后只需执行一次）
cd pbrt-v3
git submodule update --init --recursive

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置 CMake（MinGW 用户必须指定 Generator）
cmake -G "MinGW Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_MAKE_PROGRAM=mingw32-make \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      ..

# 4. 编译（-j 后跟 CPU 核心数）
mingw32-make -j$(nproc)
```

编译产物在 `build/` 下：

| 可执行文件 | 说明 |
|-----------|------|
| `pbrt.exe` | 主渲染器 |
| `pbrt_test.exe` | 单元测试 |
| `imgtool.exe` | 图像格式转换 |
| `bsdftest.exe` | BSDF 测试 |
| `obj2pbrt.exe` | OBJ 模型转换 |
| `cyhair2pbrt.exe` | 毛发转换 |

### 3.1 MinGW 编译注意事项

本项目已针对 MinGW 做了若干兼容性修复（见 `CMakeLists.txt`、`src/ext/ptex/src/ptex/PtexInt.h`、
`src/ext/openexr/OpenEXR/IlmImf/ImfSystemSpecific.h`、`src/core/parser.cpp`）。
如果重装 MinGW 或切换版本，请确认以下设置：

- **线程模型**：需要在 CMakeLists.txt 中设置 `-static` 链接标志（第 77 行附近），否则 `libwinpthread-1.dll` 版本不匹配会导致 `std::thread` hang。
- **文件读取**：MinGW 不走 `MapViewOfFile`（仅 MSVC 使用），改用标准 `fopen`/`fread`。

## 4. 分析框架 (`analysis/`)

```
analysis/
├── __init__.py          # 包声明
├── config.py             # 【核心】实验参数配置
├── io_utils.py           # PFM/EXR/PNG 图片读写
├── metrics.py            # MSE / RMSE / PSNR / 逐像素误差
├── plots.py              # 7 类图表生成函数
├── batch_render.py       # 批量渲染脚本（可独立运行）
└── pipeline.py           # 一键式全流程主控
```

### 4.1 目录结构

```
pbrt-v3/
├── analysis/             # 分析框架源码
├── scenes/               # 场景文件 (.pbrt)
├── build/                # CMake 构建目录 & 编译产物
└── results/              # Pipeline 输出（自动创建）
    ├── images/           # 渲染图像 (PFM)
    ├── data/             # 数值结果 (JSON)
    └── plots/            # 图表 (PNG)
```

### 4.2 配置实验参数

编辑 `analysis/config.py`：

```python
# ── 场景 ──
SCENE_FILE  = SCENES_DIR / "killeroo-simple.pbrt"   # 你的场景文件
SCENE_NAME  = "killeroo"

# ── 分辨率覆盖（None = 使用场景文件中的设定） ──
OVERRIDE_RESOLUTION = (400, 400)

# ── 参考图（Ground Truth） ──
REFERENCE_SPP  = 8192

# ── SPP 扫描列表（收敛曲线横轴） ──
SPP_SWEEP = [4, 16, 64, 128, 256, 512, 1024]

# ── 实验分支（每个优化方向一条） ──
EXPERIMENTS = {
    "baseline": {
        "label": "Baseline (fixed-depth)",
        "branch": "master",
    },
    "russian_roulette": {
        "label": "Russian Roulette",
        "branch": "feat/russian-roulette",
    },
}
```

### 4.3 运行 Pipeline

```bash
# 从项目根目录执行

# 一键全流程：reference → batch → metrics → plots
python -m analysis.pipeline

# 或分步执行（哪个挂了补哪个，不会重复已完成的渲染）
python -m analysis.pipeline --step reference    # 仅渲染参考图
python -m analysis.pipeline --step batch        # 批量渲染所有 SPP × 所有实验
python -m analysis.pipeline --step metrics      # 计算 MSE / RMSE / PSNR
python -m analysis.pipeline --step plots        # 生成所有图表
```

**缓存机制**：`batch` 步骤会跳过 `results/images/` 下已存在的图片。如需强制重来，手动删除 `results/` 目录即可。

### 4.4 单独使用批量渲染

```bash
# 跑 baseline 实验的全部 SPP
python -m analysis.batch_render --experiment baseline

# 只跑指定的 SPP
python -m analysis.batch_render --experiment baseline --spp 64,256,1024

# 渲染参考图
python -m analysis.batch_render --reference

# 指定线程数
python -m analysis.batch_render --nthreads 8 --spp 64
```

## 5. 生成的图表

| 图表 | 函数 | 说明 |
|------|------|------|
| **收敛曲线** | `plot_convergence()` | X=SPP, Y=RMSE/MSE/PSNR, log-log 坐标 |
| **误差热力图** | `plot_error_heatmap()` | 逐像素平方误差，伪彩图 |
| **并排热力图对比** | `plot_side_by_side_heatmaps()` | Baseline vs 优化后 误差空间分布 |
| **采样模式散点图** | `plot_sampling_pattern()` | 2D 采样点分布（评估 sampler 质量） |
| **耗时堆叠柱状图** | `plot_timing_stack()` | 建树时间 vs 渲染时间 |
| **消融实验柱状图** | `plot_ablation_bars()` | 多配置 MSE 对比 |
| **复杂度缩放图** | `plot_scaling()` | N vs 耗时，log-log 双对数坐标 |
| **汇总四宫格** | `plot_summary()` | 收敛曲线 + 热力图 + 消融 + 耗时 |

所有图表函数均可独立调用，详见 `analysis/plots.py` 中的函数签名。

## 6. metrics 模块 API

```python
from analysis.metrics import mse, rmse, psnr, rel_mse, per_pixel_error
from analysis.io_utils import read_image, read_luminance

img  = read_image("result.pfm")   # → np.ndarray (H, W, 3) float32
ref  = read_image("reference.pfm")
err  = per_pixel_error(img, ref)  # → np.ndarray (H, W) float32

print(f"MSE={mse(img, ref):.6f}, PSNR={psnr(img, ref):.2f} dB")
```

## 7. 典型工作流示例

以下是一个完整的"Top 4 俄罗斯轮盘赌"实验流程：

```bash
# 1. 创建分支
git checkout -b feat/russian-roulette

# 2. 修改代码（例如 src/integrators/path.cpp）

# 3. 编译
cd build && mingw32-make -j$(nproc) && cd ..

# 4. 在 analysis/config.py 中添加实验：
#    EXPERIMENTS["russian_roulette"] = {...}

# 5. 跑实验
python -m analysis.pipeline

# 6. 查看结果
#    results/plots/ 下的收敛曲线会同时包含 baseline 和 russian_roulette 两条线
```

## 8. 场景文件格式

PBRT-v3 使用 `.pbrt` 文本格式。最简单的测试场景：

```
LookAt 0 5 15   0 0 0   0 1 0
Camera "perspective" "float fov" [45]
Film "image"
    "integer xresolution" [400] "integer yresolution" [400]
    "string filename" "output.pfm"         # ← 注意用 .pfm 扩展名
Sampler "halton" "integer pixelsamples" [64]
Integrator "path"
WorldBegin
    LightSource "distant" "point from" [0 20 0] "point to" [0 0 0] "color L" [30 30 30]
    Material "matte" "color Kd" [0.8 0.3 0.3]
    Shape "sphere" "float radius" [2]
WorldEnd
```

## 9. 清理

```bash
# 清理所有实验结果
rm -rf results/

# 清理 CMake 构建产物
rm -rf build/*
cd build && cmake ... && mingw32-make -j$(nproc)
```
