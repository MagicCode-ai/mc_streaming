# Build libmc_streaming into FFmpeg

**FFmpeg version: 9.0.1.** The files in this directory match the FFmpeg 9.0.1 tree in `vendor/ffmpeg-9.0.1`.

These two files turn MagicCode Streaming 2.0.0 into a bitstream filter named `mc_streaming` for H.264 and H.265. After the build, run it with `-c copy -bsf:v mc_streaming`. The input is an encoded Annex-B access unit. With `dec_pic_reenc=0` the filter also reads the display-order I420 file. With `dec_pic_reenc=1` the library re-encodes from the decoded picture and the filter does not read YUV. The output is the processed Annex-B bitstream.

`vendor/ffmpeg-9.0.1` already contains the `configure` and `libavcodec/bsf/Makefile` edits, so the build flags below apply directly. On a clean FFmpeg 9.0.1 tree, apply the same two edits from section 2.

## 1. Drop the files into the source tree

| This directory | FFmpeg 9.0.1 source |
|----------------|---------------------|
| `mc_streaming.c` | `libavcodec/bsf/mc_streaming.c` |
| `bitstream_filters.c` | `libavcodec/bitstream_filters.c` |

`mc_streaming.c` is the filter. It calls `mc_streaming_get_version`, `mc_streaming_control(MCS_CMD_SET_PARAMS)`, and `mc_streaming_enable`. H.264 is inside `libmc_streaming`. H.265 (`mcs_codec=h265_zero_delay` or `h265_8bit`) needs the `mcs_codec_h265` plugin on `MCS_PLUGIN_PATH`, or in the same directory as `libmc_streaming`. `dec_pic_reenc=0` reads POC, converts that to a display index, and loads that frame from `yuv=`. `dec_pic_reenc=1` leaves the picture planes empty.

`bitstream_filters.c` is the FFmpeg 9.0.1 original plus one line:

```c
extern const FFBitStreamFilter ff_mc_streaming_bsf;
```

`configure` scans `libavcodec/bitstream_filters.c` with `find_things_extern` for `extern const FFBitStreamFilter ff_*_bsf`. That line registers the component `mc_streaming_bsf`, which is `--enable-bsf=mc_streaming`. On a tree other than 9.0.1, add only this line to the existing `bitstream_filters.c`.

The header and the library stay in this repository and are passed on the compile command line:

| Platform | Header | Host library | H.265 plugin |
|----------|--------|--------------|--------------|
| Linux | `include/mc_streaming.h` | `lib/linux/libmc_streaming.so` | `lib/linux/libmcs_codec_h265.so` |
| Windows | `include/mc_streaming.h` | `lib/windows/mc_streaming_dll.lib` | `lib/windows/mcs_codec_h265.dll` |
| macOS arm64 | `include/mc_streaming.h` | `lib/macos/libmc_streaming.dylib` | `lib/macos/libmcs_codec_h265.dylib` |

Link the host library (`-lmc_streaming` on Linux and macOS, `-lmc_streaming_dll` on Windows). The public headers are `include/mc_streaming.h` and `include/mcs_err.h`. H.265 stays a dynamic plugin: the host loads `mcs_plugin_entry_v1` from `libmcs_codec_*.so`, `.dylib`, or `.dll`. There is no H.265 static archive on macOS. Linux `libmcs_codec_h265.a` and Windows `mcs_codec_h265.lib` are not used by this filter.

On Windows, keep `mc_streaming.dll` and, for H.265, `mcs_codec_h265.dll` on the runtime path. The static archive `mc_streaming.lib` is a separate H.264 artifact.

## 2. Register the build

Add this line to `libavcodec/bsf/Makefile`:

```make
OBJS-$(CONFIG_MC_STREAMING_BSF)           += bsf/mc_streaming.o
```

Declare the library dependency in `configure`, next to `libx264_encoder_deps`:

```
mc_streaming_bsf_deps="libmcstreaming"
```

When `libmcstreaming` is enabled, check `mc_streaming_get_version`. Linux uses `pkg-config libmc_streaming` or `-lmc_streaming`. Windows uses `-lmc_streaming_dll`. `vendor/ffmpeg-9.0.1/configure` already contains this check.

`--enable-libmcstreaming` is enough to build the bitstream filter. libx264 does not have to be linked into that same ffmpeg binary.

