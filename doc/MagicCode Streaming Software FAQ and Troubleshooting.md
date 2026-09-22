# MagicCode Streaming Software FAQ and Troubleshooting

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

## 1. General

**What does MagicCode Streaming do?**  
MagicCode Streaming is a module that reduces encoded bitstream bitrate. Using MagicCode AI algorithms and human visual principles, it detects residual components that do not improve perceived quality, removes those components, and keeps the ones that do. The compressed video that must be transmitted or stored is therefore smaller, with no drop in subjective quality, which saves bandwidth and storage.

**If residual components in the bitstream are changed, will the decoder output diverge from encoder reconstruction and cause quality problems?**  
No. MagicCode Streaming works as follows: it parses the bitstream to obtain coding information such as mode, motion vectors, and QP; reuses that information together with the input picture to encode and obtain residuals; analyzes and processes those residuals; compresses them with QP; then runs a decode path to reconstruct. In effect, MagicCode Streaming is an encoder that reuses mode, motion vectors, and QP. The decoder output of a processed bitstream therefore matches reconstruction inside MagicCode Streaming, and there is no quality mismatch of that kind.

**Which scenarios can MagicCode Streaming be used in?**  
Almost any video scenario: real-time RTC calls, live streaming, VOD, cloud gaming, and similar. Wherever a video encoder is present, MagicCode Streaming can be placed after it. Pass the encoder’s input video and output bitstream to the module and it can start working.

**In a low-latency RTC call, if send bitrate drops, will network bandwidth estimation become inaccurate?**  
No. Take WebRTC GCC/TWCC congestion control as an example. With the correct wiring—`setBitrate` drives the encoder, MagicCode Streaming then shrinks the `EncodedImage`, and only then does WebRTC packetize and paced-send—GCC will not mis-estimate bandwidth. Packet size, send time, arrival time, and loss used by congestion control all come from the bitstream after MagicCode Streaming has reduced it. Delay-based and loss-based estimates of “how much this path is carrying now” remain consistent.

**How much bandwidth can MagicCode Streaming save?**  
It depends on the encoded scene and the bandwidth allocated to that scene. If the scene is over-allocated, MagicCode Streaming can save more; if allocation is already tight, it can save less. For example, with constant bitrate (CBR) encoding, simple scenes do not need that much bandwidth, so MagicCode Streaming can reduce it; complex scenes need that bandwidth, so MagicCode Streaming can do relatively little.

**MagicCode Streaming looks similar to encoder VBR rate control. Why not just use VBR?**  
They look similar in that both use less bandwidth in low-complexity scenes and more in high-complexity scenes. There are two differences:

1. Encoder rate control uses QP to control an entire block. MagicCode Streaming uses finer-grained, pixel-level control. The two do not conflict, so a scene can use VBR and still use MagicCode Streaming to reduce bandwidth further.
2. Some scenarios, such as high-realtime RTC, cannot use VBR. Network bandwidth is then fixed. VBR is fine when complexity is low and bitrate is small, but when complexity rises and the encoder wants a high bitrate, that bitrate can exceed the network, causing bitstream backlog, extra delay, or packet loss. MagicCode Streaming only reduces bitrate; it never increases it, so it does not have this problem.

**How should quality after MagicCode Streaming be evaluated? Can objective metrics such as PSNR or SSIM be used?**  
MagicCode Streaming uses human visual principles—relative sensitivity, masking, nonlinearity, and similar properties—and AI analysis of picture content and pixels. It keeps information where the eye can tell a difference and removes information where the eye cannot, so bitrate drops with no perceptible change. Objective metrics such as PSNR and SSIM compare pixels and content statistics, so those scores will fall. The goal of MagicCode Streaming is bitrate savings with no subjective quality loss. Evaluate output quality by subjective viewing, not by objective metrics.

**Does the software upload my video or bitstream?**  
No. v1.0.0 does not report YUV or Annex-B payloads. The standard SDK sends a small technical packet (app-instance UUID, device model, SDK version, average per-frame time, size averages, error code, report count) for billing / license metering and issue diagnosis. It does not send a device IP in the packet. See the Privacy Policy and Data Reporting Notice. There is no public switch to turn telemetry off; ask MagicCode in writing if you need a non-reporting archive.

**Which codec profile should I use?**  
MagicCode Streaming provides two profiles: `MCS_H264_ZERO_DELAY_8BIT` and `MCS_H264_8BIT`. `MCS_H264_8BIT` is designed for any video scenario and can be used in any video application; it is slightly slower because the algorithm does more work to cover a wide range of scenes. `MCS_H264_ZERO_DELAY_8BIT` is designed for ultra-low-latency applications such as video calls.

**How do I read the version?**  
`mc_streaming_get_version()` returns `"1.0.0"`.

## 2. Integration FAQ

**Why do I get missing symbols / empty encode?**  
Confirm `libmc_streaming.a` is on the native link line (Android arm64-v8a or iOS device arm64) and that the app calls `mc_streaming_enable`.

**Can I pass AVCC (length-prefixed NAL)?**  
No. Annex-B start codes only.

**Do I pass SPS/PPS every time?**  
Pass the AU your encoder produced. Non-VCL NALs in that AU are copied. You do not call a separate header API.

**Must YUV match the bitstream size?**  
Yes. Width/height must match coded or display size (`MCS_ERR_CROP`).

## 3. Initialization and process problems

| Symptom | Likely cause | What to do |
|---------|--------------|------------|
| `MCS_ERR_INVAL`, `bs_size == 0` | NULL planes, zero AU, bad handle pointer | Check `struct_size`, pointers, `au_size` |
| `MCS_ERR_KEY` | `frame_type` vs I/IDR mismatch | Set `frame_type` from the encoder’s key flag / NAL type |
| `MCS_ERR_CROP` | I420 size ≠ coded/display | Use the same buffer you fed the encoder, after crop/scale |
| `MCS_ERR_OUTPUT` | Buffer smaller than a copy | Capacity ≥ input AU |
| `MCS_ERR_SIZE` on P/B | Resolution changed without a key | Force IDR / re-create encoder; caps grow only on key |
| `MCS_ERR_DECODE` | Unparseable AU | Confirm Annex-B, one picture per call, 8-bit progressive |
| `MCS_ERR_BYPASS` | Sticky copy after a failed picture | Wait for next I/IDR; inspect the previous error |
| No bitrate change | I/IDR-only stream, or processing not applied | Need P/B; check profile and that the session API is actually called |

## 5. Diagnostic checklist

1. `mc_streaming_get_version()` is `1.0.0`.
2. Record `codec_type`, width, height, `frame_type`, `au_size`, return code, `bs_size`.
3. Confirm `libmc_streaming.a` is on the native link line.
4. Confirm hardware encoder is H.264 Annex-B, not HEVC.
5. Review Known Issues and the EULA before a store build.
