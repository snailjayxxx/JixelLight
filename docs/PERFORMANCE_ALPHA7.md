# alpha.7 — A–E 性能改造与验收

基线：`main@f0f43472c5fbb0c6e3e4eb540a7c06fdb43be90b`（alpha.6）。A–E 是互补改造，不是替代方案。本文区分实际实现和后续优化，未在用户电脑上测量的指标不作承诺。

## 数据路径

```text
不可变原文件
  ├─ 相机内嵌 JPEG → 占位显示（不能编辑，不参与直方图）
  ├─ 已校验的线性预览缓存 → 可编辑预览
  └─ LibRaw CPU 开发 / 普通图片 ICC 转换 → Linear ProPhoto RGBA64
       ├─ 有预算的开发缓存 / 邻图预读
       ├─ 视区准备 → RGBA32F → 融合 GPU 调色 → GPU 纹理直接显示
       │                                    └─ 分块直方图 → 合并 → 16,400 字节回读
       ├─ CPU 预览回退 → 独立后台直方图
       ├─ 完整尺寸的 128 行处理 → 全分辨率精确统计
       └─ 冻结参数 → 128 行处理 → 目标色域映射 → JPEG scanlines + ICC
```

CPU 与 GPU 共享 `ProcessingPlan` 的参数布局和运算顺序。RGBA64 是每通道 16 位整数；RGBA32F 是每通道 32 位浮点，并不是“同样精度”的两种名字。GPU 暂不启用 FP16。LibRaw 入口仍是原版本的 16-bit 线性结果，不能恢复该阶段已截断的值。

## A：最新请求、取消与保存

`LatestJob` 每实例一个执行任务、一个覆盖式待执行请求。新请求发出合作式取消标记；不支持即时中断的系统调用完成后，版本校验仍阻止旧结果发布。后台任务只持有不可变参数快照。销毁时取消并等待工作完成，不允许悬空回调。

控制器区分照片 epoch、参数 revision、视区 preparation generation。切图、移动视区、调参都会使旧结果失效。图像发布不等待直方图；统计状态显示“更新中（旧统计）”，不会把旧数值称作当前精确结果。

项目写入由专用线程管理 SQLite 连接，批量事务、WAL、3 秒 busy timeout。参数改动 300ms 合并，连续操作最长 1 秒提交一次；松手、切图会提交，关闭窗口会检查 flush 成功。保存失败保留内存状态并提示。未提交期间突然断电的少量操作仍可能丢失，这不是零丢失事务日志。

CPU 快速预览长边最多 1024，停止 120ms 后补齐正常视区精度；GPU 编辑不必为每次拖动降采样。快速缩图后调色是近似预览，最终导出不使用缩图代替原图。

## B：减少计算，而不是牺牲结果

白平衡增益、固定色彩矩阵、曝光系数和启用标记在编译参数时产生。中性明暗/色彩操作绕过不必要数学；HSL 仅计算实际启用色带。保留色域、范围限制和输出变换。没有将“所有滑块为零”误当成“可以直接显示线性 RAW”。

CPU 使用共享、有界的行块线程池；调用线程也参与处理。预览、导出、精确统计不会再各自无限创建线程。暂不加入未经基准证明有收益的全局 LUT、手写 AVX/NEON 或 fast-math。`tests/LegacyPipeline.cpp` 保留原 alpha.6 数学路径作对照。

## C：预算与缓存失效

开发缓存默认 512MiB，按 QCache 成本淘汰。仍被当前照片、任务或导出引用的 QImage 可能在缓存淘汰后继续占用内存，因此该数值不是进程 RSS 硬上限。诊断包额外记录实际 RSS。后台一次最多解码一个 RAW；预读最多一张，并检查估算的两张容量。独立纹理、中间缓冲和解码器本身也需要内存。

磁盘线性预览长边最多 2048、总量约 1GiB，按写入时间淘汰。文件内包含受限尺寸/长度、元数据、像素和 SHA-256 校验，原子写入。源指纹包括引擎版本、LibRaw 版本、开发配置、规范路径、大小、修改时间与首尾各 64KiB。它不是完整文件内容哈希；外部软件若刻意保留时间/大小并只改文件中间，需要清缓存后重读。

100%/200% 模式只计算可见原始像素区域，但底层 RAW 仍完整解包/开发；这不是压缩 RAW 的随机图块解码器。源切换后旧纹理结果不能以新照片名发布。

## D：GPU 与色彩边界

Qt 版本固定为 6.8.3。QRhi 对版本兼容性保证有限，升级 Qt 要重编译并执行全部 GPU 测试。源/输出纹理为 RGBA32F，预览边长最多 4096；纹理大小和能力检查失败时回退 CPU。

GPU 运算在 Qt Quick RHI 渲染线程内执行。相同源图仅参数改变时不重复上传纹理；数据只更新约 400 字节 uniform。多种点运算按原顺序融合为一个 compute pass。显示直接读取 GPU 输出，无每帧整图 CPU 回读。只有诊断截图/测试显式读取图像；源尺寸变化或析构时可能等待尚未完成的回读，不在每次滑块更新时 `finish()`。

Mac 使用 Metal，Windows 默认 D3D11；OpenGL Compute 用于兼容与软件后端验证。支持 GPU 不代表每张卡都已实测；设备丢失、驱动、内存压力仍需要目标机器验证。CPU/GPU 允许非常小的浮点误差，最终 16-bit 输出对照测试有明确阈值。

