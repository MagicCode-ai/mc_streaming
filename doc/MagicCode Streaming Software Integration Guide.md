# MagicCode Streaming Software Integration Guide

**Document Version:** v2.0.0. **Release Date:** November 1, 2026.

## 1. Libraries

| Platform | Archive | Header |
|----------|---------|--------|
| Android arm64-v8a | `lib/android/arm64-v8a/libmc_streaming.a` or `libmc_streaming.so`; H.265 `libmcs_codec_h265.so` | `include/mc_streaming.h` |
| iOS / iPadOS arm64 | `lib/ios/libmc_streaming.a` or `libmc_streaming.dylib`; H.265 `mcs_codec_h265.framework` | `include/mc_streaming.h` |
| macOS arm64 | `lib/macos/libmc_streaming.a` or `libmc_streaming.dylib`; H.265 `libmcs_codec_h265.dylib` | `include/mc_streaming.h` |

Also add `include/` (for `mcs_err.h`) to the include path. Do not add `src/` to a shipping app unless you are debugging internals.

The archive is a static library of many objects. **Whole-archive / force-load is required**, or the linker will drop unreferenced encode/parse objects and you will get missing symbols at runtime or link time.

## 2. Android (NDK / CMake)

```cmake
set(MAGIC_STREAMING_ROOT "/path/to/magic_steaming")
add_library(magic_streaming STATIC IMPORTED)
set_target_properties(magic_streaming PROPERTIES
    IMPORTED_LOCATION
    ${MAGIC_STREAMING_ROOT}/lib/android/arm64-v8a/libmc_streaming.a)

target_include_directories(your_jni PRIVATE ${MAGIC_STREAMING_ROOT}/include)

target_link_libraries(your_jni
    -Wl,--whole-archive
    magic_streaming
    -Wl,--no-whole-archive
    log
    z
    m
    android
)
```

Constraints:

- ABI: **arm64-v8a only** for the published `.a`.
- `minSdk` 24 or higher.
- Call `mc_streaming_enable` from the encoder callback thread that owns the AU and I420 (typically after MediaCodec / WebRTC `encode`).
- JNI must keep direct `ByteBuffer` addresses valid for the duration of the call.

Typical placement: wrap the hardware H.264 encoder, after `EncodedImage` is produced, before RTP packetization.

## 3. iOS / iPadOS (Xcode)

Header search path:

```
$(inherited) /path/to/magic_steaming/include
```

Other linker flags (force-load is required):

```
-force_load /path/to/magic_steaming/lib/ios/libmc_streaming.a
-lc++
-framework CoreFoundation
-framework CoreVideo
-framework CoreMedia
```

If your app already force-loads other C++ static libraries, keep a single `-lc++`.

Constraints:

- Device **arm64** only. Do not link this archive into an iOS Simulator target.
- Deployment target 13.0+.
- Typical placement: wrap `RTCVideoEncoder` / VideoToolbox after the encoded sample is ready.

## 4. Encoder integration rules

1. Produce Annex-B (start codes), not AVCC/HVCC length prefixes. For H.265, ship and load `mcs_codec_h265` before `MCS_CMD_SET_PARAMS` with codec 2 or 3. There is no static H.265 archive.
2. Pass **one AU per call** (all NALs of one picture, including non-VCL that belong with it).
3. `frame_type` must match I/IDR vs P/B.
4. I420 must be the same frame the encoder just compressed (display order).
5. Output capacity ≥ input AU size (I/IDR is copied 1:1).
6. Reuse the same `handle` across frames. Create once; disable on encoder release or resolution reset that you cannot express as a key reinit.

Recommended codec for real-time calls: `MCS_H264_ZERO_DELAY_8BIT`. Use `MCS_H264_8BIT` when you want residual processing on P and B Inter (higher CPU).

## 5. Logging

```c
mc_streaming_set_log_level(MCS_LOG_ERROR); /* default */
```

Android: logcat tag `MagicStreaming`. Host/iOS: stderr via the log backend.

## 6. License packaging

The Software is licensed under the MagicCode Streaming Software End User License Agreement (EULA). Before shipping:

- Keep copyright, trademark, and proprietary notices intact.
- Confirm your distribution scope matches the EULA (personal or commercial authorization as applicable).

See the EULA, Privacy Policy, and Data Reporting Notice (includes a store / SDK disclosure table).
