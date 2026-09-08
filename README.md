> alpha.9 更新：已增加三组真实 Sony ST ARW/JPEG 的兼容性验证和独立场景经验拟合；详见 [真文件验证与限制](docs/SONY_REAL_VALIDATION.md)。下文 alpha.8 的“尚无真实 Sony 样本”记录是历史状态，不代表当前测试覆盖。十二种内置近似外观仍不等于 Sony 官方标定。

# JixelLight

JixelLight 是面向 Windows / macOS 的专业摄影后期桌面软件，核心工作流以 **RAW 照片、批量后期、非破坏编辑** 为中心。

当前开发版本：**v0.1.0-alpha.10**

## alpha.10：RAW 基础显影修正

alpha.9 仍把 LibRaw 的无自动提亮、线性 ProPhoto 输出过于直接地送入显示链。实拍 RAW 与同次机内 JPEG 对照暴露出默认画面明显偏暗：这不是 Bayer 解码失败，而是缺少稳定的 scene-linear → display-referred 基础显影。

alpha.10 保持 LibRaw `no_auto_bright=1` 和线性输出，不启用逐照片自动亮度；新增 **Jixel Neutral v1** 基础显影。在 RAW 专用路径中，用户曝光/白平衡/明暗调整之后、显示色彩变换之前加入固定 **+2.5 EV 场景基准放置**，再沿用软高光肩部压缩。JPEG/TIFF 等已显示编码输入不应用这一步，所以用户的“曝光 0.00”仍表示相对于稳定 RAW 基准的 0 EV，而不是把传感器线性数值直接显示。

处理引擎版本升级为 `jixellight-linear-v4-base1-look4`，旧缓存自动失效。基于旧 RAW 基础显影拟合出的图片专用/多场景 `.jlook.json` 会被拒绝并要求重新拟合；普通用户导入的 `.cube` 不受此限制。Sony ST 实验配置会在新引擎的真实 ARW/JPEG CI 中重新生成，避免继续吸收 alpha.9 的基础显影偏差。

为保证跨平台颜色正确性，Metal 继续使用完整 GPU 感知颜色路径；D3D11 / OpenGL Compute 在两类已验证为数值敏感的情况——复杂 Hue/HSL 混合，以及 RAW 用户曝光达到 +2.5 EV 以上同时调整 Saturation/Vibrance——会自动切换到 CPU reference 颜色计算，再将结果上传 GPU 继续显示与 1024-bin 直方图。这个安全回退不会放宽 CPU/GPU 40/65535 验收门槛，并会写入 performance 诊断。普通曝光、基础颜色、Sony Look/LUT、显示和直方图仍保持 GPU 路径。

这次是有意的画面基准修正，旧项目打开后的 RAW 默认亮度可能发生明显变化；原始 RAW 与编辑参数不会被改写。详见 `docs/RAW_BASE_RENDERING_ALPHA10.md`。


## alpha.8：Sony 外观识别、相机参考与可编辑匹配

新增 Sony MakerNote 外观名称及八项微调读取，保留原始值、缺失和冲突状态；RAW/JPEG 独立处理。新增并排相机 JPEG/RAW 内嵌参考，独立 1024-bin 参考直方图。外观层支持按拍摄设置、12 种独立近似预设、八项微调、强度、关闭、配置导入/保存；支持将同次拍摄参考图拟合成带保留样本误差报告的本照片 17³ LUT，继续 RAW 编辑。

**内置外观未经过索尼原片标定，不是 Sony 官方显影。** 参考拟合仅在本照片上验证；不是通用相机色彩配置，也不能用颜色 LUT 复刻降噪/光学细节算法。拍摄记录和编辑外观分开，旧项目默认不开启新外观层；新 RAW 仅在元数据明确时自动匹配，JPEG/TIFF 不自动再次套用。

操作、字段、正确性与限制见 [Sony 创意外观说明](docs/SONY_CREATIVE_LOOK.md) 和 [Sony 功能验证](docs/SONY_LOOK_VALIDATION.md)。以下 alpha.7/alpha.6 章节保留历史实现记录。

## alpha.7：A—E 性能改造

在保留非破坏编辑、1024-bin RGB/亮度直方图和 Bug 诊断的基础上，加入：

| 模块 | 已实现 |
|---|---|
| A：调度 | 后台读取、预览、统计与导出；每条交互队列至多一个运行任务和一个可替换待办；照片/参数/视区版本校验；300ms 合并保存与 1s 最大等待。 |
| B：CPU | 参数预编译、白平衡/曝光矩阵合并、中性调整旁路、仅执行启用的色带、共享行块线程池；保留 alpha.6 对照实现用于测试。 |
| C：缓存 | 512MiB 默认开发缓存、1GiB 磁盘线性预览、源文件指纹与校验和、相邻照片预读、相机 JPEG 占位、Fit/100%/200% 视区处理。 |
| D：GPU | Qt 6.8.3 QRhi：Metal / D3D11 / OpenGL Compute；RGBA32F 常驻纹理，当前调色步骤融合执行，直接 GPU 显示；能力不足自动回退 CPU。 |
| E：统计 | 1024-bin GPU 分块与归并直方图，每次仅读回 16,400 字节；可选全分辨率 CPU 精确统计，明确显示统计范围和更新状态。 |
| 导出/诊断 | 冻结参数的后台批量队列、128 行分块 JPEG、直接宽色域 ICC 输出、取消不提交半成品、保护已导入原图；批量异步日志与 performance.json。 |

详细实现、测试方法、可配置预算和未覆盖范围见 [性能改造说明](docs/PERFORMANCE_ALPHA7.md)。

**GPU 不是 AI 推理。** RAW 解包/去马赛克仍为 LibRaw CPU；当前 GPU 负责交互调色、显示和预览统计。导出保留经过测试的 CPU 分块路径。显示仍以 sRGB 为目标；显示器 ICC/软打样不在本次范围。

以下 alpha.6/alpha.5 章节是历史设计记录，其导出、同步预览和待办状态不代表 alpha.7。


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
- **Qt 6.8.3**（包含 Qt Shader Tools 和 Qt Gui private headers；QRhi 属于有限兼容 API，升级需重新验证）
- LibRaw（vcpkg manifest）
- LittleCMS 2（vcpkg `lcms`）
- libjpeg-turbo（分块 JPEG 写入）
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

## alpha.7 之后的专业化方向

1. Camera/DCP Profile 管理与按相机模型选择 Profile。
2. 高位深 TIFF 等更多输出格式；alpha.7 已直接分叉 JPEG 宽色域输出。
3. 显示器 ICC / soft proof / rendering intent。
4. 真正的传感器级 highlight reconstruction 与更高级 demosaic。
5. 在 RGBA32F GPU 基线之上评估 FP16 和更多空间处理节点。
6. 更多相机 RAW 样本、低分辨率开发算法、长期缓存与峰值内存调优。
7. Metadata 索引、筛选与批量检索。

架构说明见 [`docs/RAW_PIPELINE_ALPHA5.md`](docs/RAW_PIPELINE_ALPHA5.md) 与 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。