## 3. Build

Linux example. Unrelated components stay off, and raw H.264 demux, mux, and this filter stay on. With `--disable-everything`, also pass `--enable-avfilter`, because the `ffmpeg` program depends on it.

```sh
ROOT=/path/to/magic_steaming
cd /path/to/ffmpeg-build
/path/to/ffmpeg-9.0.1/configure \
    --disable-everything \
    --disable-autodetect \
    --enable-avfilter \
    --enable-ffmpeg \
    --enable-protocol=file \
    --enable-demuxer=h264 \
    --enable-muxer=h264 \
    --enable-parser=h264 \
    --enable-bsf=mc_streaming \
    --enable-libmcstreaming \
    --extra-cflags="-I$ROOT/include" \
    --extra-ldflags="-L$ROOT/lib/linux -Wl,-rpath,$ROOT/lib/linux"
make -j"$(nproc)"
```

For H.265, add `--enable-demuxer=hevc --enable-muxer=hevc --enable-parser=hevc` to the same configure line. macOS uses `-L$ROOT/lib/macos` and `libmc_streaming.dylib` instead of the Linux `-L` line.

Confirm the filter is in the binary:

```sh
./ffmpeg -hide_banner -bsfs | grep mc_streaming
```

## 4. Command line

`dec_pic_reenc=0` reads `yuv=`, the I420 file that was encoded, stored in display order. When the bitstream contains B-frames, the filter reads POC and loads that frame. A packet-order index sequence of `0, 3, 1, 2, 6, 4, 5` reads YUV frames 0, 3, 1, 2, 6, 4, and 5.

`dec_pic_reenc=1` does not read `yuv=`. The library decodes the access unit and re-encodes the residual from that picture. `jnd=1` runs JND on that decoded picture. `jnd=0` only skips JND. Not calling the filter is what turns streaming off.

No B-frames: encode with `-tune zerolatency` and set `mcs_codec=zero_delay`.

```sh
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 -i input.yuv \
    -an -c:v libx264 -tune zerolatency out_bs.264
ffmpeg -i out_bs.264 -c copy \
    -bsf:v "mc_streaming=yuv=input.yuv:mcs_codec=zero_delay:jnd=1" out.264
```

With B-frames, set `mcs_codec=h264_8bit`.

```sh
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 -i input.yuv \
    -an -c:v libx264 -bf 2 out_bs.264
ffmpeg -i out_bs.264 -c copy \
    -bsf:v "mc_streaming=yuv=input.yuv:mcs_codec=h264_8bit:jnd=1:dec_pic_reenc=0" out.264
```

H.265 uses the plugin. This example re-encodes from the decoded picture, so `yuv=` is omitted.

```sh
export MCS_PLUGIN_PATH=/path/to/magic_steaming/lib/linux
ffmpeg -i in.265 -c copy \
    -bsf:v "mc_streaming=mcs_codec=h265_zero_delay:jnd=1:dec_pic_reenc=1" out.265
```

For MP4 input, convert length-prefixed NALs first: `-bsf:v hevc_mp4toannexb,mc_streaming=...` or `h264_mp4toannexb,mc_streaming=...`. The filter also accepts AVCC and HVCC extradata on its own.

Put `-c:v` before the output file name. `-c copy` keeps the input bitstream and runs it through `mc_streaming`. For a raw elementary stream whose size is missing, add `:width=1280:height=720`. Each dimension must be even and at most 32768.

| Option | Meaning |
|--------|---------|
| `yuv` | Display-order I420 path. Required when `dec_pic_reenc=0`. |
| `width` / `height` | Picture size. `0` uses the input stream size. Maximum 32768. |
| `mcs_codec` | `zero_delay`, `h264_8bit`, `h265_zero_delay`, `h265_8bit` |
| `jnd` | `0` skips JND. `1` runs JND on the decoded picture. |
| `jnd_strength` | H.264 `0..7`. H.265 `-1` auto, or `0..2`. |
| `jnd_scale` | Extra JND scale. `0` is treated as `1`. |
| `skin_skip` | `1` skips JND on skin blocks. |
| `dec_pic_reenc` | `0` syntax rewrite using `yuv=`. `1` re-encode from the decoded picture. |
| `mcs_log` | `none`, `error`, `warning`, `info`, `debug` |
