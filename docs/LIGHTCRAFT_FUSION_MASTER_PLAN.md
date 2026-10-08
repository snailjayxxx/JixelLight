# JixelLight × LightCraft 全面融合总计划

> 2026-10-08 | 设计方案草案（不改变生产代码）  
> JixelLight main 审查基线：4efe6aa2ec81eb83053dea239be5d7fa3435d500（0.1.0-alpha.11）  
> 参考：storytold/lightcraft (v0.2.1) 与 Adobe Lightroom Classic 的公开工作流描述  
> 当前 alpha.12 A7R VI / FL3 工作：PR #15，仍未合并，本计划不得覆盖或绕过其验收。

## 0. 产品定位与不可妥协的原则

目标是打造 JixelLight 自有品牌的专业 RAW 非破坏性后期与摄影素材管理软件：
- 操作布局以 Lightroom Classic 的 Library / Develop / Filmstrip / 调色面板为主要参照，UI 仍由 Qt6/QML 编写；
- 技术设计优先参考 LightCraft，学习可验证的算法、分层架构、缓存、编辑工具和命令化设计；
- JixelLight 已有的 Sony Creative Look、相机 JPEG 参考、1024-bin GPU Scopes、诊断与色彩管理保留并继续演进；
- 默认完整离线；原始文件只读；不偷偷下载模型、上传照片、改变现有 Sony 拟合证据和未合并分支；
- 不直接拿 LightCraft 的 Rust/egui/wgpu 替换 JixelLight 的 C++20/Qt6/QML/QRhi/LibRaw/LCMS2/Exiv2/SQLite；
- 与 Adobe 的 UI 相似的是信息架构和交互习惯；不得复制 Adobe 图标、商标、图片或受限制的资源。

## 1. 现有代码盘点（2026-10-08）

