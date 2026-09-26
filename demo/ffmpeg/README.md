# Build libmc_streaming into FFmpeg

**FFmpeg version: 9.0.1.** The files in this directory match the FFmpeg 9.0.1 tree in `vendor/ffmpeg-9.0.1`.

These two files turn MagicCode Streaming into an H.264 bitstream filter named `mc_streaming`. After the build, run it with `-c copy -bsf:v mc_streaming`. The input is an encoded Annex-B bitstream plus the display-order I420 file. The output is the processed Annex-B bitstream.

`vendor/ffmpeg-9.0.1` already contains the `configure` and `libavcodec/bsf/Makefile` edits, so the build flags below apply directly. On a clean FFmpeg 9.0.1 tree, apply the same two edits from section 2.

## 1. Drop the files into the source tree

| This directory | FFmpeg 9.0.1 source |
|----------------|---------------------|
| `mc_streaming.c` | `libavcodec/bsf/mc_streaming.c` |
| `bitstream_filters.c` | `libavcodec/bitstream_filters.c` |

`mc_streaming.c` is the filter. For each access unit it reads the POC, converts that to a display index, loads that frame from the I420 file given by `yuv=`, and calls `mc_streaming_enable` with the AU and that picture.

`bitstream_filters.c` is the FFmpeg 9.0.1 original plus one line:

```c
extern const FFBitStreamFilter ff_mc_streaming_bsf;
```

`configure` scans `libavcodec/bitstream_filters.c` with `find_things_extern` for `extern const FFBitStreamFilter ff_*_bsf`. That line registers the component `mc_streaming_bsf`, which is `--enable-bsf=mc_streaming`. On a tree other than 9.0.1, add only this line to the existing `bitstream_filters.c`.

The header and the library stay in this repository and are passed on the compile command line:

| Platform | Header | Library |
|----------|--------|---------|
| Linux | `include/mc_streaming.h` | `lib/linux/libmc_streaming.so` (link `-lmc_streaming`) |
| Windows | `include/mc_streaming.h` | `lib/windows/mc_streaming_dll.lib` (link `-lmc_streaming_dll`) |

On Windows, link the import library `mc_streaming_dll.lib` and keep `mc_streaming.dll` on the runtime path. The static archive `mc_streaming.lib` is a separate artifact.

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

Confirm the filter is in the binary:

```sh
./ffmpeg -hide_banner -bsfs | grep mc_streaming
```

## 4. Command line

`yuv=` is the I420 file that was encoded, stored in display order, one frame after another. When the bitstream contains B-frames, the filter reads POC, gets the display index, and loads that frame. A packet-order index sequence of `0, 3, 1, 2, 6, 4, 5` reads YUV frames 0, 3, 1, 2, 6, 4, and 5.

No B-frames: encode with `-tune zerolatency` and set `mcs_codec=zero_delay`.

```sh
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 -i input.yuv \
    -an -c:v libx264 -tune zerolatency out_bs.264
ffmpeg -i out_bs.264 -c copy \
    -bsf:v "mc_streaming=yuv=input.yuv:mcs_codec=zero_delay" out.264
```

With B-frames, set `mcs_codec=h264_8bit`.

```sh
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 -i input.yuv \
    -an -c:v libx264 -bf 2 out_bs.264
ffmpeg -i out_bs.264 -c copy \
    -bsf:v "mc_streaming=yuv=input.yuv:mcs_codec=h264_8bit" out.264
```

Put `-c:v` before the output file name. `-c copy` keeps the input bitstream and runs it through `mc_streaming`. For a raw `.264` input whose size is missing, add `:width=1280:height=720` to the filter options.

| Option | Meaning |
|--------|---------|
| `yuv` | Display-order I420 path. Required. |
| `width` / `height` | YUV size. `0` uses the input stream size. |
| `mcs_codec` | `zero_delay` (no B-frames) or `h264_8bit` (B-frames) |
| `mcs_log` | `none`, `error`, `warning`, `info`, `debug` |
