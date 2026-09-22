# MagicCode Streaming 软件常见问题与故障排除

**文档版本：** v1.0.0。**发布日期：** 2026 年 9 月 19 日。

## 1. 通用问题

**MagicCode Streaming 是做什么的？**  
MagicCode Streaming 是一个能节省编码码流的模块。基于 MagicCode 的 AI 算法和人眼视觉原理，它能检测码流中哪些残差分量对画质提升没有帮助并删除这些分量，同时保留对画质提升有用的分量，从而在主观画质没有下降的情况下减少需要传输或存储的压缩视频流，为客户节省带宽和存储资源。

**如果改变码流里面的残差分量，会不会造成解码端输出和编码端重建不一致而产生画质问题？**  
不会。MagicCode Streaming 的工作流程是：解析码流获取模式、MV、QP 等编码信息，然后复用这些信息和输入的图像进行编码，得到残差，再对残差进行分析和处理，最后再通过 QP 压缩，然后再走解码流程进行重建。本质上可以认为 MagicCode Streaming 就是一个复用了模式、MV、QP 的编码器，所以经过 MagicCode Streaming 处理后的码流在解码端的输出和在 MagicCode Streaming 中的重建肯定是一致的，不会有这类画质问题。

**MagicCode Streaming 能在哪些场景中使用？**  
MagicCode Streaming 能在几乎所有视频场景中使用，例如 RTC 实时通话、直播、点播、云游戏。只要有视频编码器的地方就能部署 MagicCode Streaming：把它接到视频编码器后面，把编码器输入的视频和输出的码流传给它就可以开始工作。

**在 RTC 这种低延时通话场景中，如果发送码率减少，会不会导致网络带宽估计不准？**  
不会。以 WebRTC 中的 GCC/TWCC 拥塞控制算法为例，如果按照正确的接法：`setBitrate` 去压 → MCS 缩小 `EncodedImage` → 再交给 WebRTC 分包 / paced 发送，GCC 就不会把带宽估计错。拥塞控制用的包长、发送时刻、到达时刻、丢包，都来自 MagicCode Streaming 缩小后的码流。基于时延 / 基于丢包的估计对「这条链路现在载了多少」仍然自洽。

**MagicCode Streaming 具体能节省多少带宽？**  
这取决于编码场景和为该场景分配的带宽：如果为该场景分配的带宽过多，MagicCode Streaming 能节省得较多；反之能节省的就比较少。例如采用恒定比特率（CBR）编码时，一些简单场景并不需要那么多带宽，就能通过 MagicCode Streaming 减少带宽；而一些复杂场景需要这么多带宽，MagicCode Streaming 能做的就比较有限。

**看上去 MagicCode Streaming 的原理和编码器的 VBR 码率控制类似，为什么不直接采用 VBR？**  
MagicCode Streaming 和 VBR 码率控制看起来类似：都是在低复杂度场景用较少带宽，在高复杂度场景用较高带宽来节省码率。但和 VBR 有两点区别：

1. 编码器的码率控制是通过 QP 来控制一整个块，MagicCode Streaming 采用的是更细粒度的像素级控制。这两者并不冲突，所以有的场景可以采用 VBR，仍然可以使用 MagicCode Streaming 来进一步降低带宽。
2. 有的场景（例如需要高实时的 RTC）不能采用 VBR，因为这时网络带宽是一定的。若采用 VBR，低复杂度时用较少带宽没有问题，但高复杂度时需要高带宽，很可能会因为编码带宽超过网络带宽而产生码流堆积，从而增加延时或引起丢包。MagicCode Streaming 只是减少带宽，并不会增加带宽，所以不会有这个问题。

