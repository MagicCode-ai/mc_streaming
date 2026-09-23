# MagicCode Streaming Software Getting Started

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

## 1. What you integrate

| Item | Path |
|------|------|
| Header | `include/mc_streaming.h` (also needs `include/mcs_err.h`) |
| Android library | `lib/android/arm64-v8a/libMagicCode_streaming.a` |
| iOS library | `lib/ios/libMagicCode_streaming.a` |
| Version | `mc_streaming_get_version()` → `"1.0.0"` |

The library sits **after** your H.264 encoder. Each encoded Annex-B AU plus the matching I420 camera/source frame is passed in; a rewritten Annex-B AU comes out.

## 2. Call sequence

1. Keep `void *handle = NULL` for the session.
2. For every encoded picture:
   - Fill `mc_streaming_input_t` (`struct_size`, I420 pointers/strides, `width`/`height`, `frame_type`, `au_size`).
   - Fill `mc_streaming_output_t` so `bs` points at the Annex-B AU and `bs_size` is the buffer capacity.
   - Call `mc_streaming_enable(&handle, codec_type, &input, &output)`.
   - Use `output.bs_size` bytes at `output.bs[0]` as the AU to send. `0` means drop / nothing emitted.
3. On teardown: `mc_streaming_disable(handle); handle = NULL;`.

`mc_streaming_enable` allocates on first use when `*handle == NULL`.

## 3. Minimal C sketch

```c
#include "mc_streaming.h"

void *handle = NULL;
mc_streaming_input_t in = {0};
mc_streaming_output_t out = {0};

in.struct_size = sizeof(in);
in.width = w;
in.height = h;
in.y = y; in.u = u; in.v = v;
in.stride_y = stride_y;
in.stride_u = stride_u;
in.stride_v = stride_v;
in.frame_type = is_key ? 1 : 0;
in.au_size = au_len;

out.bs = annexb;          /* encoded AU at offset 0 */
out.bs_size = capacity;   /* in: capacity; out: bytes written */

int rc = mc_streaming_enable(&handle, MCS_H264_ZERO_DELAY_8BIT, &in, &out);
/* rc == MCS_OK: processed. Negative MCS_ERR_* may still write a legal AU
 * if out.bs_size > 0 (bypass copy). */
(void)rc;

/* ... later ... */
mc_streaming_disable(handle);
```

Optional: `mc_streaming_set_log_level(MCS_LOG_INFO);` before the first enable. Default is `MCS_LOG_ERROR`.

## 4. Input contracts (must get these right)

- **I420**, same display size as the encoder input. Width/height must match coded or display size of the AU (`MCS_ERR_CROP` otherwise).
- **`frame_type`**: `1` for I/IDR, `0` for delta. Must match whether the AU is I/IDR (`MCS_ERR_KEY`).
- **`au_size`**: Annex-B length at `output.bs[0]`. Prefer this over zero-filling the unused tail.
- Output buffer must be large enough for a copied AU (I/IDR and bypass paths copy the input NAL).

## 5. RTC: pair Annex-B with I420 by RTP timestamp

Hardware encode is asynchronous. `encode(frame)` returns before the AU is ready, and several frames can be in flight. Do **not** pair “the last YUV” with “the next EncodedImage”. Store the I420 that you feed the encoder, then look it up with the same timestamp the encoder copies onto the encoded image.

The [video-call](https://github.com/MagicCode-ai/APP/tree/main/video-call) sample wraps the WebRTC hardware H.264 encoder and uses that timestamp as the map key:

| Platform | Store I420 on `encode()` | Look up on encoded callback |
|----------|--------------------------|-----------------------------|
| iOS / iPadOS | `RTCVideoFrame.timeStamp` (90 kHz RTP timestamp) | `RTCEncodedImage.timeStamp` |
| Android | `VideoFrame.timestampNs` | `EncodedImage.captureTimeNs` |

WebRTC copies the capture/RTP clock from the input frame onto the `EncodedImage`. The iOS key is the 90 kHz RTP timestamp (`timeStamp`). Android uses nanosecond capture time (`timestampNs` / `captureTimeNs`), which is the same clock WebRTC uses to derive the RTP timestamp.

Flow:

1. In `encode()`, convert the frame to I420 (`toI420()`), retain it, and insert it into a pending map keyed by the timestamp above.
2. Bound the map (the sample keeps **8** in-flight frames). Drop the oldest I420 if the encoder stalls.
3. In the encoded callback, **remove** the I420 with that timestamp. If the lookup misses, send the original AU unchanged — do not invent a YUV or reuse another frame.
4. Call `mc_streaming_enable` with that I420 plus the Annex-B AU. Packetize `output.bs` / `output.bs_size` (GCC/TWCC then sees the reduced size).

iOS (`MagicStreamingEncoder.mm`):

```objc
/* encode() — remember I420 under the RTP timestamp */
[self rememberI420:[frame.buffer toI420] timestamp:frame.timeStamp];
return [_inner encode:frame codecSpecificInfo:info frameTypes:frameTypes];

/* encoded callback — take the matching I420, then enable */
id<RTCI420Buffer> i420 = [self takeI420:image.timeStamp];
if (i420 == nil) {
    return image; /* no pair: pass the AU through */
}
/* fill mc_streaming_input_t from i420, output.bs from image.buffer, then: */
mc_streaming_enable(&_handle, MCS_H264_ZERO_DELAY_8BIT, &in, &out);
```

Android (`MagicStreamingVideoEncoder.kt`):

```kotlin
/* encode() */
pending.put(frame.timestampNs, frame.buffer.toI420())
return inner.encode(frame, encodeInfo)

/* onEncodedFrame */
val i420 = pending.remove(image.captureTimeNs) ?: return image
/* JNI mc_streaming_enable with i420 + image.buffer */
```

Use `MCS_H264_ZERO_DELAY_8BIT` for calls. Full wrappers: [video-call](https://github.com/MagicCode-ai/APP/tree/main/video-call) (`app/ios/Runner/MagicStreamingEncoder.mm`, `app/android/app/src/main/kotlin/.../MagicStreamingVideoEncoder.kt`).

## 6. Next documents

- Integration Guide: Android `--whole-archive` / iOS `-force_load` and system libraries.
- API Reference: structs, enums, return codes.
- Specification: bitstream and session limits.
- EULA: license terms for use and distribution.
