# JixelLight Lightroom Classic 风格 UI / UX 设计蓝图

> 2026-10-08 · 与 LIGHTCRAFT_FUSION_MASTER_PLAN.md 配套；仅设计，尚未修改 ui/Main.qml。

## 1. 外观目标

以 Lightroom Classic 的“图库 Library”和“修改照片 Develop”为**交互参照**：深色专业工作区、中央图片主导、两侧可折叠功能面板、底部持久胶片条、全局快捷键与非破坏编辑。

不是像素级复刻 Adobe 产品；使用 JixelLight 自己的标识、图标、文案、主题令牌与代码。LightCraft 的“编辑面板组织/分层”和照片管理 UI 是第二参考来源。

必须保持 JixelLight 的特色工具显眼、完整、可达：Sony Creative Look、参考 JPEG 对照、1024-bin Scopes、GPU 模式/精确统计、报告问题（Bug ZIP）。

## 2. 窗口框架（目标结构）

    +--------------------------------------------------------------------+
    | JixelLight     Library  Develop  Compare     Import  Export       |
    +--------------+-----------------------------------+-----------------+
    | LEFT PANEL   |            PHOTO WORKSPACE        | RIGHT PANEL     |
    | Navigator    |                                   | Histogram [pin] |
    | Presets      |       Fit / 1:1 / 2:1 / Pan       | clipping + info |
    | History      |                                   | Tool Strip      |
    | Folders      |     Non-destructive preview       | [Basic]         |
    | Collections  |                                   | [Tone Curve]    |
    |              |                                   | [Color Mixer]   |
    |              |                                   | [Color Grading] |
    |              |                                   | [Sony Look]     |
    |              |                                   | [Detail/Optics] |
    |              +-----------------------------------+-----------------+
    |              | Before/After | reference | zoom  |                 |
    +--------------+-----------------------------------+-----------------+
    | FILMSTRIP  [thumb] [thumb] [thumb] ...       rating / flag / filter|
    +--------------------------------------------------------------------+
    | Ready  RAW/ProPhoto  GPU: Metal  Scopes: Display  [Bug ZIP]         |
    +--------------------------------------------------------------------+

**Library** 模式：中央区显示虚拟化 Photo Grid，左侧 Folders / Collections，右侧 metadata/filter/quick develop；Filmstrip 仍在底部。

**Develop** 模式：中央图片区最大，右侧调色模块，左侧 Navigator/Presets/History；上下切换照片不跳页。**Compare** 作为 Develop 中的视图模式（Before/After、参考照片、Sony Camera JPEG），不强行制造独立复杂页面。

## 3. 适配尺寸与布局

- 默认 1540×920；最低 1180×720 是现有 Main.qml 设置，不能因新布局造成严重裁切；
- 左侧建议初始 220–260px，右侧初始 320–380px，底部 Filmstrip 初始 110–150px；均可拖拽调整并可自动折叠；
- 进入 1180px 窗口时优先折叠左侧、收起次要工具，保持中央可编辑视区；
- 双屏和高 DPI 通过 Qt devicePixelRatio，精确 100% 像素视图不能因缩放出现误判；
- 布局、左右面板、最近工作区、语言、最近工具存到 QSettings；老用户未保存过布局时合理默认。

## 4. 右侧面板规范

右侧不是把所有控制平铺在一个巨大的 ScrollView 中。分为：

1. **始终显示的顶部 Scopes 区域**：可切换 RGB/Luma，保留 1024 bins 和 clipping；右键或子面板展开未来的 Waveform / RGB Parade / Vectorscope；切换至 Source/Working/Display 时明确显示统计范围。
2. **常驻工具列**：裁剪、修复、蒙版、局部刷、几何等。没有实现的工具不可假装能用。
3. **可滚动手风琴调整区**，建议顺序：Camera/Profile（含 Sony）→ Light → Color → Tone Curve → Color Mixer → Color Grading → Detail → Optics → Transform → Effects → Calibration。
4. **Sony Creative Look** 保留为独立而非隐藏在 HSL 内的面板：拍摄记录为只读块，当前编辑外观有明确作用状态，参考 JPEG 打开中央的左右比较视图；现有参考直方图保留在比较区域。

面板标题可折叠、右键复位当前块、显示工具启用/禁用状态；第一次加载默认仅展开 Light、Camera Look。高级功能可由用户设置固定或调整顺序。

## 5. 调整控件规范

- 保留 AdjustmentState 现有曝光、温度、色调、对比度、Highlights/Shadows/Whites/Blacks、高光恢复、HSL、Master/R/G/B 曲线和 Sony Look 参数。
- slider 可拖拽、数值可直接键入、双击数值或 slider 复位、Shift 精调，Escape 取消交互（必须与控制器取消语义相匹配）。
- 拖动期间在同一 undo group 内形成一次编辑；松开后调用现有 finishInteraction()，不新增滑动时每像素写 SQLite 的路径。
- 数值单位清晰：曝光 EV，色温 K 仅在可校准时；相对白平衡标明“相对”，避免 ARW 把没有标定的 Kelvin 假装为准确值。
- 组内变更先刷新预览，Scopes 可异步稍后刷新；不得显示旧统计“精确已更新”。
- 低精度 draft 与 full-resolution 的状态可检查，放大后100%与导出必须按真实像素验收。

## 6. 图片工具与快捷键

第一阶段先实现实际已有可运行的命令并统一键位，避免为了接近 Lightroom 增加空按钮：

