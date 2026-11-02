# MagicCode Streaming Software API Reference

**Document Version:** v2.0.0. **Release Date:** November 1, 2026.

Header: `include/mc_streaming.h`. Error codes: `include/mcs_err.h`. C++ projects include the header normally; symbols use `extern "C"`.

## 1. Overview

The native API is a C interface for session create-or-process, control, release, and version query. There is no separate Init/Process pair: `mc_streaming_enable` both allocates (when `*handle == NULL`) and processes one AU. Codec, colorspace, JND, and `dec_pic_reenc` are set with `mc_streaming_control`.

## 2. Core workflow

1. Optional: zero `mc_streaming_ctrl_params_t` and call `mc_streaming_control(&handle, MCS_CMD_SET_PARAMS, &ctrl, NULL)`.
2. Zero-init `mc_streaming_input_t`. Fill YUV planes, `frame_type`, `bs`, and `au_size`.
3. Point `mc_streaming_output_t.bs` at a write buffer; set `bs_size` to capacity. `input.bs` and `output.bs` may alias.
4. Call `mc_streaming_enable(&handle, &input, &output)`.
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
| 0 | `MCS_H264_ZERO_DELAY_8BIT` | Recommended for real-time H.264. Default. |
| 1 | `MCS_H264_8BIT` | H.264 P and B; higher CPU |
| 2 | `MCS_H265_ZERO_DELAY_8BIT` | Real-time H.265. Plugin required. |
| 3 | `MCS_H265_8BIT` | H.265 P and B; higher CPU. Plugin required. |
| 4 | `MCS_MAX_CODEC` | Sentinel, not a valid codec |

### 3.3 `mc_streaming_csp_e`

| Value | Enumerator | Notes |
|-------|------------|-------|
| 0 | `MCS_CSP_I420` | Planar Y, U, V. Default. |
| 1 | `MCS_CSP_NV12` | Y + interleaved UV. `v` unused. |
| 2 | `MCS_CSP_NV21` | Y + interleaved VU. `v` unused. |

### 3.4 `mc_streaming_cmd_e`

| Value | Enumerator | Notes |
|-------|------------|-------|
| 0 | `MCS_CMD_SET_PARAMS` | `ctrl` required. `*handle == NULL` allocates. |
| 1 | `MCS_CMD_GET_STATUS` | `status_info` required. Does not allocate. |

## 4. Structures

### 4.1 `mc_streaming_input_t`

| Field | Meaning |
|-------|---------|
| `width`, `height` | Display size. Must match coded or display size of the AU. Each dimension in `(0, 32768]`. |
| `y`, `u`, `v` | I420 planes, or NV12/NV21 luma + interleaved chroma in `u`. |
| `stride_y`, `stride_u`, `stride_v` | Byte strides. `stride_v` unused for NV12/NV21. |
| `frame_type` | `0` = delta, `1` = key. Must match whether the AU is I/IDR. |
| `bs` | Annex-B AU at offset 0. May alias `output.bs`. |
| `au_size` | Length of that AU. |

### 4.2 `mc_streaming_output_t`

| Field | Meaning |
|-------|---------|
| `bs` | Output Annex-B buffer. May alias `input.bs`. |
| `bs_size` | In: capacity. Out: bytes written. `0` = drop / nothing emitted. |

### 4.3 `mc_streaming_ctrl_params_t`

Zero the struct before use. Appended fields default to 0, which keeps older H.264 callers bit-identical.

| Field | Meaning |
|-------|---------|
| `codec_type` | Default `MCS_H264_ZERO_DELAY_8BIT`. |
| `pic_csp` | Default `MCS_CSP_I420`. |
| `log_level` | Process-wide. Default `MCS_LOG_ERROR`. |
| `jnd_enable` | `0` skips JND. |
| `skin_skip` | Skip JND on skin blocks. Default 0. |
| `jnd_strength_level` | H.264: 0..7. H.265: `-1` auto, 0..2 force. |
| `jnd_scale` | Extra multiplier. Use `1.0` unless you want a different scale. |
| `dec_pic_reenc` | `0` syntax rewrite. `1` decode to display YUV and re-encode the residual. |

### 4.4 `mc_streaming_status_params_t`

| Field | Meaning |
|-------|---------|
| `max_width`, `max_height` | Session workspace cap. Starts 1920×1088. |
| `bits_save_rate` | Cumulative `(in-out)*100/in`. 0 if nothing emitted yet. |

## 5. Functions

| Function | Purpose | Return |
|----------|---------|--------|
| `int mc_streaming_enable(void **handle, mc_streaming_input_t *input, mc_streaming_output_t *output)` | Init-or-process one AU | `MCS_OK` or negative `MCS_ERR_*` |
| `int mc_streaming_disable(void *handle)` | Free session | `MCS_OK` or negative |
| `int mc_streaming_control(void **handle, mc_streaming_cmd_e cmd, const mc_streaming_ctrl_params_t *ctrl, mc_streaming_status_params_t *status_info)` | Set params or read status | `MCS_OK` or negative |
| `const char *mc_streaming_get_version(void)` | Version string | Static `"2.0.0"`. Do not free. |

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
| `MCS_ERR_CODEC_UNAVAILABLE` | -11 | No plugin registered for `codec_type` (H.265) |

The public session API treats `MCS_OK` as success for `mc_streaming_enable`.

Lower-case aliases (`mcs_ok`, `mcs_err_inval`, …) are provided for compatibility.
