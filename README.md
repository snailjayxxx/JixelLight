# JixelLight

JixelLight 是面向 Windows / macOS 的专业摄影后期桌面软件，核心工作流以 **RAW 照片、批量后期、非破坏编辑** 为中心，而不是以 JPEG 小图编辑为主。

当前开发版本：**v0.1.0-alpha.3**

## alpha.3 交互修复

- 修复 alpha.2 中“文件选择窗口可以打开，但选完照片后没有任何动作”的问题。
- 根因是 QML `FileDialog.selectedFiles` 直接传递给 C++ `QVariantList` 的运行时类型桥接不可靠。
- 导入窗口现改为 C++ / Qt Widgets 原生 `QFileDialog`，选中的 `QStringList` 直接进入 `PhotoController`，不再经过 QML 列表类型转换。
- 增加单文件 `PhotoController::importFile(QUrl)` 路径，拖放导入也逐文件走同一条明确的 C++ 调用链。
- 增加 `Ctrl/Cmd + O` 导入快捷键和窗口拖放 RAW / 照片导入。
- `🐞 报告当前问题` 不再只修改底部状态栏：成功会弹窗显示 Diagnostic ZIP 路径，失败会直接弹错误窗口。
- CI 新增完整控制器导入回归测试：真实 Leica M8 DNG → `PhotoController.importFile()` → 图片库 → LibRaw 解码 → 16-bit 预览。Windows x64 / macOS ARM64 均通过。

## 当前版本重点

### RAW 优先

已接入 **LibRaw**，RAW 不再交给 Qt 普通图片读取器处理。当前支持导入/解码常见相机 RAW 扩展名，包括：

- Sony：ARW
- Canon：CR2 / CR3 / CRW
- Nikon：NEF / NRW
- Fujifilm：RAF
- Panasonic：RW2
- OM System / Olympus：ORF
- Adobe：DNG
- Pentax：PEF
- Samsung：SRW
- Leica：RWL
- 以及 3FR / ERF / KDC / MOS / MRW / X3F / IIQ / RAW 等

RAW 参考解码使用相机白平衡、16-bit 输出，并进入 JixelLight 的统一非破坏处理管线。

> 当前 alpha 使用 LibRaw/dcraw reference processor 完成 RAW → 16-bit RGB 的第一条完整链路。后续仍会继续实现相机空间、Camera Profile、LittleCMS/ICC 和 RGBA16F GPU Processing Graph；因此这不是最终画质管线。

### 16-bit 处理与 Professional Scopes

- CPU reference pipeline 使用 `QImage::Format_RGBA64`，即 16 bits/channel。
- 曝光、色温、色调、对比度、高光、阴影、白色色阶、黑色色阶均在 16-bit buffer 上计算。
- RGB / Luminance Histogram 使用 **1024 bins**，直接从 16-bit 当前调整结果统计。
- 保留阴影 / 高光裁切检测。

### 中文 / English

- **首次启动默认中文。**
- 顶部工具栏可随时切换 `中文 / English`。
- 语言选择使用 `QSettings` 保存在本机，下次启动继续使用上次选择。

## 已可使用的工作流

1. 新建 JixelLight 项目。
2. 导入 RAW / JPEG / PNG / TIFF 等照片。
3. 选择 RAW 后由 LibRaw 解码为 16-bit 工作图像。
4. 查看实时 RGB / Luminance Histogram。
5. 调整曝光 / 白平衡 / 对比度 / 高光阴影 / 黑白场。
6. Histogram 与当前调整结果实时同步。
7. 复制 / 粘贴参数，或将当前参数同步到全部照片。
8. 从全分辨率源图导出 JPEG。
9. 遇到问题时点击 `🐞 报告当前问题` 生成 Diagnostic ZIP，并弹窗显示保存路径。

## Diagnostics

从第一版起保留：

- Session Logging
- 最近 1000 条 Action Trace
- RAW 解码成功 / 失败记录
- 当前文件、RAW 类型及解码信息
- Crash marker
- 一键 Bug Snapshot / Diagnostic ZIP

## 构建依赖

- CMake 3.24+
- C++20
- Qt 6.5+（Core / Gui / Widgets / Quick / Qml / QuickControls2 / Sql）
- LibRaw（通过 vcpkg manifest 管理）

仓库包含 `vcpkg.json`。架构说明见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## 下一阶段

RAW 是 JixelLight 的主线。后续优先级继续围绕 RAW 画质：Camera-space White Balance、Camera Profile / Color Matrix、LittleCMS / ICC、RGBA16F / Qt RHI GPU processing graph、Tone Curve / RGB Curve / HSL、RAW preview cache 与 Exiv2。
