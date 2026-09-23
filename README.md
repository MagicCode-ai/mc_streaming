# MagicCode Streaming

A bitstream-saving module and a companion enhancer for the video encoder, reducing transmission and storage without dropping subjective quality.

Using MagicCode AI and human visual principles, it detects residual coefficients that do not help quality, removes them, and keeps the ones that do.

- Website: https://www.magiccode-ai.com/
- Product page: https://www.magiccode-ai.com/products/ai-streaming

## What is MagicCode Streaming

MagicCode Streaming is a module that saves encoded bitstream. Based on MagicCode AI algorithms and human visual principles, it detects which residual coefficients in the bitstream do not help improve picture quality and removes them, while keeping the coefficients that do help. This reduces the compressed video that needs to be transmitted or stored without lowering subjective quality, saving bandwidth and storage for customers.

Deployment topology:

```
Sender:  Input YUV ──────────────────┐
              │                      │
              ↓                      ↓
           Encode ─────────→ Streaming → Send
                                        ↓
                                     Network
                                        ↓
Receiver:                        Receive → Decode → Play
```

Connect it after the video encoder. Pass the encoder’s input video and output bitstream; MagicCode Streaming rewrites the bitstream before it enters the send (or store) path. The receiver uses a standard decoder.

## What value can MagicCode Streaming bring

### 1. Save precious bandwidth during video transmission

In typical conferencing scenarios, MagicCode Streaming can save around 20% bandwidth; in VOD and live-streaming scenarios, it can save around 15% bandwidth.

Savings depend on the video scene and encoding bitrate. 20% or 15% is not guaranteed for every video.

### 2. Improve the interactive experience of video calls

**Measured advantages of mc_streaming on RTC**

RTC call: an Apple device sends 720p @ 1.8 Mbps @ 30 fps; an Android phone receives; media is relayed through a production server. Within the same call, mc_streaming is toggled on for 1 minute then off for 1 minute, repeated 10 times (20 minutes total), then on/off states are averaged separately.

With mc_streaming on, under almost identical BWE / RTT and the same 1800 kbps target:

| Metric | streaming on | streaming off | vs off |
|---|---|---|---|
| Mean send bitrate | 1326 kbps | 1610 kbps | Saves 17.6% |
| Bandwidth estimate | 5339 kbps | 5372 kbps | Same estimate accuracy |
| RTT | 19.3 ms | 18.9 ms | Essentially the same |
| Mean fps | 28.2 | 24.5 | Frame rate up 15.1% |
| Large stalls (>800 ms) | 7 times | 10 times | Reduced 30% |

Target bitrate is the ceiling the encoder is allowed to reach, not a volume it must fill. With the module off, send already sits near 1800 kbps (1610); with it on, the same ceiling sends only 1326 kbps. The extra ~284 kbps no longer crowds the send queue. With RTT unchanged, that shows up as higher fps and fewer stalls over 800 ms.

## What we need to pay for these values

### 1. For 720p video, <3 ms extra per-frame latency on M3

| Path | Per-frame time |
|---|---|
| x264 veryfast | 5.54 ms |
| Apple M3 hardware encode | 2.56 ms |
| Streaming rewrite | 2.93 ms |

720p hardware encode is about 2.56 ms; MCS adds about 2.93 ms, still about 5.4 ms total. That is far below the 33 ms frame interval. Real-time pressure is bitrate and queuing, not these few milliseconds of CPU. Turning on mc_streaming trades a negligible rewrite delay for lower on-the-wire bitrate and a more stable frame interval — not software encode for hardware encode.

### 2. Mobile package size increase <1 MByte

| Android shared library | iOS dynamic library |
|---|---|
| 900K | 958K |

## Even better together with [MagicSR](https://github.com/MagicCode-ai/SuperResolution)