| 领域 | JixelLight 已实现（源码位置） | 处理策略 |
|---|---|---|
| RAW 解码与范围 | core/raw/RawDecoder.*；LibRaw、多厂商格式、机内 JPEG | 保留，增强机型测试与传感器事实；不换成 LightCraft 自写 RAW 解码器 |
| RAW 基础曝光 | core/pipeline/ProcessingPlan.h；Neutral v2，baseExposure=0，RAW identity 显式 | 冻结为初始回归基准，禁止重引入 alpha.10 +2.5EV |
| CPU 显影 | core/pipeline/ImagePipeline.*；core/pipeline/AdjustmentState.h | 保留作为高精度 reference，逐步拆成有稳定语义的 stages |
| GPU 调色 | core/gpu/GpuEngine.*；shaders/pipeline.comp；Qt QRhi | 保留直接 GPU 纹理显示及 CPU 数值回退；新增空间处理 stages |
| 1024-bin Scopes | core/scopes/ScopesEngine.*；shaders/histogram*.comp；ui/HistogramView.qml | 保留精度和 GPU 归并；增加 waveform/parade/vectorscope |
| Sony 独有功能 | core/look/*；app/LookController.cpp；ui/SonyLookPanel.qml 与 CameraReferenceView.qml | 独立模块保护；原始 MakerNote 和当前编辑参数不得合并混淆 |
| 显示 ICC / 导出 | core/color/*；app/DisplayColorController.cpp；core/export/* | 保留 monitor ICC display-only 与直接宽色域导出；新增深度/格式 |
| 缓存、异步、取消 | core/cache/SourceCache.*；core/preview/PreviewTasks.*；core/async/LatestJob.h | 保留版本校验，按依赖拆缓存，增加 LRU/邻图预读可观测性 |
| 项目数据库 | core/project/ProjectDatabase.*；SQLite WAL | 保留项目结构及旧项目读取；逐表迁移 catalog 与 history |
| 诊断 | diagnostics/*；tests/*；tools/* | 保留 Bug ZIP/Trace/performance/精度测试，作为所有融合 PR 的准入条件 |
| UI | ui/Main.qml、PhotoCanvas.qml、CurveEditor.qml 等 | 拆解为可测试模块，逐渐转成 Lightroom 式框架 |

注意：README 顶部仍有 alpha.10 历史介绍，实际 main 的 CMake 与 ProcessingPlan 是 alpha.11；修改实现和缓存格式时以最新代码、测试和相应提交为准，不凭旧文字复原废弃逻辑。

## 2. LightCraft 对应模块的迁移映射

| LightCraft 参考 | JixelLight 承接位置 | 迁移方式 | 主要注意事项 |
|---|---|---|---|
| crates/pipeline/：scene-linear stages、Plan、StageCache | core/pipeline/ 新增 StageGraph/StageCache | C++ 重构/独立再实现 | 必须建立逐节点 golden outputs；既有用户调整语义不能悄悄漂移 |
| crates/gpu/：WGSL compute、CPU oracle、GPU fallback | core/gpu/ + shaders/ | 学习机制，继续 QRhi compute | 不复制 wgpu，保留原 40/65535 跨平台回归门槛 |
| crates/preview/：RAM/磁盘 LRU、优先队列、缓存代际 | core/cache/ + core/preview/ | 可增量实施 | 保护 source/param/viewport revision；缓存键带算法版本 |
| crates/raw/：normalize、demosaic、highlight reconstruction | core/raw/ + core/pipeline/ | 算法性参考，不替换 LibRaw | 黑/白电平、裁切、相机矩阵不同于 display tone |
| crates/engine/camera_preview + camera_profiles | core/look/LookCalibration.* + core/raw/ | 与 JixelLight Sony 拟合框架比对后合并长处 | 保留训练/留出/跨场景指标；原始拍摄设置与风格 LUT 分开 |
| crates/catalog/：album、rating、flags、history、search、journal | core/project/ + 新增 core/library/ | SQLite schema migration + 可回放 edit op | 数据迁移无损、跨版本拒绝危险写入、无静默丢失 |
| crates/develop/：settings、presets、controls | core/pipeline/AdjustmentState.h + 新增 core/commands/ | 统一参数注册与可选预设组 | 兼容旧项目 JSON，参数范围和 LUT 解释保留 |
| crates/ui-egui/：panels/grid/develop/history | ui/ 内 QML 组件 | 重新设计 QML，不直接移植 Rust UI | Lightroom Classic 交互为首要参考，品牌用 JixelLight 自有 |
| crates/mcp/ + CLI | 新增 core/commands/ 和 apps/jixellight-cli | 后期可选扩展 | 命令状态一致，危险的文件写操作要有权限与撤销机制 |

**严格不引入**：LightCraft 未验证的默认相机颜色配置、其 256-bin CPU histogram 替换现有 1024-bin GPU histogram、其 GPU output readback 取代现有 texture 直显、未经用户启用的 AI 模型，以及会覆盖 JixelLight Sony Profile 证据的经验参数。

## 3. 图像引擎未来模块边界

目标 RAW 流程：

1. Source / RawDecode：LibRaw 解包、元数据（包括 bit depth、black/white、CFA、active/default crop、As Shot WB）、像素来源身份；
2. CameraLinear：曝光零点的相机元数据、白平衡基准、相机色彩校准；输出仍是带超出 1.0 高光的 scene-linear float；
3. OpticalGeometry：镜头修正、几何、旋转、裁切，仅在源/几何/尺寸变化时重做；
4. SceneSpatial：降噪、纹理、清晰度、去雾、局部明暗及所需的分离辅助 planes；
5. SceneColor：用户 EV/WB delta、基础显影 tone、Creative Look（分阶段）、HSL/OkLab、局部色彩与整体 Color Grading；
6. Output：针对导出目标独立的 gamut/transfer/bit depth/ICC；monitor ICC 只在显示终端执行；
7. Scopes：支持 Source / Working / Display 统计入口，默认显示最终显示编码结果，但不把 monitor ICC 纳入统计；
8. Diagnostics：stage hash、source facts、fallback reason、参数/序列、性能采样、可重放命令。

过渡期间不得把 16-bit QImage 当成 32-bit float 的等价表示。升级完整 float 工作源是单独的“颜色/高光容量”里程碑，需要检查 LibRaw 16-bit 输出造成的既有量化上限。任何改变色彩运算顺序的 PR 必须附画质变化报告、缓存版本变更和旧项目兼容性说明。

CPU reference 与 GPU 共用参数解释和常量，但可以使用不同语言/着色器实现。缓存失效依赖 RAW source identity、decoder config、camera profile、engine version、stage settings、输出尺寸、mask/geometry revision。曝光/HSL 调整不得无故重复 RAW 解码和空间滤波。

## 4. 里程碑与 PR 顺序

每个阶段独立分支、独立 PR、单独可回退。下列顺序同时考虑基础正确性和用户能立即看到的效果。

### F0：冻结基准和避免改坏已有能力（第一批）
- main 目前 alpha.11 构建工作流已成功；取 main 当前 commit 作为基线；
- 列出所有现有参数、Sony 状态、缓存格式、GPU 路径、数据库/导出格式；
- 建立 Lightroom 型 UI 现状 screenshot / action trace / golden fixtures；
- 对已有性能、色彩、Sony 实拍、项目打开/关闭/取消和 CPU/GPU 回退加回归断言；
- 单独跟踪 PR #15；它仍为 draft 时不得替换它的 A7R VI/FL3 修改，也不直接合并未经验证的代码。

### F1：Lightroom 风格 Shell 与主导航（先让操作体验成形）
- 分出 Library / Develop 两个页面；顶部导航、左侧导航/预设/历史、中央 Canvas、右侧常驻 Histogram + 工具抽屉、底部 Filmstrip；
- 面板可调整宽度、折叠、记住布局；有适用于 1180 px 起步窗口的紧凑模式；
- 图片缩略图使用异步 model/provider，不在 QML 主线程解 RAW；
- Sony Look 独立放入 Camera / Creative Look 折叠面板，任何已存在功能不能失踪；
- 原有 bug 上报、GPU 状态、精确 Scopes 入口应清晰可达；提供与老版操作对照。

### F2：照片库 / Catalog / 历史与预设
- 保持现有 SQLite 项目读写，新增照片索引、相册、虚拟副本、评分、Flag、颜色标签、Keywords、可组合筛选；
- 增加 capture/import/edit date 排序、批量选择、可编辑预设、命名版本与撤销/重做；
- 设计迁移脚本：旧 Project.db 只可备份迁移，不能原地破坏，失败回滚；
- 逐步补齐 XMP sidecars，不宣称与 Adobe 所有调整等价。

### F3：显影 Pipeline / 缓存 / 性能
- 先做 stage dependency graph 和缓存观测，再改共享参数与可变缓存；
- 结合 JixelLight 既有 512MiB 开发缓存、1GiB 线性预览、相邻预读优化 source/proxy/stage 缓存；
- Preview 交互 draft/full 分层，停止拖动高质量替换；GPU 保留当前纹理直显；
- 对 float 源、高光恢复、Color Pipeline 做隔离升级，禁止隐式 RAW base gain；
- 每个 stage 输出有 hashes / timings 可写入 Diagnostic ZIP。

### F4：LightCraft 核心非 AI 编辑能力补齐
- Crop / straighten / rotate / transform / lens CA 与畸变，局部 masks：brush / linear / radial / color/luma；
- Texture / Clarity / Dehaze、真正的噪声抑制与锐化、Vignette、Grain；
- Color Grading 三色轮、Point Color、Black & White Mix、更多曲线控制和可用预设；
- 先 CPU oracle 后逐阶段 QRhi compute；空间操作采用 halo/边界测试，输出分辨率与预览视觉一致；
- 某功能暂时只有 CPU 版必须在 UI/诊断标注，不能宣称已全 GPU。

### F5：批量处理、色彩与专业输出
- 导入 Copy/Add/Move、重复文件判断与安全拷贝、文件名模板、批量同步局部组；
- TIFF 16-bit / PNG / WebP 等高位深路径和可选 soft proof；输出 ICC 在宽色域 working image 上直接执行；
- RAW 格式交叉检查 CR3/ARW/NEF/RAF/DNG，通过更多 Sony A7 IV/A7R VI RAW/JPEG 实拍验证；
- Histogram 不降级，同时增 RGB parade、waveform、vectorscope 与 1:1 前后对照；
- 导出不覆盖原始文件，取消不保留半文件，颜色/版本回归可测。

### F6：统一命令、CLI、自动化与可选扩展
- 将 UI actions、快捷键、诊断重放和 CLI 映射至一个 CommandRegistry；
- 增 batch/headless 命令，photo.import / develop.set / mask.add / export.run 等参数 schema；
- 在相同命令路径上实现 undo/redo、行为记录、撤销粒度和验收；
- MCP 接口可在后续作为“可选功能”，**不把 AI 模型、联网和远程控制作为修图必备条件**。

## 5. 不可降级的验收条件（逐 PR 执行）

| 分类 | 必须满足 |
|---|---|
| 核心构建 | Windows x64、macOS arm64、Linux GPU/CPU CI 均通过对应任务；不能以某单平台成功代替全通过 |
| RAW 基础 | RAW/JPEG identity、DNG baseline、As Shot WB、黑白电平/几何、+0EV 用户曝光行为不回退；不得复活 universal +2.5EV |
| GPU | 现有跨后端 CPU/GPU 最大容许偏差 40/65535 不放宽；提供致命 GPU 错误回退和诊断 |
| Scopes | 1024 bins + RGB/Luma + clipping 保留；采样视区、版本/范围必须准确，不显示旧数据为当前精确结果 |
| Sony | 12 个外观入口、8 项 MakerNote 微调、记录与编辑分离、参考对比/实拍拟合及证据保存功能保留 |
| Color | monitor ICC 只能影响屏幕 presentation；不能污染 RAW 拟合、统计、cache、导出或 CPU/GPU 对比 |
| 项目 | 旧 Project.db 能无损打开，必要时备份升级；旧 look profile 对 engineVersion 校验不能被取消 |
| 非破坏性 | 不写坏原片、批量/导出失败回滚、关闭时持久化失败有提示 |
| 诊断 | Bug ZIP 包含 stage/adapter/fallback/trace；新增核心功能必须可重现 |
| UI | 无功能孤岛；原有所有菜单/对话、键盘、项目与 Sony 操作均仍可访问；所有新文案至少中文/英文 |
| 性能 | 分别测 decode/cold loupe/warm slider/切图/CPU/GPU/24MP 全尺寸导出；公布设备、数据源和测试条件，不移植 LightCraft 的数值冒称 JixelLight 实测 |

## 6. 源码许可与隔离规则

LightCraft 根项目标注 MIT OR Apache-2.0。移植算法或文件前必须查该文件的版权、NOTICE、依赖和素材许可，保留适用版权声明/许可/归属，记录取材版本与文件 SHA。不要从 Adobe 私有资产、受限制模型权重或许可证不允许的项目复制内容。ArtCraft 的自定义限制性许可证不等同于 LightCraft 的开源许可证。

优先独立实现思路；确需直接移植 LightCraft 代码时，应建立 THIRD_PARTY_NOTICES、来源追踪和测试，避免跨语言语义漂移。LightCraft 的 SAM 3 模型权重独立授权、不在仓库中；JixelLight 当前以“无需 AI”优先。

## 7. 开发约定与完成定义

- 不在 main 上开展高风险结构改造；所有阶段 PR 对比基线并可独立回退；
- 修改 RAW / display 时必须附“前后对比、相机参照、不同曝光/ISO、高光、人像肤色、裁切、16-bit vs float、CPU/GPU、导出”检查；
- 禁止把未实现 UI 按钮展示为可用功能；可以标“计划中”，但核心流程须可操作；
- 只完成文档不代表融合已实现；各阶段必须分别报告已改的文件、构建/回归状态、未完成项目及下一步；
- 本设计分支只冻结方案，不改变 RAW、参数、Project.db、GPU shader 或 UI 的行为。