**如何评价 MagicCode Streaming 处理后的画质？可以使用 PSNR、SSIM 这种客观指标吗？**  
MagicCode Streaming 通过人眼视觉原理（例如人眼的相对敏感度、掩蔽效应、非线性等特性），利用 AI 分析图像内容和像素：对人眼能分辨的地方保留信息，对人眼不能分辨的地方删除信息，从而在降低码率的同时让人眼无感知。但 PSNR、SSIM 这类客观指标基于像素和内容统计比较，所以会看到指标下降。MagicCode Streaming 的目标是节省码率同时主观画质无下降。评价输出码流画质应通过人眼主观观看，不能用客观指标来评价。

**软件会上传我的视频或码流吗？**  
不会。v1.0.0 不会上报 YUV 或 Annex-B 载荷。标准 SDK 会发送一个很小的技术数据包（应用实例 UUID、设备型号、SDK 版本、平均每帧耗时、尺寸均值、错误码、上报次数），用于**计费/授权计量**和**问题定位**。包内不上报设备 IP。详见《隐私政策》和《数据上报说明》。标准版没有公开开关可关闭上报；如法规或产品形态需要无上报库，请向 MagicCode 书面申请。

**应该使用哪个 codec profile？**  
MagicCode Streaming 提供两个 profile：`MCS_H264_ZERO_DELAY_8BIT` 和 `MCS_H264_8BIT`。`MCS_H264_8BIT` 面向任何视频场景，在任何视频应用中都可以使用，但处理会稍慢，因为算法需要更多运算来覆盖各种视频场景。`MCS_H264_ZERO_DELAY_8BIT` 专为超低延时视频应用设计，例如视频通话。

**如何读取版本号？**  
`mc_streaming_get_version()` 返回 `"1.0.0"`。

## 2. 集成常见问题

**为什么会出现缺失符号 / 空编码结果？**  
确认 `libmc_streaming.a` 已加入原生链接（Android arm64-v8a 或 iOS 真机 arm64），并且应用调用了 `mc_streaming_enable`。

**可以传入 AVCC（长度前缀 NAL）吗？**  
不可以。只支持 Annex-B 起始码。

**每次都要传入 SPS/PPS 吗？**  
传入编码器产出的 AU 即可。该 AU 中的非 VCL NAL 会被拷贝。不需要调用单独的头信息 API。

**YUV 必须和码流尺寸一致吗？**  
必须。宽高必须与编码尺寸或显示尺寸一致（否则返回 `MCS_ERR_CROP`）。

## 3. 初始化与处理问题

| 现象 | 可能原因 | 处理办法 |
|------|----------|----------|
| `MCS_ERR_INVAL`，`bs_size == 0` | 平面指针为空、AU 长度为 0、句柄指针无效 | 检查 `struct_size`、指针、`au_size` |
| `MCS_ERR_KEY` | `frame_type` 与 I/IDR 不一致 | 按编码器的关键帧标志 / NAL 类型设置 `frame_type` |
| `MCS_ERR_CROP` | I420 尺寸 ≠ 编码/显示尺寸 | 使用送入编码器的同一块缓冲（裁剪/缩放之后） |
| `MCS_ERR_OUTPUT` | 输出缓冲小于一次拷贝所需大小 | 容量 ≥ 输入 AU |
| P/B 上出现 `MCS_ERR_SIZE` | 分辨率变化但没有关键帧 | 强制 IDR / 重建编码器；上限只在关键帧上增长 |
| `MCS_ERR_DECODE` | AU 无法解析 | 确认 Annex-B、每次调用一张图、8-bit 逐行 |
| `MCS_ERR_BYPASS` | 某帧失败后持续直通拷贝 | 等到下一帧 I/IDR；检查前一次错误 |
| 码率没有变化 | 只有 I/IDR 的码流，或处理未生效 | 需要 P/B；检查 profile，并确认确实调用了会话 API |

## 5. 诊断检查清单

1. `mc_streaming_get_version()` 为 `1.0.0`。
2. 记录 `codec_type`、宽、高、`frame_type`、`au_size`、返回码、`bs_size`。
3. 确认 `libmc_streaming.a` 已加入原生链接。
4. 确认硬件编码器是 H.264 Annex-B，不是 HEVC。
5. 上架构建前查阅《已知问题》和 EULA。
