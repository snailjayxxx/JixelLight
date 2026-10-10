> alpha.9 更新：已增加三组真实 Sony ST ARW/JPEG 的兼容性验证和独立场景经验拟合；详见 [真文件验证与限制](docs/SONY_REAL_VALIDATION.md)。下文 alpha.8 的“尚无真实 Sony 样本”记录是历史状态，不代表当前测试覆盖。十二种内置近似外观仍不等于 Sony 官方标定。

# JixelLight

JixelLight 是面向 Windows / macOS 的专业摄影后期桌面软件，核心工作流以 **RAW 照片、批量后期、非破坏编辑** 为中心。

当前开发版本：**v0.1.0-alpha.10**

## alpha.10：RAW 基础显影修正

alpha.9 仍把 LibRaw 的无自动提亮、线性 ProPhoto 输出过于直接地送入显示链。实拍 RAW 与同次机内 JPEG 对照暴露出默认画面明显偏暗：这不是 Bayer 解码失败，而是缺少稳定的 scene-linear → display-referred 基础显影。

alpha.10 保持 LibRaw `no_auto_bright=1` 和线性输出，不启用逐照片自动亮度；新增 **Jixel Neutral v1** 基础显影。在 RAW 专用路径中，用户曝光/白平衡/明暗调整之后、显示色彩变换之前加入固定 **+2.5 EV 场景基准放置**，再沿用软高光肩部压缩。JPEG/TIFF 等已显示编码输入不应用这一步，所以用户的“曝光 0.00”仍表示相对于稳定 RAW 基准的 0 EV，而不是把传感器线性数值直接显示。

处理引擎版本升级为 `jixellight-linear-v4-base1-look4`，旧缓存自动失效。基于旧 RAW 基础显影拟合出的图片专用/多场景 `.jlook.json` 会被拒绝并要求重新拟合；普通用户导入的 `.cube` 不受此限制。Sony ST 实验配置会在新引擎的真实 ARW/JPEG CI 中重新生成，避免继续吸收 alpha.9 的基础显影偏差。

为保证跨平台颜色正确性，Metal 继续使用完整 GPU 感知颜色路径；D3D11 / OpenGL Compute 在两类已验证为数值敏感的情况——复杂 Hue/HSL 混合，以及 LinearProPhoto 输入的用户曝光与正向暗角的合计增益达到 +2.5 EV 以上，同时调整 Saturation/Vibrance——会自动切换到 CPU reference 颜色计算，再将结果上传 GPU 继续显示与 1024-bin 直方图。这个安全回退不会放宽 CPU/GPU 40/65535 验收门槛，并会写入 performance 诊断。普通曝光、基础颜色、Sony Look/LUT、显示和直方图仍保持 GPU 路径。

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
- 裁切后椭圆暗角：强度 −3～+3 EV、中点和羽化，支持 CPU reference / QRhi 调色及独立重置。
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

### 按参数组粘贴与同步（融合开发分支）

“复制”保存当前版本的调整快照；“粘贴…”选择要替换的参数组。“同步…”可对已选版本或图库全部版本应用当前来源的曝光、白平衡偏移、明暗、整体颜色、HSL、曲线、Sony 外观、效果（暗角）或几何参数。默认不勾选裁切/透视/镜头/方向；来源版本不会被同步修改，每个目标保留独立撤销。评分、关键词和相册不参与同步。

### 安全复制导入（融合开发分支）

导入菜单提供“添加现有照片”和“复制到文件夹并导入”。选择文件后可在名称预览中保留原名，或使用 `{name}`、`{seq}`、`{seq:4}` 模板及起始序号；扩展名自动保留，序号按选择顺序递增。`{capture_date}` / `{capture_time}` 使用相机记录的 `yyyyMMdd` / `HHmmss`，不猜测时区或使用当前日期；后台读取拍摄元数据，缺失时禁用开始并拒绝整批，改模板或序号可复用最多 1000 条记录的缓存。复制前重新检查预览日期，校验后的副本日期须与名称一致。复制在后台执行，先检查整批来源和目标，再逐文件校验 SHA-256、以不覆盖方式发布；取消清理当前临时副本，已完成副本保留并加入当前项目。非法模板、路径字符、保留名称和重名整批拒绝。原文件只读；尚不支持 Move。

### 非破坏几何（融合开发分支）

- 画布裁切、90° 旋转/翻转及 ±45° 拉直；拉直使用线性 16-bit CPU 插值，自动收边、不放大，随后可在校正画面上裁切。
- 可折叠的双语手动透视与镜头面板：横向/纵向透视各 ±40%、径向畸变 ±30%、红/蓝相对绿通道倍率各 ±2%。这些是手动模型参数，不是角度或自动镜头配置文件。校正与拉直共用一次 CPU 重采样；保守收边覆盖所有通道和曲线边缘，输出尺寸不大于来源。
- 预览、全分辨率统计及 JPEG/PNG/TIFF/WebP 导出共用几何处理，保留 Undo/Redo、项目历史和 XMP；连续拉直拖动合并为一条历史。
- 所有校正为零保留旧像素路径：无拉直为 schema 1，仅拉直为 schema 2；启用透视/镜头时使用完整 schema 3。旧项目不自动应用校正；未知、缺字段或越界几何拒绝加载。CLI 支持 `{"command":"geometry.straighten","degrees":5.5}` 和 `{"command":"geometry.set","parameter":"distortion","value":0.1}`；`geometry.resetCorrections` 仅重置五项新参数，保留拉直、裁切与方向。自动镜头配置、切向畸变和自动垂直线识别仍未实现。

### 暗角（融合开发分支）

