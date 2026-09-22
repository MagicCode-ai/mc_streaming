# MagicCode Streaming Software Migration Guide

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

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
2. Link `libMagicCode_streaming.a` with whole-archive / force-load.
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

- [ ] `mc_streaming_get_version()` returns `1.0.0`.
- [ ] I/IDR output matches input AU bytes (copy).
- [ ] P frames can differ when residual processing is active.
- [ ] Wrong `frame_type` is rejected or copied per API rules.
- [ ] Resolution change on a key reinitializes; on a delta returns `MCS_ERR_SIZE`.
- [ ] Android `--whole-archive` and iOS `-force_load` are set.
- [ ] EULA reviewed for the shipping package.
- [ ] Known Issues reviewed.
