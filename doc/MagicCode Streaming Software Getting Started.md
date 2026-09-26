# MagicCode Streaming Software Getting Started

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

## 1. What you integrate

| Item | Path |
|------|------|
| Header | `include/mc_streaming.h` (also needs `include/mcs_err.h`) |
| Android library | `lib/android/arm64-v8a/libMagicCode_streaming.a` |
| iOS library | `lib/ios/libMagicCode_streaming.a` |
| Version | `mc_streaming_get_version()` → `"1.0.0"` |

The library sits **after** your H.264 encoder. Each encoded Annex-B AU plus the matching source frame (I420, NV12, or NV21) is passed in; a rewritten Annex-B AU comes out.

## 2. Call sequence

1. Keep `void *handle = NULL` for the session.
2. Optional: `mc_streaming_control(&handle, MCS_CMD_SET_PARAMS, &ctrl, NULL)` if you need a codec, colorspace, or log level other than the defaults. `*handle == NULL` allocates.
3. For every encoded picture:
   - Fill `mc_streaming_input_t` (YUV pointers/strides matching session `pic_csp`, `width`/`height`, `frame_type`, `bs`, `au_size`).
   - Fill `mc_streaming_output_t` so `bs` is a write buffer and `bs_size` is the capacity. `input.bs` and `output.bs` may be the same pointer.
   - Call `mc_streaming_enable(&handle, &input, &output)`.
   - Use `output.bs_size` bytes at `output.bs[0]` as the AU to send. `0` means drop / nothing emitted.
4. On teardown: `mc_streaming_disable(handle); handle = NULL;`.

`mc_streaming_enable` allocates on first use when `*handle == NULL` (defaults: 1920×1088, `MCS_H264_ZERO_DELAY_8BIT`, I420, `MCS_LOG_ERROR`).

## 3. Minimal C sketch

```c
#include "mc_streaming.h"

void *handle = NULL;
mc_streaming_input_t in = {0};
mc_streaming_output_t out = {0};

in.width = w;
in.height = h;
in.y = y; in.u = u; in.v = v;
in.stride_y = stride_y;
in.stride_u = stride_u;
in.stride_v = stride_v;
in.frame_type = is_key ? 1 : 0;
in.bs = annexb;
in.au_size = au_len;

out.bs = out_annexb;      /* write buffer; may equal in.bs */
out.bs_size = capacity;   /* in: capacity; out: bytes written */

int rc = mc_streaming_enable(&handle, &in, &out);
/* rc == MCS_OK: processed. Negative MCS_ERR_* may still write a legal AU
 * if out.bs_size > 0 (bypass copy). */
(void)rc;

/* ... later ... */
mc_streaming_disable(handle);
```

Optional: `mc_streaming_control(&handle, MCS_CMD_SET_PARAMS, &ctrl, NULL)` with `log_level = MCS_LOG_INFO` before the first enable. Default is `MCS_LOG_ERROR`.

## 4. Input contracts (must get these right)

- **I420, NV12, or NV21** (session `pic_csp`, default I420), same display size as the encoder input. Width/height must match coded or display size of the AU (`MCS_ERR_CROP` otherwise). For NV12/NV21, `u` is the interleaved chroma plane and `v` is unused.
- **`frame_type`**: `1` for I/IDR, `0` for delta. Must match whether the AU is I/IDR (`MCS_ERR_KEY`).
- **`bs` / `au_size`**: Annex-B AU pointer and length. Length must be > 0.
- Output buffer must be large enough for a copied AU (I/IDR and bypass paths copy the input NAL). `input.bs` and `output.bs` may alias.

## 5. Pairing the bitstream with YUV

Each `mc_streaming_enable` call takes one Annex-B access unit and the source picture that was encoded into that access unit. The picture is in **display order**. The access unit may arrive in **decode order**. Those two orders are the same only when the encoder emits no B-frames.

### 5.1 RTC

Real-time calls use `MCS_H264_ZERO_DELAY_8BIT` (the default) and an encoder with no B-frames. Capture order, display order, and packet order are then the same sequence. Pair each AU with the source picture by the **RTP timestamp** you handed to the encoder. Call `mc_streaming_enable` after encode and before RTP packetization.

The video RTP clock is 90 kHz. The timestamp is a `uint32_t` and wraps. From a capture time in microseconds:

```c
uint32_t rtp_ts = (uint32_t)(capture_time_us * 90 / 1000);
```

`capture_time_ms * 90` is the same value. WebRTC already stores it on the frame:

