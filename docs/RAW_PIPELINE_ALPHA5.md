# JixelLight RAW Processing Graph — alpha.5

alpha.5 将 RAW 处理从“16-bit 显示 RGB 后调整”升级为明确分阶段的 RAW Processing Graph。

处理顺序：

1. RAW Sensor decode / black-white normalization（LibRaw）
2. Camera white balance baseline
3. Demosaic
4. Camera matrix / embedded color data
5. Linear ProPhoto RGB working space
6. White-balance delta / exposure / highlight recovery / tonal zones / contrast
7. Perceptual color layer：Hue / Saturation / Vibrance / 8-band Color Mixer
8. Display-linear conversion
9. Master Tone Curve + per-channel RGB Curves
10. Display shoulder / gamut handling
11. sRGB transfer for preview/export
12. 1024-bin RGB/Luma scopes from the current rendered result

关键原则：RAW 在进入最终显示转换前不先制作成 gamma-encoded sRGB 成片。16-bit 只是精度，不再被当作“RAW 域”的代名词。

当前仍属于 CPU reference pipeline。后续 GPU/RHI 实现必须保持相同处理阶段与参数语义。Camera profile 当前使用 LibRaw 的 embedded/built-in camera color data；更完整的 DCP/ICC profile browser 与显示器 ICC transform 仍是后续独立层。
