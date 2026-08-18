# JixelLight

JixelLight 是面向 Windows / macOS 的专业摄影后期桌面软件，核心工作流以 **RAW 照片、批量后期、非破坏编辑** 为中心。

当前开发版本：**v0.1.0-alpha.5**

## alpha.5：完整 RAW Processing Graph 基础

alpha.5 不再把“16-bit RGB”误当作 RAW 域。RAW 输入会保持在线性宽色域处理链中，直到最终显示转换：

`RAW → Camera WB / Camera Matrix → Demosaic → Linear ProPhoto RGB → Exposure / Tonal → Hue / Saturation / Vibrance / HSL → Master / RGB Curves → Display sRGB`

### RAW 开发

- LibRaw 解码常见 RAW：ARW / CR2 / CR3 / CRW / NEF / NRW / RAF / RW2 / ORF / DNG / PEF / SRW / RWL / 3FR / ERF / KDC / MOS / MRW / X3F / IIQ / RAW。
- Camera White Balance 作为 RAW baseline。
- `use_camera_matrix = 3`，优先使用 embedded / built-in camera color data。
- AHD demosaic reference path。
- LibRaw highlight blend。
- 禁止 auto-bright，禁止自动 maximum 调整。
- gamma 明确设为 linear。
- RAW 工作空间改为 **Linear ProPhoto RGB 16-bit**，不先渲染成 gamma-encoded sRGB 成片。

### 明暗与颜色

当前 CPU reference pipeline 已包含：

- Exposure：scene-linear EV，+1 EV 在工作数据上真正 ×2。
- Temperature / Tint：相机白平衡 baseline 之后的线性 chromatic-adaptation delta。
- Contrast / Highlights / Shadows / Whites / Blacks。
- Highlight Recovery。
- Global Hue。
- Saturation。
- Vibrance。
- 8 色 HSL Color Mixer：Red / Orange / Yellow / Green / Aqua / Blue / Purple / Magenta，每色独立 Hue / Saturation / Luminance。
- 感知颜色层基于 OKLab/OKLCh 思路，避免在最终 sRGB/HSV 成片上硬拉颜色。

### 曲线

- Master Tone Curve。
- Red Curve。
- Green Curve。
- Blue Curve。
- 5 个可拖动控制点。
- 曲线参数进入项目数据库、复制/粘贴、批量同步和 Bug Snapshot。

### Professional Scopes

- RGB / Luminance Histogram：1024 bins。
- Histogram 读取**当前最终显示结果**，所以曝光、HSL、饱和度、曲线变化都会实时反映。
- Shadow / Highlight clipping 百分比。
- 架构保留以后切换 RAW Source / Working / Display scopes 的能力。

### Diagnostics

Bug ZIP 现在记录：

- Source file / project。
- 完整 AdjustmentState，包括 HSL 和曲线。
- 当前 Processing Graph。
- Working Space。
- Display Output。
- 1024-bin scopes 阶段。
- Session Log / Action Trace / Preview。

程序内 RAW 状态会直接显示 `RAW · Linear ProPhoto · 16-bit`，不再只显示模糊的 `RAW · 16-bit`。

## 中文 / English

- 首次启动默认中文。
- 顶部可即时切换中文 / English。
- 语言选择通过 QSettings 持久化。

## 导入与操作

- C++ 原生文件选择窗口导入。
- Ctrl/Cmd + O。
- 可拖放照片到窗口。
- 批量导入、复制参数、粘贴参数、同步全部。
- JPEG 全分辨率导出。
- `🐞 报告当前问题` 生成并明确提示 Diagnostic ZIP 路径。

## 构建依赖

- CMake 3.24+
- C++20
- Qt 6.5+
- LibRaw（vcpkg manifest）

### macOS Apple Silicon

```bash
git clone https://github.com/microsoft/vcpkg.git .vcpkg
./.vcpkg/bootstrap-vcpkg.sh -disableMetrics
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-static \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/triplets"
cmake --build build --config Release --parallel
```

### Windows x64

```powershell
git clone https://github.com/microsoft/vcpkg.git .vcpkg
.\.vcpkg\bootstrap-vcpkg.bat -disableMetrics
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/triplets"
cmake --build build --config Release --parallel
```

## 仍然明确属于后续专业化的部分

alpha.5 已经把“RAW 调整基座”从 JPEG-like display RGB 改成线性宽色域 Graph，但仍是 CPU reference pipeline。后续会继续在不改变参数语义的前提下升级：

1. 更完整的 Camera/DCP Profile 管理。
2. 真正的传感器级 highlight reconstruction 与更高级 demosaic。
3. Display ICC / LittleCMS 输出管理。
4. RGBA16F / Qt RHI GPU Processing Graph。
5. 更高性能 RAW preview cache / 后台解码。
6. Exiv2 完整 EXIF 浏览与筛选。

架构说明见 [`docs/RAW_PIPELINE_ALPHA5.md`](docs/RAW_PIPELINE_ALPHA5.md) 与 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。