| | Resolution | Path | Actual bitrate |
|---|---|---|---|
| Before | 1280x720 | encode only | 1838 kbps |
| After | 854x480 → 1280x720 | Streaming + 1.5× [MagicSR](https://github.com/MagicCode-ai/SuperResolution) |1026 kbps |

## Streaming Technical FAQ

**What does MagicCode Streaming do?**

MagicCode Streaming is a module that saves encoded bitstream. Based on MagicCode AI algorithms and human visual principles, it detects which residual coefficients in the bitstream do not help improve picture quality and removes them, while keeping the coefficients that do help. This reduces the compressed video that needs to be transmitted or stored without lowering subjective quality, saving bandwidth and storage for customers.

**If residual coefficients in the bitstream are changed, will that make the decoder output diverge from the encoder reconstruction and cause quality issues?**

No. MagicCode Streaming works as follows: it parses the bitstream to obtain coding information such as mode, MV, and QP, then reuses that information together with the input image to encode, obtain residuals, analyze and process those residuals, compress them with QP, and then run the decode path for reconstruction. In essence, MagicCode Streaming can be seen as an encoder that reuses mode, MV, and QP. Therefore the decoder output of a bitstream processed by MagicCode Streaming is guaranteed to match the reconstruction inside MagicCode Streaming, and this class of quality issue does not occur.

**In which scenarios can MagicCode Streaming be used?**

MagicCode Streaming can be used in almost all video scenarios, such as RTC real-time calls, live streaming, VOD, and cloud gaming. Wherever there is a video encoder, MagicCode Streaming can be deployed: connect it after the encoder, pass it the encoder input video and output bitstream, and it can start working.

**In low-latency RTC calls, if send bitrate drops, will that make network bandwidth estimation inaccurate?**

No. Take GCC/TWCC congestion control in WebRTC as an example. With the correct wiring — `setBitrate` to constrain → MCS shrinks `EncodedImage` → then hand off to WebRTC packetization / paced sending — GCC will not mis-estimate bandwidth. Packet size, send time, arrival time, and loss used by congestion control all come from the bitstream after MagicCode Streaming has shrunk it. Both delay-based and loss-based estimates remain consistent about “how much this path is currently carrying.”

**How much bandwidth can MagicCode Streaming actually save?**

It depends on the encoding scenario and the bandwidth allocated to it: if the allocated bandwidth is too high, MagicCode Streaming can save more; if not, it can save less. For example, with constant bitrate (CBR) encoding, some simple scenes do not need that much bandwidth, so MagicCode Streaming can reduce it; some complex scenes do need that much bandwidth, so MagicCode Streaming can do relatively little.

**The principle looks similar to encoder VBR rate control. Why not just use VBR?**

MagicCode Streaming and VBR rate control look similar: both use less bandwidth in low-complexity scenes and more in high-complexity scenes to save bitrate. There are two differences versus VBR:

1. Encoder rate control uses QP to control an entire block; MagicCode Streaming uses finer-grained pixel-level control. The two do not conflict, so some scenes can use VBR and still use MagicCode Streaming to further reduce bandwidth.
2. Some scenes (for example, high-real-time RTC) cannot use VBR, because network bandwidth is fixed. With VBR, using less bandwidth in low complexity is fine, but high complexity needs high bandwidth and may overflow the network, causing bitstream buildup, extra delay, or packet loss. MagicCode Streaming only reduces bandwidth and never increases it, so this problem does not occur.

**How should quality after MagicCode Streaming be evaluated? Can objective metrics such as PSNR and SSIM be used?**

MagicCode Streaming uses human visual principles (relative sensitivity, masking, nonlinearity, and similar properties) and AI analysis of image content and pixels: it keeps information where the eye can tell the difference and removes it where the eye cannot, so bitrate drops while remaining imperceptible. Objective metrics such as PSNR and SSIM compare pixels and content statistics, so they will show a drop. The goal of MagicCode Streaming is to save bitrate with no drop in subjective quality. Output bitstream quality should be judged by human viewing, not by objective metrics.

## Repository layout

```
mc_streaming/
  include/     Public C API (mc_streaming.h)
  lib/         Prebuilt libraries (Android / iOS / Linux / macOS / Windows)
  doc/         Getting Started, API, Integration, EULA, Privacy, FAQ
  demo/        Pointer to the sample app
```

Demo (1-to-1 real-time video call): https://github.com/MagicCode-ai/APP/tree/main/video-call

Start with [doc/README.md](doc/README.md). The native header `include/mc_streaming.h` is the API contract.

## License

Software is free under the EULA; professional services (integration support, customization, SLA) are available separately.

Before using the SDK, read the [EULA](doc/MagicCode%20Streaming%20Software%20End%20User%20License%20Agreement%20(EULA).md), [Privacy Policy](doc/MagicCode%20Streaming%20Software%20Privacy%20Policy.md), and [Data Reporting Notice](doc/MagicCode%20Streaming%20Software%20Data%20Reporting%20Notice.md).