| 用途 | 建议默认键（需检测与系统/现有键位冲突） | 入口 |
|---|---|---|
| 导入 | Ctrl/Cmd+O（已存在） | 顶部 Import、File 菜单、拖放 |
| Gallery/Grid | G | 模式切换 |
| Develop | D | 模式切换 |
| Fit / 100% / 200% | 现有按钮和 Z 扩展 | 中央画布工具列 |
| Before/After | \ 和 Y（在原图状态被缓存支持后） | 画布工具列 |
| Crop | C（实现后） | 右侧 Tool Strip |
| Masks | M（实现后） | 右侧 Tool Strip |
| Copy/Paste adjustment | 原有显式按钮先保留 | 右侧或 Edit 菜单 |
| Report Bug | 原有全局按钮/菜单 | 右上及底部诊断入口 |
| Undo/Redo | Ctrl/Cmd+Z / +Shift+Z（编辑历史实现后） | 编辑菜单 |
| Library rating / flags | 0–5、P / X / U（实现后） | Grid / Filmstrip |

快捷键不应硬绑在不可用命令上：由后续 CommandRegistry 注册，统一检测冲突，屏蔽输入框时全局误触。

## 7. 底部 Filmstrip 的基础数据模型

现有 PhotoController.library 是 QVariantList，仅在左侧 ListView 显示文件名和类型。第一步可以共享它的选中状态、索引与基本数据，但真正的 Filmstrip 缩略图必须使用后台缩略图 provider + 内容/状态 hash；不得在一个 QML delegate 里触发多个 LibRaw 全分辨率解码。

逐张缩略图：预览、小 RAW badge、选中边框、星级/色标/flag（Catalog 阶段后）。水平虚拟化 ListView、可调整高度、支持 shift/ctrl 多选（Catalog 模型完成后）、右键菜单、上下切图预读。批量同步使用选中照片组而不是当前“全部照片”硬编码模型。

## 8. QML 模块拆分和建议文件

替换现有单个 Main.qml 混合全部功能的写法（340 行）；保留底层 C++ API，先不推倒 PhotoController：

    ui/
      Main.qml                     <- 应用根和生命周期
      shell/
        WorkspaceShell.qml         <- 水平/垂直 SplitView
        AppHeader.qml              <- 模块导航、导入、导出、诊断
        StatusBar.qml              <- GPU/Color/Scopes/进度
        Filmstrip.qml              <- 始终可达的缩略图队列
        LeftSidebar.qml            <- Navigator/Presets/History/Folders
        RightSidebar.qml           <- Pin Histogram + ToolStrip + collapsible sections
      workspace/
        DevelopWorkspace.qml       <- PhotoCanvas / BeforeAfter / Sony reference
        LibraryWorkspace.qml       <- Virtualized Photo Grid
      panels/
        BasicPanel.qml
        ColorPanel.qml
        ToneCurvePanel.qml         <- 可复用现有 CurveEditor
        ColorMixerPanel.qml
        SonyLookPanel.qml          <- 包装旧逻辑、保留原始控件
        ScopesPanel.qml            <- 可复用现有 HistogramView
        ExifPanel.qml
        AdjustSection.qml          <- 折叠与统一 reset 的基础容器
      dialogs/
        ExportDialog.qml
        ProjectDialog.qml
      style/
        Theme.qml                  <- 颜色、间距、字体、控件高度等令牌

迁移顺序：先 Shell 只显示已实现内容 -> 将 Main.qml 内容机械抽取组件 -> 保存布局 -> 添加可用 Filmstrip -> 加 Library Grid -> 调整排版；每次运行 smoke UI、GPU/CPU 两条路径和 QML warnings 检查。不能先删掉全部控件，再分期“补回来”。

注意：这是目录与职责建议，不代表这些文件已存在或已经编译。

## 9. 测试清单（UI 重构不可遗漏）

- Windows、macOS、Linux 三平台 QML 启动、软件渲染和 GPU rendering；
- 启动未导入图片时正常显示；拖入单张、几百张、RAW/JPEG 混合也不阻塞 UI；
- 当前照片选择/切图/视区缩放/拖动不发布旧任务预览；
- 拖动 Basic/HSL/Curve 和 Sony 参数仍可实时更新 GPU/CPU，精确统计状态正确；
- Sony 参考图左右比较、独立参考 Histogram、拟合本照片、保存/导入 .jlook.json 仍可操作；
- 旧项目打开、复制/粘贴/同步、批量导出/取消、关闭前 flush、出错提示；
- 显示 ICC 与 GPU/CPU 预览颜色一致；直方图不受 monitor ICC 影响；
- 中英语言即时切换，包括按钮、对话框、错误提示；
- 首次运行和 1180px 最小窗口的屏幕截图对照、键盘焦点、Tab 顺序、面板尺寸持久化；
- 性能基线记录：界面启动、首次载入、预览/缓存命中、点击切图、拖滑块、Scoping。

## 10. 图标、素材与许可

只采用 JixelLight 原创/许可兼容图标与文字。以 Adobe 官方文档学习模块分布和工作流，不复制其商标或私有资产。LightCraft 相关 Rust/egui 源码如有实际移植，须单独记录 MIT/Apache-2.0 和 NOTICE；跨语言移植先做行为及精度测试。

## 11. 本蓝图的实施边界

当前文件是“目标和验收规范”，不是已经完成的 UI。第一份生产代码 PR 只应完成可靠的 Shell 和现有功能迁移；所有新增专业功能分别提供真正的 C++/QRhi 实现、测试和回归报告后再启用 UI。
