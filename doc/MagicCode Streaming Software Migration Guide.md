# MagicCode Streaming Software Migration Guide

**Document Version:** v2.0.0. **Release Date:** November 1, 2026.

## 1. Purpose

This is the first public SDK. The guide covers moving from the CLI to `mc_streaming_enable`.

## 2. Main changes

| Area | Before (CLI) | v1.0.0 public SDK | Action |
|------|--------------|-------------------|--------|
| Entry | `magic_streaming` CLI | `mc_streaming_enable` | Call per encoded AU |
| Session | File in / file out | Hidden. Session handle is `void *` | Do not call internal headers from apps |
| Version | None | `mc_streaming_get_version` → `1.0.0` | Query at runtime |
| Logs | stderr | `mc_streaming_set_log_level` | Default ERROR |
| I/IDR | Various | Copy AU | Do not expect I-frame residual rewrite |

## 3. Native code migration

1. Include **only** `mc_streaming.h` (and `mcs_err.h` via that header).
2. Link `libmc_streaming.a` with whole-archive / force-load. The v1.0.0 name `libMagicCode_streaming.a` is retired.
3. Replace file-based CLI invocation with in-memory AU + I420.
4. Set `input.struct_size` and `input.au_size`.
5. Treat `output.bs_size` as the AU to packetize even when the return code is negative, if `bs_size > 0`.
6. Call `mc_streaming_disable` when the encoder is released.

## 4. Flutter / WebRTC sample

The in-tree video-call app wraps hardware H.264:

- Android: JNI `magic_streaming_jni.cpp` after MediaCodec.
- iOS: `MagicStreamingEncoder` after VideoToolbox.

Those wrappers are samples, not part of the SDK archive. Copy the call pattern, not the app package name.

## 5. Acceptance checklist

- [ ] `mc_streaming_get_version()` returns `2.0.0`.
- [ ] I/IDR output matches input AU bytes (copy).
- [ ] P frames can differ when residual processing is active.
- [ ] Wrong `frame_type` is rejected or copied per API rules.
- [ ] Resolution change on a key reinitializes; on a delta returns `MCS_ERR_SIZE`.
- [ ] Android `--whole-archive` and iOS `-force_load` are set.
- [ ] EULA reviewed for the shipping package.
- [ ] Known Issues reviewed.

## 6. v1.0.0 → v2.0.0

| Area | v1.0.0 | v2.0.0 | Action |
|------|--------|--------|--------|
| Version | `1.0.0` | `2.0.0` | Query `mc_streaming_get_version` |
| Archive | `libMagicCode_streaming.a` | `libmc_streaming.a` / `.so` / `.dylib` | Relink |
| H.265 | Not supported | Plugin `mcs_codec_h265` | Load the plugin before codec 2 or 3. No static H.265 `.a`. |
| Control | Defaults only | `mc_streaming_control` | Optional. A zeroed `mc_streaming_ctrl_params_t` stays H.264, I420, `dec_pic_reenc` 0. |
| Colorspace | I420 | I420, NV12, NV21 | Set `pic_csp` if not I420. |
