# MagicCode Streaming Software Release Notes

**Document Version:** v2.0.0. **Release Date:** November 1, 2026.

## 1. Summary

MagicCode Streaming Software v2.0.0 processes H.264 and H.265 Annex-B access units after a hardware or software encoder. I/IDR pictures are copied. P pictures (and B pictures on the non-zero-delay profiles) are rewritten. H.264 is builtin in `libmc_streaming`. H.265 is the shared plugin `mcs_codec_h265` (no separate static H.265 archive). The public surface is `include/mc_streaming.h`. `mc_streaming_get_version()` returns `2.0.0`.

v1.0.0 (September 19, 2026) was the first public SDK and covered H.264 only.

## 2. Key Changes

- Public version string is `2.0.0` (`mc_streaming_get_version`).
- Codec profiles: `MCS_H264_ZERO_DELAY_8BIT` (0), `MCS_H264_8BIT` (1), `MCS_H265_ZERO_DELAY_8BIT` (2), `MCS_H265_8BIT` (3). H.265 returns `MCS_ERR_CODEC_UNAVAILABLE` until the plugin is registered.
- `mc_streaming_control` with `MCS_CMD_SET_PARAMS` / `MCS_CMD_GET_STATUS`. New fields on `mc_streaming_ctrl_params_t` are appended: `jnd_enable`, `skin_skip`, `jnd_strength_level`, `jnd_scale`, `dec_pic_reenc`. A zeroed struct stays H.264, I420, JND off, syntax rewrite (`dec_pic_reenc` 0).
- `dec_pic_reenc` 1 decodes the AU to display YUV (H.264 deblock, H.265 deblock+SAO) and re-encodes the residual from that picture. Not calling the library at all is streaming off. `jnd_enable` 0 only skips JND.
- Colorspace: I420, NV12, NV21.
- Archive names are `libmc_streaming.a` / `.so` / `.dylib`, plus the H.265 plugin. The old `libMagicCode_streaming.a` name is retired.
- Platforms in this drop: Android 8+ arm64-v8a, iOS/iPadOS 13+ device arm64, macOS 11+ arm64.

Historical v1.0.0 notes:

- Public version string was `1.0.0` (`mc_streaming_get_version`).
- Session API: `mc_streaming_enable` / `mc_streaming_disable`.
- Two public codec profiles: `MCS_H264_ZERO_DELAY_8BIT` (0) and `MCS_H264_8BIT` (1).
- Process-wide log threshold: `mc_streaming_set_log_level`. Default `MCS_LOG_ERROR`.
- Technical telemetry (TCP client): report time, app-instance UUID, device model, SDK version, average per-frame time (ms), average input/output bitstream size, error code, report count. Used for billing / license metering and issue diagnosis. Device IP is not included in the packet. Standard SDK: always on as a license condition; a non-reporting archive requires written authorization.
- I/IDR: copy the input AU.
- P/B: process the AU; residual is not copied from the input NAL.
- Android arm64-v8a and iOS/iPadOS device arm64 archives.

## 3. Compatibility

| Platform | Archive | ABI |
|----------|---------|-----|
| Android 8.0+ (API 24+) | `lib/android/arm64-v8a/libmc_streaming.a`, `libmc_streaming.so`, H.265 `libmcs_codec_h265.so` | arm64-v8a |
| iOS / iPadOS 13.0+ | `lib/ios/libmc_streaming.a`, `libmc_streaming.dylib`, H.265 `mcs_codec_h265.framework` | arm64 device (not Simulator) |
| macOS 11.0+ | `lib/macos/libmc_streaming.a`, `libmc_streaming.dylib`, H.265 `libmcs_codec_h265.dylib` | arm64 |

Operating systems verified in this drop: Android 16 (Samsung SM-S9110), iPadOS 26, iOS 27 (iPhone 14 Pro). API support is not a claim that every device or encoder is device-run.

Bitstream: 8-bit progressive H.264 Baseline / Main / High, and 8-bit 4:2:0 H.265/HEVC via the plugin. Typical production input is hardware Annex-B (MediaCodec / VideoToolbox).

Session caps start at 1920×1088 coded area, 2 reference frames, and 1 B-frame, and grow on a **key** reinit when the stream exceeds those caps.

## 4. Codec profiles

| Value | Enumerator | Typical use |
|-------|------------|-------------|
| 0 | `MCS_H264_ZERO_DELAY_8BIT` | Real-time H.264 |
| 1 | `MCS_H264_8BIT` | H.264 P and B; higher CPU |
| 2 | `MCS_H265_ZERO_DELAY_8BIT` | Real-time H.265 (plugin) |
| 3 | `MCS_H265_8BIT` | H.265 P and B; higher CPU |

## 5. Migration Notes

This is the first public SDK. Integrators moving from the CLI `magic_streaming` should follow the Migration Guide. Public symbols are `mc_streaming_*` only.

Use of the Software is governed by the MagicCode Streaming Software End User License Agreement (EULA). Review the EULA before distributing an application that links the SDK.

## 6. Known Limitations

See Known Issues. Highlights: interlaced and 10-bit are not supported; I/IDR are copied; Android Simulator and iOS Simulator archives are not provided.

## 7. Validation Snapshot

API contract: `include/mc_streaming.h`, `include/mcs_err.h`.
