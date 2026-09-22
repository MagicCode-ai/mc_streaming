# MagicCode Streaming Software Release Notes

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

## 1. Summary

MagicCode Streaming Software v1.0.0 is the first public SDK for H.264 Annex-B residual processing. After a hardware or software encoder produces an Access Unit (AU), the library processes that AU together with the matching I420 source frame. I/IDR AUs are copied. The public surface is `include/mc_streaming.h` and `libMagicCode_streaming.a`.

## 2. Key Changes

- Public version string is `1.0.0` (`mc_streaming_get_version`).
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
| Android 8.0+ (API 24+) | `lib/android/arm64-v8a/libMagicCode_streaming.a` | arm64-v8a |
| iOS / iPadOS 13.0+ | `lib/ios/libMagicCode_streaming.a` | arm64 device (not Simulator) |

Operating systems verified in this drop: Android 16 (Samsung SM-S9110), iPadOS 26, iOS 27 (iPhone 14 Pro). API support is not a claim that every device or encoder is device-run.

Bitstream: 8-bit progressive H.264 Baseline / Main / High. Typical production input is hardware H.264 (MediaCodec / VideoToolbox) Annex-B.

Session caps start at 1920×1088 coded area, 2 reference frames, and 1 B-frame, and grow on a **key** reinit when the stream exceeds those caps.

## 4. Codec profiles

| Value | Enumerator | Typical use |
|-------|------------|-------------|
| 0 | `MCS_H264_ZERO_DELAY_8BIT` | Real-time calls |
| 1 | `MCS_H264_8BIT` | P and B processing; higher CPU |

## 5. Migration Notes

This is the first public SDK. Integrators moving from the CLI `magic_streaming` should follow the Migration Guide. Public symbols are `mc_streaming_*` only.

Use of the Software is governed by the MagicCode Streaming Software End User License Agreement (EULA). Review the EULA before distributing an application that links the SDK.

## 6. Known Limitations

See Known Issues. Highlights: interlaced and 10-bit are not supported; I/IDR are copied; Android Simulator and iOS Simulator archives are not provided.

## 7. Validation Snapshot

API contract: `include/mc_streaming.h`, `include/mcs_err.h`. Device path: video-call Release builds on SM-S9110, iPad, and iPhone 14 Pro linking `libMagicCode_streaming.a`.
