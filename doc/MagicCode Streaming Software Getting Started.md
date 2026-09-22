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

## 5. Next documents

- Integration Guide: Android `--whole-archive` / iOS `-force_load` and system libraries.
- API Reference: structs, enums, return codes.
- Specification: bitstream and session limits.
- EULA: license terms for use and distribution.
