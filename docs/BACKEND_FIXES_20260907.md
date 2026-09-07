# alpha.7 跨平台修复记录 / Backend fixes

2026-09-07（Asia/Tokyo）。本文件补充 PERFORMANCE_ALPHA7.md 和 VALIDATION_20260907.md；较早的恢复检查点仅为历史记录。最终各平台结果以本次提交对应的 CI 报告为准。

## 1. RAW 工作线程栈溢出

PR #11 最初的 macOS 构建成功，但 core-tests 在 controllerImportsRealRawIntoWideGamutPipeline 中触发 SIGBUS。LibRaw 解码器原来直接位于线程栈上。本地验证 SDK 的 sizeof(LibRaw) 为 767320 字节，不能安全放入 512 KiB 工作线程栈。

- 完整解码和内嵌预览提取均改用 std::unique_ptr<LibRaw> 管理堆上的实例。
- 新增 raw-worker-stack 测试，在明确为 512 KiB 的 QThread 中依次提取预览和完整解码真实 DNG。
- 同一小栈测试，旧实现本地 SIGSEGV，修复实现通过。
- 修复提交 9e1e4ff 的 CI 34074948104：macOS 四组测试、Metal 检查、程序打包均通过；Windows 的 core、performance 和 raw-worker-stack 通过。Windows GPU 数值问题见下节。

## 2. CPU / GPU 色域边界不连续

Windows D3D11 WARP 能正常运行计算着色器，但混合 HSL 参数测试出现最大通道误差 962/65535。扩大本地 llvmpipe 对照到 1024×1024 像素后，也复现最大误差 2606/65535，并定位到负色域压缩。

原实现只在最小通道小于零时乘以固定的 0.995 内缩系数。这在零边界造成不连续：CPU 和 GPU 很小的浮点差异可能落在边界两侧，被放大为明显的像素差异。

修复在 CPU 和着色器中一致执行：在最小通道 [-0.001,0] 区间平滑引入最多 0.5% 的内缩；<= -0.001 时维持原映射。没有降低浮点存储精度，也没有放宽已有 GPU 最大 40/65535 的误差标准。

### 输出兼容性说明

这是一项有意的颜色正确性修复，不是对 alpha.6 所有像素逐位一致的性能替换。接近负色域边界的少数像素可能改变。保留原始 legacyProcess 用于历史比较和基准；另外提供命名清楚的 correctedReferenceProcess，仅包含相同边界修复，其余仍为独立、未优化的参考算法。生产算法对该独立参考的误差上限仍为 24/65535。

在本地 11811 像素的三组参考样本中，修复后的优化与独立修正参考最大差为 3、2、2；与未修复历史算法相比，超过 24 的像素分别为 33、18、18。因此不能声称完全保持旧版本输出。

### 新增验证

- 保留原有四组 GPU 参数、直方图计数、方向和源纹理复用测试。
- 增加百万像素 denseColorParity，仍要求每个通道最大误差 <= 40/65535；本地修复后最大为 10，超限像素为零。
- 增加 gamutBoundaryHasNoVisibleStep，检查连续输入下不再出现原先的两千级跳变。
- 全部四组本地 CTest 通过：core-tests、performance-correctness、gpu-correctness、raw-worker-stack。

本地 GPU 是 Mesa llvmpipe 软件后端。这些数值不是用户硬件性能承诺。新的 Windows / macOS 结果必须阅读最终 CI，不能用本地通过代替。

## 3. CPU 回退与界面测试

PhotoCanvas 使用 Loader，仅在 GPU 启用且有图像时创建 GpuPreview。避免纯 CPU 模式仍实例化 GPU 组件，重复输出 No QRhi 警告。现有 GPU / CPU 回退逻辑及图像版本校验保留。

SmokeRun 的报告增加源提交号和程序版本。CI 进一步运行 Windows D3D11/WARP 与 macOS Metal 的实际界面测试，并保存 JSON 和截图；Linux 原有 GPU/CPU 界面验证继续保留。窗口截图仅为辅助证据，不替代断言、日志或真正的硬件验收。

## 4. 交付边界

main 保留 alpha.6，所有改动位于 perf/20260907-a-e / PR #11。未经最终 CI 验证不发布正式版本。程序尚未进行商业代码签名或 Apple notarization。Windows WARP、Apple Paravirtual Metal、Linux llvmpipe 的正确性通过，不代表实物显卡的性能已经测量。

RAW 解码与最终导出仍为 CPU 路径；FP16、传感器级随机图块解码、完整显示器 ICC/软打样、多相机大型批量验证，不在本次完成声明内。
