# MagicCode Streaming Software Known Issues

**Document Version:** v1.0.0. **Release Date:** September 19, 2026.

## 1. Scope

Known limitations for MagicCode Streaming Software v1.0.0.

## 2. Functional constraints

- I/IDR pictures are **copied**. Residual processing applies to P/B only.
- 8-bit progressive H.264 only. Interlaced and 10-bit streams are not supported.
- YUV must be I420. Other pixel formats are not accepted.
- `frame_type` must match the AU. A mismatch returns `MCS_ERR_KEY` (the AU may still be copied).
- Output buffer smaller than a copy-sized AU returns `MCS_ERR_OUTPUT` with nothing emitted.
- Session is single-threaded per handle.
- Published mobile archives are arm64 device only (Android arm64-v8a, iOS device). No 32-bit, no iOS Simulator, no Windows SDK in this drop.

## 3. Quality and bitstream

- Bitrate saving depends on content and codec profile. It is not a guaranteed percentage.
- After a process failure the library may **bypass-copy** until the next I/IDR. Downstream decoders will see the original encoder AU for those pictures.
- B-frame residual may change when a reference P was rewritten, even with `MCS_H264_ZERO_DELAY_8BIT`.

## 4. Performance

- `MCS_H264_8BIT` uses more CPU than `MCS_H264_ZERO_DELAY_8BIT`.
- The Android `.a` file on disk is larger than the code that ends up in a stripped `.so`. That on-disk size is not APK bloat of the same amount.

## 5. Mitigation

- Always check `output.bs_size` as well as the return code.
- On `MCS_ERR_SIZE` / `MCS_ERR_KEY` / `MCS_ERR_CROP`, inspect whether a copied AU was still emitted.
- Keep GOP reasonably short (for example 2 s) so a bypass window ends at the next key.
- Validate each hardware encoder’s Annex-B + I420 pairing on the target device before shipping.
