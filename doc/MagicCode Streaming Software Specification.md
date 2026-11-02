# MagicCode Streaming Software Specification

**Document Version:** v2.0.0. **Release Date:** November 1, 2026.

## 1. Document Overview

This specification defines capabilities, platforms, and functional boundaries of MagicCode Streaming Software v2.0.0. It covers the public session API only. CLI tools (`magic_streaming`, `mcs_file_process`) are developer utilities, not the shipping API.

### 1.1 Document Status

| Version | Date | Status | Change |
|---------|------|--------|--------|
| v1.0.0 | September 19, 2026 | Initial | First public SDK. H.264 only. |
| v2.0.0 | November 1, 2026 | Current | H.265 plugin, `mc_streaming_control`, `dec_pic_reenc`, JND fields, NV12/NV21, macOS arm64. |

## 2. System Requirements

| Platform | CPU | GPU | Notes |
|----------|-----|-----|-------|
| Android 8.0+ | ARM64 | Not used | arm64-v8a static and shared `libmc_streaming`, plus `libmcs_codec_h265.so`. |
| iOS / iPadOS 13.0+ | ARM64 | Not used | Device archive only. Simulator is not a target. H.265: `mcs_codec_h265.framework`. |
| macOS 11.0+ | ARM64 | Not used | `libmc_streaming` plus `libmcs_codec_h265.dylib`. |

Host builds exist for tests and are not a published mobile archive.

## 3. Functional Capabilities

The software processes already-encoded H.264 or H.265 Annex-B access units together with matching source frames (I420, NV12, or NV21). It is not a camera encoder and not a super-resolution library. H.265 is served only when the codec plugin is registered.

### 3.1 Picture types

| Picture | Public behavior |
|---------|-----------------|
| I / IDR | Copy the input AU. |
| P / B | Process the AU using the selected codec profile. |

### 3.2 Codec profiles

| Value | Enumerator | Typical use |
|-------|------------|-------------|
| 0 | `MCS_H264_ZERO_DELAY_8BIT` | Real-time H.264. Processes P frames. Default. |
| 1 | `MCS_H264_8BIT` | H.264 P and B; higher CPU. |
| 2 | `MCS_H265_ZERO_DELAY_8BIT` | Real-time H.265. Requires the plugin. |
| 3 | `MCS_H265_8BIT` | H.265 P and B; higher CPU. Requires the plugin. |

`jnd_enable` 0 skips JND. `dec_pic_reenc` 0 is syntax rewrite (default). `dec_pic_reenc` 1 decodes the AU to display YUV and re-encodes the residual. Not calling `mc_streaming` is streaming off. Zeroing `mc_streaming_ctrl_params_t` keeps the H.264 default.

### 3.3 Bitstream support

| Feature | Support |
|---------|---------|
| 8-bit progressive H.264 Baseline / Main / High | Yes |
| 8-bit 4:2:0 H.265/HEVC (plugin) | Yes |
| Interlaced / 10-bit | No |
| AV1 / VP9 | No |

Typical production input is hardware H.264 (MediaCodec / VideoToolbox) Annex-B.

### 3.4 Session caps

On first `mc_streaming_enable` with a NULL handle:

- Max coded area: 1920×1088
- Max reference frames: 2
- Max B-frames: 1

If a later **key** picture exceeds these, the session reinitializes with larger caps. A size change on a **delta** picture is `MCS_ERR_SIZE` (the AU may still be copied if `bs_size > 0`).

### 3.5 Failure and bypass

If a picture cannot be processed, the library may emit the **input** AU and then copy subsequent pictures until the next I/IDR (`MCS_ERR_BYPASS`). That is the only production copy path besides I/IDR.

A negative return with `output->bs_size > 0` still means a legal Annex-B AU was written.

## 4. Resource contract

- Caller owns I420 planes and the Annex-B buffer.
- The library owns session state. `mc_streaming_disable` frees it.
- No GPU context. No model files.
- Default log level `MCS_LOG_ERROR`. Logs go to stderr (host) or Android logcat tag `MagicStreaming`.

## 5. Conformance notes

API support is not the same as device-run verification. Hardware encoder GOP, bitrate, and Annex-B layout must match the input contract. Do not claim every vendor encoder or every resolution is verified.
