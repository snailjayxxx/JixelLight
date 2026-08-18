# JixelLight

JixelLight 是面向 Windows / macOS 的专业摄影后期桌面软件，核心工作流以 **RAW 照片、批量后期、非破坏编辑** 为中心，而不是以 JPEG 小图编辑为主。

当前开发版本：**v0.1.0-alpha.2**

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

- CPU reference pipeline 改为 `QImage::Format_RGBA64`，即 16 bits/channel。
- 曝光、色温、色调、对比度、高光、阴影、白色色阶、黑色色阶均在 16-bit buffer 上计算。
- RGB / Luminance Histogram 使用 **1024 bins**，直接从 16-bit 当前调整结果统计。
- 保留阴影 / 高光裁切检测。

### 中文 / English

- **首次启动默认中文。**
- 顶部工具栏可随时切换 `中文 / English`。
- 语言选择使用 `QSettings` 保存在本机，下次启动继续使用上次选择。
- 工具栏、项目创建、导入/导出、图片库、Scopes、基础调整、状态提示均已接入双语界面。

## 已可使用的工作流

1. 新建 JixelLight 项目。
2. 导入 RAW / JPEG / PNG / TIFF 等照片。
3. 选择 RAW 后由 LibRaw 解码为 16-bit 工作图像。
4. 查看实时 RGB / Luminance Histogram。
5. 调整曝光 / 白平衡 / 对比度 / 高光阴影 / 黑白场。
6. Histogram 与当前调整结果实时同步。
7. 复制 / 粘贴参数，或将当前参数同步到全部照片。
8. 从全分辨率源图导出 JPEG。
9. 遇到问题时点击 `🐞 报告当前问题` 生成 Diagnostic ZIP。

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
- Qt 6.5+
- LibRaw（通过 vcpkg manifest 管理）

仓库包含 `vcpkg.json`。推荐使用 vcpkg toolchain：

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

## 下一阶段

RAW 是 JixelLight 的主线。接下来的优先级将继续围绕 RAW 画质，而不是先扩普通图片功能：

1. LibRaw RAW decode / metadata 完整化。
2. Camera-space White Balance。
3. Camera Profile / Color Matrix。
4. LittleCMS / ICC display transform。
5. RGBA16F / Qt RHI GPU processing graph。
6. Tone Curve / RGB Curve / HSL。
7. 更高性能 RAW preview cache 与后台解码。
8. Exiv2 完整 EXIF 浏览与筛选。

架构说明见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。