| API | Where to read it |
|-----|------------------|
| WebRTC `VideoFrame` (input of `Encode`) | `frame.rtp_timestamp()` |
| WebRTC `EncodedImage` (output) | `encoded_image.RtpTimestamp()` — set this from the input frame when you build the image |
| Android `MediaCodec` | `BufferInfo.presentationTimeUs`, the same value passed to `queueInputBuffer` |
| iOS VideoToolbox | `CMSampleBufferGetPresentationTimeStamp`. Scale that `CMTime` to timescale 90000 |

`MediaCodec` and `VTCompressionSession` echo the presentation time you submitted. Use that echoed value as the lookup key. Convert it to an RTP timestamp only when the time base is microseconds (`presentationTimeUs * 90 / 1000`).

Hold the YUV when you submit the frame. Take it back when the AU arrives. If the camera reuses its buffer before the AU returns, copy the planes inside `hold_frame`.

```c
enum { kHeldCap = 16 };

typedef struct held_frame {
    int used;
    uint32_t rtp_ts;
    const uint8_t *y, *u, *v;
    int stride_y, stride_u, stride_v;
} held_frame;

static held_frame g_held[kHeldCap];

int hold_frame(uint32_t rtp_ts,
               const uint8_t *y, const uint8_t *u, const uint8_t *v,
               int stride_y, int stride_u, int stride_v)
{
    int i;
    for (i = 0; i < kHeldCap; i++) {
        if (g_held[i].used)
            continue;
        g_held[i].used = 1;
        g_held[i].rtp_ts = rtp_ts;
        g_held[i].y = y;
        g_held[i].u = u;
        g_held[i].v = v;
        g_held[i].stride_y = stride_y;
        g_held[i].stride_u = stride_u;
        g_held[i].stride_v = stride_v;
        return 0;
    }
    return -1; /* queue full: encoder delay is longer than kHeldCap */
}

held_frame *take_frame_with_timestamp(uint32_t rtp_ts)
{
    int i;
    for (i = 0; i < kHeldCap; i++) {
        if (g_held[i].used && g_held[i].rtp_ts == rtp_ts)
            return &g_held[i];
    }
    return NULL;
}

void release_frame(held_frame *f)
{
    if (f)
        f->used = 0;
}
```

Compare the timestamp with `==`. It is a `uint32_t`; a less-than test breaks across the wrap.

```c
/* Encode() entry. rtp_ts = frame.rtp_timestamp() or the formula above. */
hold_frame(rtp_ts, y, u, v, stride_y, stride_u, stride_v);
encoder_submit(rtp_ts, y, u, v);

/* Encoder callback. rtp_ts = EncodedImage.RtpTimestamp()
 * or BufferInfo.presentationTimeUs, using the same key you stored. */
held_frame *src = take_frame_with_timestamp(rtp_ts);
in.y = src->y; in.u = src->u; in.v = src->v;
in.stride_y = src->stride_y;
in.stride_u = src->stride_u;
in.stride_v = src->stride_v;
in.frame_type = encoded->is_key ? 1 : 0;
in.bs = encoded->annexb;
in.au_size = encoded->annexb_size;
mc_streaming_enable(&handle, &in, &out);
release_frame(src);
/* send out.bs[0 .. out.bs_size) */
```

With B-frames, set `MCS_H264_8BIT` and keep this timestamp lookup. The RTP timestamp is the display time of that picture, so it still selects the YUV that belongs to the AU.

### 5.2 FFmpeg

Encode first, then run the `mc_streaming` bitstream filter with `-c copy`. `yuv=` is the same display-order I420 file that was encoded (frame 0, frame 1, frame 2, …). The filter reads POC from each access unit, turns it into a display index, and reads that frame from the file.

No B-frames (`-tune zerolatency`): packet i is frame i, so the file is read straight through. Use `mcs_codec=zero_delay`.

```
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 -i input.yuv -an -c:v libx264 -tune zerolatency out_bs.264
ffmpeg -i out_bs.264 -c copy -bsf:v "mc_streaming=yuv=input.yuv:mcs_codec=zero_delay" out.264
```

With B-frames, packets arrive in decode order. A display-index sequence of `0, 3, 1, 2, 6, 4, 5` means the filter reads YUV frame 0, then frame 3, then frame 1, then frame 2, and so on, each time with the access unit just read. Use `mcs_codec=h264_8bit`.

| Packet order | Picture | Display index | YUV frame read |
|--------------|---------|---------------|----------------|
| 1 | I | 0 | 0 |
| 2 | P | 3 | 3 |
| 3 | B | 1 | 1 |
| 4 | B | 2 | 2 |
| 5 | P | 6 | 6 |
| 6 | B | 4 | 4 |
| 7 | B | 5 | 5 |

