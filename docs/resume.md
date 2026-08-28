# Resume and interview material

Use only measurements you can reproduce. The examples below separate verified repository facts from machine-dependent metrics.

## 中文项目名称

**PulseForge：可故障注入的Linux虚拟遥测设备与数字孪生系统**

## 中文简历表述

- 独立设计无硬件依赖的Linux虚拟遥测设备，基于`hrtimer`、字符设备、等待队列与自管理环形缓冲区实现周期采样、阻塞/非阻塞读取及`poll/epoll`异步数据通路。
- 设计32字节稳定UAPI及`ioctl`控制面，实现CRC-32完整性校验、运行时参数配置和统计计数；使用C采集器批量读取并输出NDJSON/CSV。
- 提出“确定性故障指纹”机制，以xorshift32种子复现丢包、尖峰、冻结、噪声与漂移，并使用Python位级数字孪生生成相同协议帧和SHA-256事故指纹。
- 建立GitHub Actions验证流水线，覆盖多Python版本单元测试、`-Werror`用户态C构建、cppcheck静态分析及Linux内核模块编译。

完成Linux实机测试后可补充：

> 在XX Hz采样率和XX深度环形缓冲区下连续运行XX小时；测得端到端P99延迟XX μs、吞吐量XX samples/s，零CRC错误。

不要在实际测量前填写这些数字。

## English resume bullets

- Designed a hardware-independent Linux telemetry device using `hrtimer`, a misc character device, wait queues and a bounded kernel ring, supporting blocking/non-blocking reads and poll/epoll consumers.
- Defined a stable 32-byte UAPI with runtime ioctls, IEEE CRC-32 validation and observable overrun semantics; implemented batched C utilities for control and NDJSON/CSV collection.
- Introduced deterministic incident fingerprints: seeded xorshift32 fault injection reproduces drops, spikes, freezes, noise and drift in a bit-exact Python digital twin.
- Built CI across multiple Python versions with unit tests, strict C warnings, cppcheck and out-of-tree Linux kernel-module compilation.

## Interview discussion prompts

Be prepared to explain:

1. Why a high-resolution timer callback cannot sleep or allocate with `GFP_KERNEL`.
2. Why the PRNG advances even when a frame is dropped.
3. The difference between injected drops and ring overruns.
4. Why CRC is not a substitute for cryptographic authentication.
5. How the wait queue and `epoll` readiness path avoid busy waiting.
6. What changes are required to support multiple independent readers.
7. How a QEMU MMIO frontend would change the driver architecture.
8. Which measurements are needed before calling the data path real-time.

