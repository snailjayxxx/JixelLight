# JixelLight

JixelLight 是面向 Windows / macOS 的专业摄影后期桌面软件，核心工作流以 **RAW 照片、批量后期、非破坏编辑** 为中心。

当前开发版本：**v0.1.0-alpha.6**

## alpha.6：ICC 色彩管理与 RAW 元数据基础

alpha.6 在 alpha.5 的线性宽色域 RAW Processing Graph 上继续补齐“专业软件必须知道颜色从哪里来、往哪里去”的基础设施：

- 引入 **LittleCMS 2** 作为 ICC profile 验证与后续显示/输出变换基础。
- 新增统一 `ColorManagement` 模块。
- JPEG 导出可选择：
  - sRGB
  - Display P3
  - Adobe RGB (1998)
  - ProPhoto RGB
- 导出图片带目标 `QColorSpace` / ICC profile，JPEG 质量可调 1–100。
- Action Trace 记录输出色彩空间、ICC 字节数、JPEG 质量和写入错误。
- 引入 **Exiv2** 元数据层，并启用 BMFF 支持，为 CR3 等格式的 metadata 做准备。
- 右侧新增照片信息：相机、镜头、快门、光圈、ISO、焦距、拍摄时间、尺寸，以及 RAW bit depth / working space / demosaic。
- Windows 路径处理使用 `QFile` + 内存映射后交给 Exiv2，避免依赖窄字符串文件路径。

### alpha.6 当前边界

编辑 Graph 仍然在最终 preview 阶段映射为 **ICC sRGB display result**。alpha.6 的 P3 / Adobe RGB / ProPhoto 导出是对该显示结果进行颜色管理转换并嵌入目标 ICC，**不会恢复已经在 sRGB display mapping 阶段压掉的超出色域信息**。

因此当前能力应该准确理解为：

`RAW Linear ProPhoto Working Data → Edit Graph → ICC sRGB Preview → ICC-managed Export Encoding`

后续会把 export 分支直接接到显示映射之前的宽色域工作数据，实现真正的 native wide-gamut export：

`RAW Linear ProPhoto Working Data → Edit Graph → Output Transform (sRGB / P3 / Adobe RGB / ProPhoto / TIFF...)`

## alpha.5：完整 RAW Processing Graph 基础

RAW 输入保持在线性宽色域处理链中，直到最终显示转换：

`RAW → Camera WB / Camera Matrix → Demosaic → Linear ProPhoto RGB → Exposure / Tonal → Hue / Saturation / Vibrance / HSL → Master / RGB Curves → ICC sRGB Preview`

这条链的重点是：**曝光、色温/色调、色相、饱和度、自然饱和度、HSL 和曲线都不建立在 gamma-encoded sRGB 成片上。**

### RAW 开发

- LibRaw 解码常见 RAW：ARW / CR2 / CR3 / CRW / NEF / NRW / RAF / RW2 / ORF / DNG / PEF / SRW / RWL / 3FR / ERF / KDC / MOS / MRW / X3F / IIQ / RAW。
- Camera White Balance 作为 RAW baseline。
- `use_camera_matrix = 3`，优先使用 embedded / built-in camera color data。
- AHD demosaic reference path。
- LibRaw highlight blend。
- 禁止 auto-bright，禁止自动 maximum 调整。
- gamma 明确设为 linear。
- RAW 工作空间：**Linear ProPhoto RGB 16-bit**。

### 明暗与颜色

当前 CPU reference pipeline 包含：

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

Bug ZIP / Action Trace 当前覆盖：

- Source file / project。
- 完整 AdjustmentState，包括 HSL 和曲线。
- 当前 Processing Graph。
- Working Space。
- Display Output。
- Metadata read warning / RAW decode metadata。
- Export ICC target / profile bytes / JPEG quality。
- 1024-bin scopes 阶段。
- Session Log / Action Trace / Preview。

程序内 RAW 状态直接显示 `RAW · Linear ProPhoto · 16-bit`。

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
- ICC 输出色彩空间选择。
- JPEG 质量选择。
- EXIF / RAW 元数据显示。
- `🐞 报告当前问题` 生成并明确提示 Diagnostic ZIP 路径。

## 构建依赖

- CMake 3.24+
- C++20
- Qt 6.5+
- LibRaw（vcpkg manifest）
- LittleCMS 2（vcpkg `lcms`）
- Exiv2（vcpkg，含 `bmff` feature）

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

## 接下来继续专业化的部分

1. Camera/DCP Profile 管理与按相机模型选择 Profile。
2. 从 Linear ProPhoto working data 直接分叉的 native wide-gamut export。
3. 显示器 ICC / soft proof / rendering intent。
4. 真正的传感器级 highlight reconstruction 与更高级 demosaic。
5. **RGBA16F / Qt RHI GPU Processing Graph**。
6. 更高性能 RAW preview cache / 后台解码。
7. Metadata 索引、筛选与批量检索。

架构说明见 [`docs/RAW_PIPELINE_ALPHA5.md`](docs/RAW_PIPELINE_ALPHA5.md) 与 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。