```
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 -i input.yuv -an -c:v libx264 -bf 2 out_bs.264
ffmpeg -i out_bs.264 -c copy -bsf:v "mc_streaming=yuv=input.yuv:mcs_codec=h264_8bit" out.264
```

`-c:v` belongs before the output name. For a raw `.264` input, add `:width=` and `:height=` when the demuxer does not fill the picture size. The file must be packed I420 in display order, one frame after another, the same frames the encoder consumed.

### 5.3 Reading POC from the access unit

The FFmpeg filter turns POC into the display index in the table above. `demo/ffmpeg/mc_streaming.c` (`slice_display`) is that parser. Use it when you index a display-order YUV file yourself. An RTC caller that already has the RTP timestamp does not need POC.

x264 writes `pic_order_cnt_type = 0`. Read two fields from the SPS, then `pic_order_cnt_lsb` from the first VCL slice (NAL type 1, or type 5 for IDR):

- SPS: `log2_max_frame_num = log2_max_frame_num_minus4 + 4`, `log2_max_poc_lsb = log2_max_pic_order_cnt_lsb_minus4 + 4`.
- Slice header, after removing `00 00 03` emulation-prevention bytes: `first_mb_in_slice` (ue), `slice_type` (ue), `pic_parameter_set_id` (ue), `frame_num` (`log2_max_frame_num` bits). For a frame picture (`frame_mbs_only_flag = 1`) the next fields are `idr_pic_id` (ue, IDR only) and then `pic_order_cnt_lsb` (`log2_max_poc_lsb` bits).
- NAL header byte: `nal_ref_idc = (nal[0] >> 5) & 3`, `nal_unit_type = nal[0] & 0x1f`. IDR is type 5.

Full POC is `poc_msb + poc_lsb`. MSB follows the previous **reference** picture (`nal_ref_idc != 0`). An IDR resets that previous value to 0 before the add. Display index then matches the library: POC steps of 2 become a frame index (`div = 2`); a stream whose `poc_lsb` is odd uses `div = 1` from that picture on.

```c
typedef struct poc_state {
    int prev_poc_msb, prev_poc_lsb;
    int poc_lsb_odd, poc_base;
    int display_epoch, max_display_order, coded_order;
} poc_state;

/* log2_max_poc_lsb = log2_max_pic_order_cnt_lsb_minus4 + 4 from the SPS.
 * poc_lsb is pic_order_cnt_lsb from the first VCL slice.
 * Returns the display-order YUV index for this access unit. */
int display_index_from_poc(poc_state *s, int poc_lsb, int log2_max_poc_lsb,
                           int idr, int nal_ref_idc)
{
    int max_poc_lsb = 1 << log2_max_poc_lsb;
    int prev_msb = idr ? 0 : s->prev_poc_msb;
    int prev_lsb = idr ? 0 : s->prev_poc_lsb;
    int poc_msb, poc, div, delta, disp;

    if (poc_lsb < prev_lsb && prev_lsb - poc_lsb >= max_poc_lsb / 2)
        poc_msb = prev_msb + max_poc_lsb;
    else if (poc_lsb > prev_lsb && prev_lsb - poc_lsb < -max_poc_lsb / 2)
        poc_msb = prev_msb - max_poc_lsb;
    else
        poc_msb = prev_msb;
    poc = poc_msb + poc_lsb;
    if (nal_ref_idc) {
        s->prev_poc_msb = poc_msb;
        s->prev_poc_lsb = poc_lsb;
    }

    if (poc_lsb & 1)
        s->poc_lsb_odd = 1;
    div = s->poc_lsb_odd ? 1 : 2;
    if (idr || s->coded_order == 0) {
        s->poc_base = poc;
        if (idr && s->coded_order > 0)
            s->display_epoch = s->max_display_order + 1;
    }
    delta = poc - s->poc_base;
    if (delta < 0)
        delta = 0;
    disp = s->display_epoch + delta / div;
    if (disp > s->max_display_order)
        s->max_display_order = disp;
    s->coded_order++;
    return disp;
}
```

Zero `poc_state` before the first AU. For the B-frame example, the decode-order POC values 0, 6, 2, 4, 12, 8, 10 produce display indexes 0, 3, 1, 2, 6, 4, 5 when `div` is 2.

## 6. Next documents

- Integration Guide: Android `--whole-archive` / iOS `-force_load` and system libraries.
- API Reference: structs, enums, return codes.
- Specification: bitstream and session limits.
- EULA: license terms for use and distribution.