导出直接从编辑工作结果进入目标色域，不再先裁成 sRGB 再转 P3/Adobe RGB/ProPhoto。显示器 ICC、软打样、DCP 管理、HDR 显示和高位深 TIFF 未在本次实现。曲线仍是既有输出相关语义，不宣称不同输出色域下逐像素完全相同。

## E：统计语义

RGB 加亮度 4 通道 × 1024 bins。支持的 CPU rebin 分辨率是 256、512、1024。GPU 每组统计局部直方图，再合并；输出包含像素数、阴影/高光计数，回读共 4100 × 4 = 16,400 字节。GPU 回读通过计数和、参数版本、照片/视区状态校验后才发布。

预览统计显示“GPU 预览统计”“视区预览”或 CPU 预览；选中“全分辨率”后，停止操作约 600ms 后按完整原图计算。全分辨率统计在 CPU 分块执行，不先分配完整输出图；当前有效全分辨率结果不会被晚到的预览统计降级覆盖。全分辨率统计是 sRGB 输出阶段，不是传感器饱和统计，也不是监视器截图统计。

亮度使用输出编码值的加权亮度 `(2126R + 7152G + 722B + 5000) / 10000`，不是场景线性亮度。阴影判断所有通道 ≤257，高光判断任一通道 ≥65278；边界沿用 alpha.6。

## 导出与诊断

单张和批量导出均冻结照片路径/状态，不受之后切图调参影响。编码以 128 行为单位，经 libjpeg scanline 接口输出。关闭需要保留整图 DCT 系数的 Huffman 优化，避免伪“流式”写入；JPEG 质量和 ICC 不因此省略。批量自动生成不冲突文件名；任何已导入原图都拒绝作为单张输出目标。取消/失败不提交半成品文件。预读和磁盘读取不能保证随时中断，取消响应时间依赖底层调用。

日志线程持有文件、100ms 批量写入；4096 条有界队列，溢出计数写入诊断。严重日志唤醒写入并保留紧急崩溃标记。新增 `performance.json` 包含各阶段耗时、缓存命中、上传/读回字节、进程 RSS、GPU 后端、任务版本等。耗时不冒充屏幕呈现延迟；`gpu_previous_frame_ms` 也只是前一完整 RHI 帧，不是单节点耗时。

诊断保留 session log、操作轨迹、参数、预览和统计上下文，不自动上传到外部。预览文件是当前参数的 CPU 参考捕获，不是 GPU 截图。诊断 ZIP 可能包含照片预览、路径与元数据，分享前应确认隐私。

## 构建与测试

依赖：C++20、CMake ≥3.24、Qt 6.8.3（含 QtShaderTools/GuiPrivate）、LibRaw、LittleCMS、Exiv2、libjpeg-turbo。Windows/macOS 构建命令见 README。产物在 `build/bin`；Windows 多配置为 `build/bin/Release`；Mac 主程序为 `.app`。

```sh
cmake --build build --config Release --parallel
# 指向自己有权使用的一张 RAW，以运行真实解码测试
JIXELLIGHT_TEST_RAW=/path/sample.DNG ctest --test-dir build -C Release --output-on-failure
./build/bin/JixelLightBenchmark > benchmark.json
# GPU 不可用时明确失败，禁止将 SKIP 计为 GPU 已验证
JIXELLIGHT_REQUIRE_GPU=1 ./build/bin/JixelLightGpuTests
./build/bin/JixelLight --smoke-report smoke.json --screenshot smoke.png /path/sample.DNG
```

Windows 测试用 D3D11 WARP 验证 shader/统计正确性，不作为硬件速度基准。Linux CI 使用 llvmpipe；Mac CI 若没有 Metal 设备会明确 SKIP，不能据此声称 Metal 验证成功。

基准固定合成 Linear ProPhoto RGBA64，分别测 alpha.6 串行、优化串行、优化并行，预热一次、记录五次中位数。**不包含 RAW 解包、GPU、显示、磁盘、完整软件响应**，不能把某个基准倍数称为软件整体提速。

## 可选环境变量

| 变量 | 含义 |
|---|---|
| `JIXELLIGHT_FORCE_CPU=1` | 强制 CPU 预览，排查驱动问题。 |
| `JIXELLIGHT_CPU_THREADS=1..32` | CPU 行块并行预算；默认 idealThreadCount−2，限制为 1..8。 |
| `JIXELLIGHT_CACHE_MB=64..4096` | 开发图像缓存 MiB，默认 512。 |
| `JIXELLIGHT_RAW_MEMORY_MB` | RAW 预估解码内存上限，默认约 3GiB；不能保证外部解码器的实际峰值。 |
| `JIXELLIGHT_GPU_TIMESTAMPS=1` | 支持时启用 RHI 帧计时，默认关闭以免测量影响交互。 |
| `JIXELLIGHT_LOG_STDERR=1` | 同时将普通日志输出到标准错误。 |
| `JIXELLIGHT_REQUIRE_GPU=1` | 测试时强制要求 compute/GPU，普通编辑不依赖它。 |

## 仍需目标机器验证或后续实现

真实相机样本矩阵（Sony/Canon/Fuji 等）、不同驱动和显示器、61MP/更大 RAW 内存与长期批处理；非破坏项目加载/更完整恢复功能；线程/解码器更细的全局资源调度；可选 half-size RAW 开发与 FP16 误差验证；GPU 导出、空间降噪/锐化/镜头节点。当前视区分块仅适用于已经实现的逐像素调色，未来邻域算法必须处理 halo，不能直接复用而产生边缘接缝。
