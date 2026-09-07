# Sony 真文件验证与多场景经验配置 — alpha.9

## 本版完成的工作

在 alpha.8 的功能上补上真实 Sony ARW＋同次拍摄 JPEG 测试、机型身份修正、拟合几何修正，以及独立场景检验的可复用经验 LUT。照片不上传至 AI 服务，主程序不联网下载标定数据。

**这些不是 Sony 官方配置，也不等于所有 Sony 相机和十二种外观均已完成标定。** 内置十二种参数型预设仍为未标定近似实现。附带的 ST 配置是可选择导入的实验配置，绝不自动替换默认外观。

## 可复现的真实样本

来源为 Photography Blog 的 Sony A7 IV 原始评测样片：
https://www.photographyblog.com/reviews/sony_a7_iv_review

使用 `sony_a7_iv_01`、`04`、`07` 的 ARW/JPEG，原文件 SHA256、URL、角色和独立 ExifTool 对照值在 `tests/fixtures/sony-real-manifest.json`。相机标准 EXIF Model 是 `MODEL-NAME`，软件字段为 `MODEL-NAME v0.02`，SonyModelID 388 / LibRaw 解码身份为 ILCE-7M4。因此这是**预生产样本**，不是用户实机或零售固件认证。全部三组均记录 ST，八项值为：对比度 0、高光 0、阴影 0、褪色 0、饱和度 0、锐度 4、锐度范围 3、清晰度 1。

本仓库、程序包、验证报告不包含评测者的原始照片。开发者可显式运行下载脚本，按固定 URL 和内容哈希获取测试数据；下载损坏或原文件变化时测试必须失败，不换成合成图。

## 两项实际修正

1. 保留原始 `recordedModel`；只有 EXIF 机型为空/明确占位且 MakerNote 提供已知身份时，使用 SonyModelID 规范化。具体型号与 MakerNote 矛盾时禁止自动应用/配对，不能覆盖矛盾。修正 RAW 完整加载后 LibRaw 规范化机型、而 JPEG 仍是占位名称造成的自动配对失败。
2. LibRaw 解码图为 7028×4688，相机 JPEG 为 7008×4672。根据默认裁切元数据 (12,8,7008,4672) 与解码旋转计算拟合范围，而不是猜测中心裁切。此裁切**只用于拟合颜色时的对齐**，不改变 RAW 完整编辑/导出尺寸，不改写原片；无有效元数据时保持全幅和原有配准拒绝机制。未知旋转、越界、异常缩放都会拒绝该裁切。缓存版本更新为 `jixellight-linear-v3-look2`。

## 单张拟合与多场景拟合的区别

界面“拟合本照片”仍针对当前照片，以空间分块留出样本检验。新增开发/高级工具 `JixelLightSonyValidation --dataset dataset.json 新输出目录`：至少两张训练场景和一张独立验证场景，最多十二组；要求同型号、同外观及完整相同的八项微调。拒绝重复文件哈希和重复拍摄时间/子秒身份、无法确认的配对。它不是防伪鉴定或自动图像配准。

独立验证场景**完全不参与 LUT 优化**，另外训练图也保留空间分块。每个独立场景须满足显示 sRGB、归一化 RGB RMSE ≤ 0.05，并相对未匹配结果改善（接近恒等的情况允许很小数值偏差）；否则不生成配置。测试还会改变验证图的目标像素，确认输出 LUT 系数完全不变，以防答案泄漏进入训练。

该门槛在首次真实多场景试验前确定，没有为使样本通过而放宽。RMSE 是在至多 256 像素长边、统一参考尺度计算，不是 Delta E、主观画质分数、全分辨率细节或像素完全相同的保证。

## 本地首轮实测（Linux Release / LibRaw 0.21.2）

默认裁切修正后的单照片空间留出误差：01 为 0.35182→0.00765，04 为 0.29413→0.00981。旧的未对齐路径分别为 0.01727、0.01419（后者为拟合后误差）。

01＋04 训练，07 **从未参与优化**：07 的误差 0.21850→0.01995。多场景 LUT 实测颜色节点覆盖约 **4.05%**，大量颜色仍依赖平滑和恒等先验。最终平台精确值应读取各平台 `sony-real-reports/summary.json`，不能将本地结果当作其测量值。

## 如何使用交付的实验配置

在 JixelLight 打开 RAW，选择“导入外观 / LUT”，导入 `Sony-ILCE7M4-ST-experimental.jlook.json`，先保持基础曝光等调整中性以比较。Windows 包的 `profiles` 目录或单独交付文件可找到；Mac app 内为 `Contents/Resources/profiles`。也可以加载自己的同次拍摄 JPEG 并使用“拟合本照片”，避免直接迁移别的场景配置。

配置来自这三个 ST 样本，包含原片哈希、训练/验证身份、机型、微调、拟合区域、引擎版本、颜色覆盖及误差证据。它拟合的是现有 JixelLight 默认开发到相机成片的整体颜色差异，可能包含动态范围处理、曝光映射、白平衡等影响；不是分离、提取或复刻了 ST 的私有算法。换光源、曝光、镜头、机型/固件或外观后不保证成立。

查表在 display-sRGB 域进行；Adobe RGB / ProPhoto 导出不能恢复查表前已映射到 sRGB 的颜色。它不复现降噪、锐化、局部清晰度、畸变或纹理；100% 细节须另行检查。实体 GPU、其他相机和 FL/FL2/FL3 等非 ST 的真实配对效果尚未据此验证。

## 高级工具与开发测试

```text
JixelLightSonyValidation --dataset dataset.json new-output-directory
```

`dataset.json` 使用相对自身位置的本地路径，例如：

```json
{"pairs":[
  {"id":"scene-a","raw":"a.ARW","jpeg":"a.JPG","role":"train"},
  {"id":"scene-b","raw":"b.ARW","jpeg":"b.JPG","role":"train"},
  {"id":"scene-c","raw":"c.ARW","jpeg":"c.JPG","role":"validation"}
]}
```

成功时产生 `empirical.jlook.json` 与 `dataset-report.json`。拒绝覆盖已存在的经验配置，请选择新目录；原照片不修改。此高级工具目前用命令行，不将“开发工具已实现”冒称为已有多场景 GUI 向导。

开发测试命令：

```text
python tools/FetchSonyFixtures.py tests/fixtures/sony-real-manifest.json .sony-fixtures
```

设置环境变量 `JIXELLIGHT_SONY_FIXTURES` 为该目录绝对路径、`JIXELLIGHT_REQUIRE_SONY_REAL=1` 后运行 CTest。CI 必须启用真实文件测试，不能把下载失败/缺失当作通过。GPU 测试还从真实验证流程生成的配置加载 LUT，与 CPU 在四种输出空间、强度和细节选项下比较，继续使用 40/65535 的既有误差门槛。

测试包未正式签名/公证，尚未合并 main；实际 CI 与包完整性结论以相应提交的构建记录为准。
