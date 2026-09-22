# MagicCode Streaming Software API Reference

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

Header: `include/mc_streaming.h`. Error codes: `include/mcs_err.h`. C++ projects include the header normally; symbols use `extern "C"`.

## 1. Overview

The native API is a C interface for session create-or-process, release, log-level, and version query. There is no separate Init/Process pair: `mc_streaming_enable` both allocates (when `*handle == NULL`) and processes one AU.

## 2. Core workflow

1. Zero-init `mc_streaming_input_t` and set `struct_size`.
2. Fill I420 planes, `frame_type`, and `au_size`.
3. Point `mc_streaming_output_t.bs` at the Annex-B AU; set `bs_size` to capacity.
4. Call `mc_streaming_enable(&handle, codec_type, &input, &output)`.
5. Read `output.bs_size`.
6. Call `mc_streaming_disable(handle)` once when the session ends. The caller then sets their pointer to NULL.

## 3. Enumerations

### 3.1 `mc_streaming_log_level_e`

Higher value is more verbose. A message prints when the current level is ≥ the message level.

| Value | Enumerator | Notes |
|-------|------------|-------|
| 0 | `MCS_LOG_NONE` | Silence all |
| 1 | `MCS_LOG_ERROR` | Default |
| 2 | `MCS_LOG_WARNING` | |
| 3 | `MCS_LOG_INFO` | |
| 4 | `MCS_LOG_DEBUG` | |

### 3.2 `mc_streaming_codec_type_e`

| Value | Enumerator | Notes |
|-------|------------|-------|
| 0 | `MCS_H264_ZERO_DELAY_8BIT` | Recommended for real-time calls |
| 1 | `MCS_H264_8BIT` | Processes P and B; higher CPU |
| 2 | `MCS_MAX_CODEC` | Sentinel, not a valid codec |

## 4. Structures

### 4.1 `mc_streaming_input_t`

| Field | Meaning |
|-------|---------|
| `struct_size` | Set to `sizeof(mc_streaming_input_t)`. |
| `width`, `height` | I420 display size. Must match coded or display size of the AU. |
| `y`, `u`, `v` | Plane pointers. I420, matching typical `VideoFrame.buffer.toI420()`. |
| `stride_y`, `stride_u`, `stride_v` | Byte strides. |
| `frame_type` | `0` = delta, `1` = key. Must match whether the AU is I/IDR. |
| `au_size` | Annex-B AU length at `output.bs[0]`. `0` = legacy: trim trailing zeros from `output.bs_size`. Prefer a real length. |

### 4.2 `mc_streaming_output_t`

| Field | Meaning |
|-------|---------|
| `bs` | EncodedImage buffer. On entry holds the Annex-B AU at offset 0. |
| `bs_size` | In: capacity. Out: bytes written. `0` = drop / nothing emitted. |

Unused tail need not be zero if `input.au_size` is the AU length.

## 5. Functions

| Function | Purpose | Return |
|----------|---------|--------|
| `int mc_streaming_enable(void **handle, mc_streaming_codec_type_e codec_type, mc_streaming_input_t *input, mc_streaming_output_t *output)` | Init-or-process one AU | `MCS_OK` or negative `MCS_ERR_*` |
| `int mc_streaming_disable(void *handle)` | Free session | `MCS_OK` or negative |
| `void mc_streaming_set_log_level(mc_streaming_log_level_e level)` | Process-wide log threshold | void; clamps to NONE..DEBUG |
| `const char *mc_streaming_get_version(void)` | Version string | Static `"1.0.0"`. Do not free. |

### 5.1 `mc_streaming_enable`

- `*handle == NULL` allocates a session (caps 1920×1088 / 2 refs / 1 B-frame).
- Session grows on a **key** reinit when coded size, refs, or B-frames exceed caps.
- Returns `MCS_OK` when the AU was processed.
- A negative `MCS_ERR_*` can still mean a legal Annex-B AU was written if `output->bs_size > 0` (bypass copy).
- `MCS_ERR_INVAL` / `MCS_ERR_OUTPUT` with `bs_size == 0`: nothing emitted.

### 5.2 Threading

The handle is not internally synchronized. One session, one thread. Do not call enable/disable concurrently on the same handle.

## 6. Return codes (`mcs_err.h`)

| Code | Value | Meaning |
|------|-------|---------|
| `MCS_OK` | 0 | Success |
| `MCS_ERR_INVAL` | -1 | Bad arguments; nothing emitted |
| `MCS_ERR_NOMEM` | -2 | Allocation failed |
| `MCS_ERR_DECODE` | -3 | Bitstream parse or decode failed |
| `MCS_ERR_ENCODE` | -4 | Processing failed |
| `MCS_ERR_REWRITE` | -5 | Output bitstream rewrite failed |
| `MCS_ERR_OUTPUT` | -6 | Output buffer too small or emit failed |
| `MCS_ERR_SIZE` | -7 | Resolution or stream-structure change not allowed |
| `MCS_ERR_KEY` | -8 | `frame_type` does not match I/IDR |
| `MCS_ERR_CROP` | -9 | YUV width/height does not match coded or display size |
| `MCS_ERR_BYPASS` | -10 | Still copying input NALs until the next I or IDR |

The public session API treats `MCS_OK` as success for `mc_streaming_enable`.

Lower-case aliases (`mcs_ok`, `mcs_err_inval`, …) are provided for compatibility.
