# RAW 基础显影 — alpha.10

## 为什么修改

JixelLight 的 LibRaw 解码一直故意保持 scene-linear：

- `no_auto_bright = 1`
- `bright = 1`
- `gamm = 1,1`
- 相机白平衡与相机矩阵开启
- 输出 16-bit Linear ProPhoto RGB

这适合作为编辑工作数据，但 alpha.9 缺少完整的默认 scene-to-display 基础放置。用户曝光为 0、曲线为线性时，许多正常曝光 RAW 会保留数档传感器高光余量，因此直接编码到显示空间会显得明显偏暗。Sony Creative Look 随后只能在错误的基础底图上工作。

这不是把 LibRaw 的逐照片 `auto_bright` 重新打开。alpha.10 仍禁止解码器根据每张照片的直方图自动改曝光。

## Jixel Neutral v1

RAW 专用处理顺序现在是：

```text
LibRaw scene-linear ProPhoto
  → camera WB / camera matrix（解码阶段）
  → 用户 Exposure / WB delta / highlights / shadows / contrast
  → Jixel Neutral v1：固定 +2.5 EV scene placement
  → linear ProPhoto → perceptual/display working color
  → Creative Look / HSL
  → output gamut mapping + soft highlight shoulder
  → user curves
  → output transfer function / ICC
```

`+2.5 EV` 是**内部基础显影常数**，不是把用户曝光滑块设为 +2.5。用户界面仍显示 `曝光 0.00`，表示相对于 Jixel Neutral 基准没有额外曝光补偿。

选择固定基准而不是逐图自动亮度有三个目的：

1. 同一批照片的 0 EV 具有稳定含义；
2. 批量同步不会因图像内容变化而偷偷改变亮度；
3. Sony Look / LUT 标定可以建立在可重复的 RAW 基线上。

已有软高光 shoulder 继续将经过基础放置后的亮部平滑压入输出范围，避免简单乘增益造成硬裁切。

## 不受影响的输入

已经是显示参考的 sRGB/JPEG/TIFF 输入不应用 Jixel Neutral RAW base gain。因此原来的 sRGB identity 与线性光曝光测试保持独立。

## 兼容性

处理引擎身份：

```text
jixellight-linear-v4-base1-look3
```

它会使旧预览缓存失效。原始 RAW 文件和 Adjustment JSON 不会被重写。

alpha.9 或更早版本针对旧 RAW 基线拟合的 `image-specific-fit` / `multi-scene-empirical-fit` `.jlook.json` 不能安全复用，因为那些 LUT 可能把“基础画面太暗”当成外观差异学进去。alpha.10 导入时检查拟合证据的 `engineVersion`，不匹配就要求重新拟合。没有拟合证据的普通 `.cube` 仍可按用户指定方式导入。

## 验收

自动测试要求：

- 3% scene-linear 中性值在 RAW 0 EV 下落在正常显示中间调附近；
- 用户 +1 EV 仍发生在线性场景域并显著提亮；
- 18% 传感器饱和值在固定场景放置后进入亮部并由 shoulder 平滑保护；
- sRGB/display 输入不获得 RAW base gain；
- CPU 独立参考、并行 CPU 与 GPU 均使用相同的基础增益；
- Windows / macOS / Linux 真实 Sony ARW 流程重新生成经验 profile，并继续通过 CPU/GPU、GUI、直方图、导出与部署包验证。

## 边界

Jixel Neutral v1 是 JixelLight 的独立基础显影设计，不是 Sony、Adobe、Capture One 或其他厂商的私有 tone curve。固定 +2.5 EV 也不是对所有相机、所有 ISO 的最终相机标定。后续可以在保持 `Exposure 0` 稳定语义的前提下，引入经过真实灰卡/曝光序列验证的相机基线校准，但不能退回依赖画面内容的隐式逐图 auto-bright。