暗角以裁切和方向确认后的整幅照片为坐标，放大与平移时保留同一位置。负值压暗边缘，正值提亮；椭圆权重通过中点和羽化控制，在线性工作空间、白平衡/曝光之后及高光恢复/明暗映射之前执行，Alpha 保持不变。它是手动效果，不使用镜头配置。三项参数支持项目历史、虚拟副本、预设、XMP、选择性“效果”同步及 CLI `develop.set`；`vignette.reset` 仅重置暗角。默认零强度保留原有输出，完整默认状态不向旧快照添加字段；启用后保存 schema 1 的可选 `vignette` 对象，未知版本、缺字段及越界快照拒绝加载。裁切编辑期间使用当前未裁切照片，应用裁切后重新定位暗角。

GPU 输入和数值回退使用逐通道 RGBA64 / FP32 转换，保留半透明及透明像素中的未预乘 RGB，避免通用格式转换先在整数空间预乘造成的精度损失；CPU reference、GPU 读回和实拍矩阵使用同一转换语义。RAW 源仍为 RGBA64。

### 批量导出名称（融合开发分支）

批量导出设置显示全部版本数量及前三个名称，支持 `{name}` 原名、`{version}` 版本名、`{seq}` 或 `{seq:1}`～`{seq:9}` 补零序号、`{capture_date}` 拍摄日期和 `{capture_time}` 拍摄时间。扩展名由输出格式确定，原版的版本名为 `Original`；日期/时间来自相机记录的 `yyyyMMdd` / `HHmmss`，不推测时区或使用当前日期。模板最多 160 字符，序号为 1～999999999，最终文件名最多 240 UTF-8 字节。

留空保留既有命名：界面使用原名加 `_JixelLight` 并自动避开重名，CLI 使用既有六位序号命名。自定义模板的重名、非法文件名或所需拍摄时间缺失会在输出前拒绝整批；对比名称时统一 Unicode NFC 并忽略大小写。目标文件夹中的文件、目录及链接均占用名称。导出队列冻结名称与调整，可继续编辑/重命名版本；批量输出使用同文件系统临时目录及不覆盖发布，失败或取消清理当前临时输出。

```sh
JixelLightCli --catalog Project.jlp --output-dir existing-folder --format png --name-template '{capture_date}_{name}_{version}_{seq:4}' --sequence-start 7
```

CLI 的命名参数只用于 `--catalog`。目录读取为只读快照；未索引拍摄时间时，只读读取源元数据，不修改原文件或项目。

先用 `--catalog Project.jlp --list-catalog` 查询保存的原版/副本标识 `catalog_key`、路径、版本名、评分/Flag、标签和日期记录。查询不读取原片元数据或解码，在原片离线时也可查看；空目录返回空列表。

`--catalog-key` 可重复指定列表中的精确标识，只导出所选版本，保持目录保存顺序。未知、重复或空标识整批拒绝；导出仍限制 1–1000 个所选版本，可以从更大的目录选少量版本。`--list-catalog` 不能与输出设置或调整命令混用。

```sh
JixelLightCli --catalog Project.jlp --list-catalog
JixelLightCli --catalog Project.jlp --catalog-key "从列表复制的 catalog_key" --output-dir existing-folder --format png
```

### Professional Scopes

- RGB / Luminance Histogram：1024 bins。
- Histogram 读取**当前最终显示结果**，所以曝光、HSL、饱和度、曲线变化都会实时反映。
- Shadow / Highlight clipping 百分比。
- 可选亮度波形 / RGB Parade / 矢量示波器：异步 CPU 参考计算，支持当前视区预览和全分辨率；波形/Parade 保留 1024 级，矢量图统计编码 sRGB 的 Cb/Cr，均在 monitor ICC 之前。旧结果变暗并标记更新中，默认 GPU 直方图保持不变。
- 三种可选示波器共享一次 128 行分块调色，保留各自原有计数和显示算法；16 MiB LRU 只保存已完成的三张示波器图和必要的 LUT 数据，同一来源/参数/几何下切换模式可直接复用。取消、过期照片和未完成计算不会交付旧结果。
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
- `stage_outputs.json`：冻结文件、图像、参数、几何和统计版本；九个 CPU 色彩阶段（含暗角）的 FP32 RGB 哈希/范围、量化和 Sony 细节边界。交互式报告另外按需读回显示器 ICC 之前的 GPU 最终工作输出，记录 FP32 RGBA 指纹/范围、实际 GPU 或 CPU 上传来源；64 MiB 上限和3秒响应期限，失败仍生成 CPU 报告。同步 `reportBug()` 保留 CPU reference。正常编辑仅读回16,400字节直方图，RAW 输入仍为 RGBA64，内部 GPU 阶段尚未捕获。
- `performance.json`：准备预览缓存、64 MiB CPU 显影预览结果缓存、4 MiB 精确直方图结果缓存、16 MiB 可选示波器缓存的命中/未命中/旁路/淘汰、占用与预算，以及请求耗时和示波器共享渲染次数。Undo/Redo 和版本切换可复用相同来源/参数/几何的已完成结果；显示器 ICC 在结果交付之后应用，导出和阶段诊断独立计算。直方图缓存不保留图像帧；示波器缓存不保留源图或已调色照片。
- 手动几何在 `geometry_corrections` 记录 CPU 请求耗时和五项参数；`stage_dependencies.geometry` 记录完整参数、操作顺序、取样与收边方法，明确没有应用镜头配置文件。校正后的准备预览同时送往 CPU/GPU 调色；几何重采样本身仍是 CPU。

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
- **Qt 6.8.3**（包含 Qt Shader Tools、Qt Image Formats 和 Qt Gui private headers；TIFF / WebP 需要 imageformats 插件；QRhi 属于有限兼容 API，升级需重新验证）
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
