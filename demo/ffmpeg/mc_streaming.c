/*
 * mc_streaming bitstream filter for libmc_streaming 2.0.0.
 *
 * H.264 is built into libmc_streaming. H.265 is the mcs_codec_h265 plugin:
 * put that shared library next to libmc_streaming, or set MCS_PLUGIN_PATH
 * to its directory. mcs_codec is zero_delay / h264_8bit / h265_zero_delay /
 * h265_8bit.
 *
 * dec_pic_reenc=0 reads the display-order I420 file named by yuv=. The frame
 * index comes from POC, so B-frame streams still pair with the picture that
 * produced the access unit. dec_pic_reenc=1 re-encodes from the decoded
 * picture and does not read yuv=.
 *
 *   ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 -i input.yuv \
 *       -an -c:v libx264 -bf 2 out_bs.264
 *   ffmpeg -i out_bs.264 -c copy \
 *       -bsf:v "mc_streaming=yuv=input.yuv:mcs_codec=h264_8bit:jnd=1" out.264
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavcodec/bsf.h"
#include "libavcodec/bsf_internal.h"
#include "libavcodec/defs.h"
#include "libavcodec/packet.h"

#include "mc_streaming.h"

/* Public header limit: each dimension is in (0, 32768]. */
#define MC_BSF_MAX_DIM (32 * 1024)

typedef struct McBsf {
    const AVClass *class;
    char *yuv_path;
    int mcs_codec;
    int mcs_log;
    int jnd_enable;
    int jnd_strength;
    float jnd_scale;
    int skin_skip;
    int dec_pic_reenc;
    int width;
    int height;
    int hevc;
    int separate_colour_plane;
    int dependent_slice;
    int output_flag_present;
    int num_extra_slice_header_bits;

    FILE *yuv;
    uint8_t *frame;
    size_t frame_bytes;
    uint8_t *in_au;
    size_t in_cap;
    uint8_t *out_au;
    size_t out_cap;
    uint8_t *ps;
    int ps_size;
    int ps_owned;
    void *mcs;
    int64_t index;
    int64_t yuv_pos;
    int nal_len_size;
    int have_sps;
    int log2_max_frame_num;
    int poc_type;
    int log2_max_poc_lsb;
    int frame_mbs_only;
    int bottom_poc_present;
    int prev_poc_msb;
    int prev_poc_lsb;
    int poc_lsb_odd;
    int poc_base;
    int display_epoch;
    int max_display_order;
    int coded_order;
    int poc_fallback_warned;
} McBsf;

static int start_code_len(const uint8_t *p, int n)
{
    if (n >= 4 && !p[0] && !p[1] && !p[2] && p[3] == 1)
        return 4;
    if (n >= 3 && !p[0] && !p[1] && p[2] == 1)
        return 3;
    return 0;
}

static const uint8_t *find_start(const uint8_t *p, const uint8_t *end, int *sc)
{
    while (p + 3 < end) {
        if (!p[0] && !p[1]) {
            if (p[2] == 1) {
                *sc = 3;
                return p;
            }
            if (!p[2] && p[3] == 1) {
                *sc = 4;
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static unsigned get_ue(const uint8_t *b, int nbits, int *bit)
{
    int zeros = 0, i;
    unsigned val;

    while (*bit < nbits) {
        int v = (b[*bit >> 3] >> (7 - (*bit & 7))) & 1;
        (*bit)++;
        if (v)
            break;
        if (++zeros > 31)
            return 0;
    }
    val = 1;
    for (i = 0; i < zeros && *bit < nbits; i++) {
        int v = (b[*bit >> 3] >> (7 - (*bit & 7))) & 1;
        (*bit)++;
        val = (val << 1) | (unsigned)v;
    }
    return val - 1;
}

static int get_bit(const uint8_t *b, int nbits, int *bit)
{
    int v;

    if (*bit >= nbits)
        return -1;
    v = (b[*bit >> 3] >> (7 - (*bit & 7))) & 1;
    (*bit)++;
    return v;
}

static int get_bits_n(const uint8_t *b, int nbits, int *bit, int n)
{
    int v = 0, i;

    for (i = 0; i < n; i++) {
        int x = get_bit(b, nbits, bit);
        if (x < 0)
            return -1;
        v = (v << 1) | x;
    }
    return v;
}

static int get_se(const uint8_t *b, int nbits, int *bit)
{
    unsigned ue = get_ue(b, nbits, bit);

    if (*bit > nbits)
        return 0;
    return (ue & 1) ? (int)((ue + 1) >> 1) : -(int)(ue >> 1);
}

static int rbsp_unescape(const uint8_t *src, int n, uint8_t *dst, int dst_cap)
{
    int i, o = 0;

    for (i = 0; i < n && o < dst_cap; i++) {
        if (i + 2 < n && !src[i] && !src[i + 1] && src[i + 2] == 3) {
            if (o + 2 > dst_cap)
                break;
            dst[o++] = 0;
            dst[o++] = 0;
            i += 2;
            continue;
        }
        dst[o++] = src[i];
    }
    return o;
}

static int skip_scaling_list(const uint8_t *b, int nbits, int *bit, int size)
{
    int i, last = 8, next = 8;

    for (i = 0; i < size; i++) {
        if (next) {
            int se = get_se(b, nbits, bit);
            next = (last + se + 256) & 255;
        }
        last = next ? next : last;
    }
    return *bit <= nbits ? 0 : -1;
}

static int high_profile(int profile)
{
    switch (profile) {
    case 100: case 110: case 122: case 244:
    case 44: case 83: case 86: case 118: case 128:
    case 138: case 139: case 134: case 135:
        return 1;
    default:
        return 0;
    }
}

/* Keep the SPS fields needed to turn a slice POC into a display index. */
static int parse_sps(McBsf *s, const uint8_t *nal, int nal_size)
{
    uint8_t rbsp[2048];
    int o, bit = 0, nbits, profile, chroma = 1, i, nlist;

    if (nal_size < 4)
        return -1;
    o = rbsp_unescape(nal + 1, nal_size - 1, rbsp, (int)sizeof(rbsp));
    if (o < 4)
        return -1;
    nbits = o * 8;
    profile = get_bits_n(rbsp, nbits, &bit, 8);
    if (profile < 0)
        return -1;
    get_bits_n(rbsp, nbits, &bit, 8);
    get_bits_n(rbsp, nbits, &bit, 8);
    get_ue(rbsp, nbits, &bit);
    if (high_profile(profile)) {
        chroma = (int)get_ue(rbsp, nbits, &bit);
        if (chroma == 3 && get_bit(rbsp, nbits, &bit) < 0)
            return -1;
        get_ue(rbsp, nbits, &bit);
        get_ue(rbsp, nbits, &bit);
        if (get_bit(rbsp, nbits, &bit) < 0)
            return -1;
        {
            int scaling = get_bit(rbsp, nbits, &bit);
            if (scaling < 0)
                return -1;
            if (scaling == 1) {
                nlist = chroma == 3 ? 12 : 8;
                for (i = 0; i < nlist; i++) {
                    int present = get_bit(rbsp, nbits, &bit);
                    if (present < 0)
                        return -1;
                    if (present &&
                        skip_scaling_list(rbsp, nbits, &bit, i < 6 ? 16 : 64) < 0)
                        return -1;
                }
            }
        }
    }
    {
        int log2_max_frame_num = (int)get_ue(rbsp, nbits, &bit) + 4;
        int poc_type = (int)get_ue(rbsp, nbits, &bit);
        int log2_max_poc_lsb = 0;
        int frame_mbs_only;

        if (poc_type == 0)
            log2_max_poc_lsb = (int)get_ue(rbsp, nbits, &bit) + 4;
        else if (poc_type == 1) {
            get_bit(rbsp, nbits, &bit);
            get_se(rbsp, nbits, &bit);
            get_se(rbsp, nbits, &bit);
            nlist = (int)get_ue(rbsp, nbits, &bit);
            for (i = 0; i < nlist; i++)
                get_se(rbsp, nbits, &bit);
        }
        get_ue(rbsp, nbits, &bit);
        get_bit(rbsp, nbits, &bit);
        get_ue(rbsp, nbits, &bit);
        get_ue(rbsp, nbits, &bit);
        frame_mbs_only = get_bit(rbsp, nbits, &bit);
        if (frame_mbs_only < 0 || bit > nbits)
            return -1;
        if (log2_max_frame_num < 4 || log2_max_frame_num > 16)
            return -1;
        if (poc_type == 0 && (log2_max_poc_lsb < 4 || log2_max_poc_lsb > 16))
            return -1;
        s->log2_max_frame_num = log2_max_frame_num;
        s->poc_type = poc_type;
        s->log2_max_poc_lsb = log2_max_poc_lsb;
        s->frame_mbs_only = frame_mbs_only;
        s->have_sps = 1;
    }
    return 0;
}

static int parse_pps(McBsf *s, const uint8_t *nal, int nal_size)
{
    uint8_t rbsp[64];
    int o, bit = 0, nbits, present;

    if (nal_size < 2)
        return -1;
    o = rbsp_unescape(nal + 1, nal_size - 1, rbsp, (int)sizeof(rbsp));
    if (o < 1)
        return -1;
    nbits = o * 8;
    get_ue(rbsp, nbits, &bit);
    get_ue(rbsp, nbits, &bit);
    if (get_bit(rbsp, nbits, &bit) < 0)
        return -1;
    present = get_bit(rbsp, nbits, &bit);
    if (present < 0)
        return -1;
    s->bottom_poc_present = present;
    return 0;
}

/* Display-order YUV index from POC type 0. Same epoch rule as decode.c. */
static int64_t slice_display(McBsf *s, const uint8_t *nal, int nal_size)
{
    uint8_t rbsp[256];
    int o, bit = 0, nbits, nal_type, ref_idc, idr;
    int poc_lsb, poc_msb, poc, max_poc_lsb, div, delta, disp;

    if (!s->have_sps || s->poc_type != 0 || nal_size < 2)
        return -1;
    nal_type = nal[0] & 0x1f;
    ref_idc = (nal[0] >> 5) & 3;
    o = rbsp_unescape(nal + 1, nal_size - 1, rbsp, (int)sizeof(rbsp));
    if (o < 2)
        return -1;
    nbits = o * 8;
    get_ue(rbsp, nbits, &bit);
    get_ue(rbsp, nbits, &bit);
    get_ue(rbsp, nbits, &bit);
    if (get_bits_n(rbsp, nbits, &bit, s->log2_max_frame_num) < 0)
        return -1;
    if (!s->frame_mbs_only) {
        int field = get_bit(rbsp, nbits, &bit);
        if (field < 0)
            return -1;
        if (field)
            return -1;
    }
    if (nal_type == 5) {
        get_ue(rbsp, nbits, &bit);
        if (bit > nbits)
            return -1;
    }
    poc_lsb = get_bits_n(rbsp, nbits, &bit, s->log2_max_poc_lsb);
    if (poc_lsb < 0)
        return -1;

    idr = nal_type == 5;
    {
        int prev_msb = idr ? 0 : s->prev_poc_msb;
        int prev_lsb = idr ? 0 : s->prev_poc_lsb;
        int delta_bottom = 0;

        if (s->bottom_poc_present) {
            delta_bottom = get_se(rbsp, nbits, &bit);
            if (bit > nbits)
                return -1;
        }
        max_poc_lsb = 1 << s->log2_max_poc_lsb;
        if (poc_lsb < prev_lsb && prev_lsb - poc_lsb >= max_poc_lsb / 2)
            poc_msb = prev_msb + max_poc_lsb;
        else if (poc_lsb > prev_lsb && prev_lsb - poc_lsb < -max_poc_lsb / 2)
            poc_msb = prev_msb - max_poc_lsb;
        else
            poc_msb = prev_msb;
        poc = poc_msb + poc_lsb;
        if (delta_bottom < 0)
            poc += delta_bottom;
    }
    if (ref_idc) {
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

static void note_sps(McBsf *s, const uint8_t *src, int len)
{
    const uint8_t *end = src + len;
    int sc = 0;
    const uint8_t *start = find_start(src, end, &sc);

    while (start) {
        const uint8_t *nal = start + sc;
        int sc2 = 0;
        const uint8_t *next = find_start(nal, end, &sc2);
        const uint8_t *nal_end = next ? next : end;
        int nal_size = (int)(nal_end - nal);

        if (nal_size > 0 && s->hevc) {
            int nal_type = hevc_nal_type(nal);
            if (nal_type == 33)
                parse_hevc_sps(s, nal, nal_size);
            else if (nal_type == 34)
                parse_hevc_pps(s, nal, nal_size);
        } else if (nal_size > 0 && (nal[0] & 0x1f) == 7)
            parse_sps(s, nal, nal_size);
        else if (nal_size > 0 && (nal[0] & 0x1f) == 8)
            parse_pps(s, nal, nal_size);
        if (!next)
            break;
        start = next;
        sc = sc2;
    }
}

static int64_t hevc_au_display(McBsf *s, const uint8_t *src, int len)
{
    const uint8_t *end = src + len;
    int sc = 0;
    const uint8_t *start = find_start(src, end, &sc);

    while (start) {
        const uint8_t *nal = start + sc;
        int sc2 = 0, nal_type, nal_size;
        const uint8_t *next = find_start(nal, end, &sc2);
        const uint8_t *nal_end = next ? next : end;
        int key = 0, poc = 0, poc_ok = 0, idr = 0, hr;

        nal_size = (int)(nal_end - nal);
        if (nal_size > 0) {
            nal_type = hevc_nal_type(nal);
            if (nal_type == 33)
                parse_hevc_sps(s, nal, nal_size);
            else if (nal_type == 34)
                parse_hevc_pps(s, nal, nal_size);
            else if (hevc_vcl(nal_type)) {
                hr = hevc_slice_header(s, nal, nal_size, &key, &poc, &poc_ok, &idr);
                if (hr == 0 && poc_ok) {
                    int is_ref = (nal_type & 1) || nal_type >= 16;
                    return hevc_poc_display(s, poc, idr, is_ref);
                }
                if (hr == 0)
                    return -1;
            }
        }
        if (!next)
            break;
        start = next;
        sc = sc2;
    }
    return -1;
}

static int64_t au_display_index(McBsf *s, const uint8_t *src, int len)
{
    const uint8_t *end = src + len;
    int sc = 0, got = 0;

    if (s->hevc)
        return hevc_au_display(s, src, len);
    int64_t disp = -1;
    const uint8_t *start = find_start(src, end, &sc);

    while (start) {
        const uint8_t *nal = start + sc;
        int sc2 = 0, nal_type;
        const uint8_t *next = find_start(nal, end, &sc2);
        const uint8_t *nal_end = next ? next : end;
        int nal_size = (int)(nal_end - nal);

        if (nal_size > 0) {
            nal_type = nal[0] & 0x1f;
            if (nal_type == 7)
                parse_sps(s, nal, nal_size);
            else if (nal_type == 8)
                parse_pps(s, nal, nal_size);
            else if (!got && (nal_type == 1 || nal_type == 5)) {
                disp = slice_display(s, nal, nal_size);
                got = 1;
            }
        }
        if (!next)
            break;
        start = next;
        sc = sc2;
    }
    return got ? disp : -1;
}

static int yuv_seek(FILE *f, uint64_t off)
{
#if defined(_MSC_VER)
    return _fseeki64(f, (int64_t)off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

static int read_display_frame(McBsf *s, int64_t index)
{
    uint64_t off;

    if (index < 0)
        return AVERROR(EINVAL);
    if (index != s->yuv_pos) {
        if ((uint64_t)index > UINT64_MAX / s->frame_bytes)
            return AVERROR(ERANGE);
        off = (uint64_t)index * s->frame_bytes;
        if (yuv_seek(s->yuv, off) != 0)
            return AVERROR(EIO);
    }
    if (fread(s->frame, 1, s->frame_bytes, s->yuv) != s->frame_bytes)
        return AVERROR(EIO);
    s->yuv_pos = index + 1;
    return 0;
}

static int slice_is_i(const uint8_t *nal, int n)
{
    uint8_t tmp[32];
    int o = 0, bit = 0, i;
    unsigned slice_type;

    if (n < 2)
        return 0;
    for (i = 1; i < n && o < (int)sizeof(tmp); i++) {
        if (i + 2 < n && !nal[i] && !nal[i + 1] && nal[i + 2] == 3) {
            if (o + 2 > (int)sizeof(tmp))
                break;
            tmp[o++] = 0;
            tmp[o++] = 0;
            i += 2;
            continue;
        }
        tmp[o++] = nal[i];
    }
    get_ue(tmp, o * 8, &bit);
    slice_type = get_ue(tmp, o * 8, &bit);
    return (slice_type % 5) == 2;
}

static int skip_bits(const uint8_t *b, int nbits, int *bit, int n)
{
    if (n < 0 || *bit > nbits - n)
        return -1;
    *bit += n;
    return 0;
}

static int hevc_nal_type(const uint8_t *nal)
{
    return (nal[0] >> 1) & 0x3f;
}

static int hevc_vcl(int nal_type)
{
    return nal_type <= 9 || (nal_type >= 16 && nal_type <= 21);
}

static int hevc_idr(int nal_type)
{
    return nal_type == 19 || nal_type == 20;
}

/* profile_tier_level() general/sub-layer profile bits, not the level byte. */
static int skip_hevc_profile_data(const uint8_t *b, int nbits, int *bit)
{
    int profile_idc, compat[32], i;
    int ext, high, main444;

    if (skip_bits(b, nbits, bit, 2) < 0 || get_bit(b, nbits, bit) < 0)
        return -1;
    profile_idc = get_bits_n(b, nbits, bit, 5);
    if (profile_idc < 0)
        return -1;
    for (i = 0; i < 32; i++) {
        compat[i] = get_bit(b, nbits, bit);
        if (compat[i] < 0)
            return -1;
    }
    if (skip_bits(b, nbits, bit, 4) < 0)
        return -1;
    ext = profile_idc == 4 || compat[4] ||
          profile_idc == 5 || compat[5] ||
          profile_idc == 6 || compat[6] ||
          profile_idc == 7 || compat[7] ||
          profile_idc == 8 || compat[8] ||
          profile_idc == 9 || compat[9] ||
          profile_idc == 10 || compat[10] ||
          profile_idc == 11 || compat[11];
    high = profile_idc == 5 || compat[5] ||
           profile_idc == 6 || compat[6] ||
           profile_idc == 7 || compat[7] ||
           profile_idc == 8 || compat[8] ||
           profile_idc == 9 || compat[9] ||
           profile_idc == 10 || compat[10] ||
           profile_idc == 11 || compat[11];
    main444 = profile_idc == 2 || compat[2];
    if (ext) {
        if (skip_bits(b, nbits, bit, 9) < 0)
            return -1;
        if (high) {
            if (get_bit(b, nbits, bit) < 0 || skip_bits(b, nbits, bit, 33) < 0)
                return -1;
        } else if (skip_bits(b, nbits, bit, 34) < 0) {
            return -1;
        }
    } else if (main444) {
        if (skip_bits(b, nbits, bit, 7) < 0 || get_bit(b, nbits, bit) < 0 ||
            skip_bits(b, nbits, bit, 35) < 0)
            return -1;
    } else if (skip_bits(b, nbits, bit, 43) < 0) {
        return -1;
    }
    return get_bit(b, nbits, bit) < 0 ? -1 : 0;
}

static int skip_hevc_profile_tier_level(const uint8_t *b, int nbits, int *bit,
                                        int max_sub_layers)
{
    int i, profile_present[7], level_present[7];

    if (max_sub_layers < 0 || max_sub_layers > 6)
        return -1;
    if (skip_hevc_profile_data(b, nbits, bit) < 0)
        return -1;
    if (skip_bits(b, nbits, bit, 8) < 0)
        return -1;
    for (i = 0; i < max_sub_layers; i++) {
        profile_present[i] = get_bit(b, nbits, bit);
        level_present[i] = get_bit(b, nbits, bit);
        if (profile_present[i] < 0 || level_present[i] < 0)
            return -1;
    }
    if (max_sub_layers > 0 &&
        skip_bits(b, nbits, bit, 2 * (8 - max_sub_layers)) < 0)
        return -1;
    for (i = 0; i < max_sub_layers; i++) {
        if (profile_present[i] && skip_hevc_profile_data(b, nbits, bit) < 0)
            return -1;
        if (level_present[i] && skip_bits(b, nbits, bit, 8) < 0)
            return -1;
    }
    return 0;
}

static int parse_hevc_sps(McBsf *s, const uint8_t *nal, int nal_size)
{
    uint8_t rbsp[4096];
    int o, bit = 0, nbits, max_sub, chroma, win, log2;

    if (nal_size < 3)
        return -1;
    o = rbsp_unescape(nal + 2, nal_size - 2, rbsp, (int)sizeof(rbsp));
    if (o < 4)
        return -1;
    nbits = o * 8;
    if (skip_bits(rbsp, nbits, &bit, 4) < 0)
        return -1;
    max_sub = get_bits_n(rbsp, nbits, &bit, 3);
    if (max_sub < 0 || get_bit(rbsp, nbits, &bit) < 0)
        return -1;
    if (skip_hevc_profile_tier_level(rbsp, nbits, &bit, max_sub) < 0)
        return -1;
    get_ue(rbsp, nbits, &bit);
    chroma = (int)get_ue(rbsp, nbits, &bit);
    s->separate_colour_plane = 0;
    if (chroma == 3) {
        int sep = get_bit(rbsp, nbits, &bit);
        if (sep < 0)
            return -1;
        s->separate_colour_plane = sep;
    }
    get_ue(rbsp, nbits, &bit);
    get_ue(rbsp, nbits, &bit);
    win = get_bit(rbsp, nbits, &bit);
    if (win < 0)
        return -1;
    if (win) {
        get_ue(rbsp, nbits, &bit);
        get_ue(rbsp, nbits, &bit);
        get_ue(rbsp, nbits, &bit);
        get_ue(rbsp, nbits, &bit);
    }
    get_ue(rbsp, nbits, &bit);
    get_ue(rbsp, nbits, &bit);
    log2 = (int)get_ue(rbsp, nbits, &bit) + 4;
    if (bit > nbits || log2 < 4 || log2 > 16)
        return -1;
    s->log2_max_poc_lsb = log2;
    s->poc_type = 0;
    s->have_sps = 1;
    return 0;
}

static int parse_hevc_pps(McBsf *s, const uint8_t *nal, int nal_size)
{
    uint8_t rbsp[256];
    int o, bit = 0, nbits, dep, outf, extra;

    if (nal_size < 3)
        return -1;
    o = rbsp_unescape(nal + 2, nal_size - 2, rbsp, (int)sizeof(rbsp));
    if (o < 2)
        return -1;
    nbits = o * 8;
    get_ue(rbsp, nbits, &bit);
    get_ue(rbsp, nbits, &bit);
    dep = get_bit(rbsp, nbits, &bit);
    outf = get_bit(rbsp, nbits, &bit);
    extra = get_bits_n(rbsp, nbits, &bit, 3);
    if (dep < 0 || outf < 0 || extra < 0 || bit > nbits)
        return -1;
    s->dependent_slice = dep;
    s->output_flag_present = outf;
    s->num_extra_slice_header_bits = extra;
    return 0;
}

/* 0 = header read, 1 = not the first slice segment, -1 = parse error.
 * *poc is the POC LSB, or 0 for IDR. *poc_ok is 0 when POC could not be read. */
static int hevc_slice_header(McBsf *s, const uint8_t *nal, int nal_size,
                             int *key, int *poc, int *poc_ok, int *idr)
{
    uint8_t rbsp[512];
    int o, bit = 0, nbits, nal_type, first, slice_type;

    *key = 0;
    *poc = 0;
    *poc_ok = 0;
    *idr = 0;
    if (nal_size < 3)
        return -1;
    nal_type = hevc_nal_type(nal);
    if (!hevc_vcl(nal_type))
        return -1;
    o = rbsp_unescape(nal + 2, nal_size - 2, rbsp, (int)sizeof(rbsp));
    if (o < 2)
        return -1;
    nbits = o * 8;
    first = get_bit(rbsp, nbits, &bit);
    if (first < 0)
        return -1;
    if (nal_type >= 16 && nal_type <= 23 && get_bit(rbsp, nbits, &bit) < 0)
        return -1;
    get_ue(rbsp, nbits, &bit);
    if (!first)
        return 1;
    if (s->num_extra_slice_header_bits &&
        skip_bits(rbsp, nbits, &bit, s->num_extra_slice_header_bits) < 0)
        return -1;
    slice_type = (int)get_ue(rbsp, nbits, &bit);
    if (bit > nbits)
        return -1;
    *idr = hevc_idr(nal_type);
    *key = *idr || (slice_type % 5) == 2;
    if (!s->have_sps || s->log2_max_poc_lsb < 4)
        return 0;
    if (*idr) {
        *poc = 0;
        *poc_ok = 1;
        return 0;
    }
    if (s->output_flag_present && get_bit(rbsp, nbits, &bit) < 0)
        return -1;
    if (s->separate_colour_plane && get_bits_n(rbsp, nbits, &bit, 2) < 0)
        return -1;
    *poc = get_bits_n(rbsp, nbits, &bit, s->log2_max_poc_lsb);
    if (*poc < 0 || bit > nbits)
        return -1;
    *poc_ok = 1;
    return 0;
}

static int64_t hevc_poc_display(McBsf *s, int poc_lsb, int idr, int is_ref)
{
    int prev_msb = idr ? 0 : s->prev_poc_msb;
    int prev_lsb = idr ? 0 : s->prev_poc_lsb;
    int max_poc_lsb = 1 << s->log2_max_poc_lsb;
    int poc_msb, poc, delta, disp;

    if (poc_lsb < prev_lsb && prev_lsb - poc_lsb >= max_poc_lsb / 2)
        poc_msb = prev_msb + max_poc_lsb;
    else if (poc_lsb > prev_lsb && prev_lsb - poc_lsb < -max_poc_lsb / 2)
        poc_msb = prev_msb - max_poc_lsb;
    else
        poc_msb = prev_msb;
    poc = poc_msb + poc_lsb;
    if (is_ref) {
        s->prev_poc_msb = poc_msb;
        s->prev_poc_lsb = poc_lsb;
    }
    if (idr || s->coded_order == 0) {
        s->poc_base = poc;
        if (idr && s->coded_order > 0)
            s->display_epoch = s->max_display_order + 1;
    }
    delta = poc - s->poc_base;
    if (delta < 0)
        delta = 0;
    disp = s->display_epoch + delta;
    if (disp > s->max_display_order)
        s->max_display_order = disp;
    s->coded_order++;
    return disp;
}

/* First VCL decides. key is I/IDR, not only IDR. */
static void au_scan(McBsf *s, const uint8_t *src, int len, int *key, int *has_sps)
{
    const uint8_t *end = src + len;
    int sc = 0;
    const uint8_t *start = find_start(src, end, &sc);

    *key = 0;
    *has_sps = 0;
    while (start) {
        const uint8_t *nal = start + sc;
        int sc2 = 0;
        const uint8_t *next = find_start(nal, end, &sc2);
        const uint8_t *nal_end = next ? next : end;
        int nal_size = (int)(nal_end - nal);
        int nal_type;

        if (nal_size > 0 && s->hevc) {
            nal_type = hevc_nal_type(nal);
            if (nal_type == 33) {
                *has_sps = 1;
                parse_hevc_sps(s, nal, nal_size);
            } else if (nal_type == 34) {
                parse_hevc_pps(s, nal, nal_size);
            }
            if (hevc_vcl(nal_type)) {
                int poc = 0, poc_ok = 0, idr = 0, hr;
                hr = hevc_slice_header(s, nal, nal_size, key, &poc, &poc_ok, &idr);
                if (hr == 0)
                    return;
            }
        } else if (nal_size > 0) {
            nal_type = nal[0] & 0x1f;
            if (nal_type == 7)
                *has_sps = 1;
            if (nal_type == 5) {
                *key = 1;
                return;
            }
            if (nal_type == 1) {
                *key = slice_is_i(nal, nal_size);
                return;
            }
        }
        if (!next)
            break;
        start = next;
        sc = sc2;
    }
}

static int buf_reserve(uint8_t **buf, size_t *cap, size_t need)
{
    uint8_t *p;

    if (need <= *cap)
        return 0;
    p = av_realloc(*buf, need + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!p)
        return AVERROR(ENOMEM);
    *buf = p;
    *cap = need;
    return 0;
}

static int avcc_extradata_annexb(const uint8_t *e, int n, uint8_t **dst, int *dst_size)
{
    int off, count, i, out = 0;
    uint8_t *buf;
    int cap;

    if (!e || n < 7 || e[0] != 1)
        return 0;
    cap = n + 4 * 16;
    buf = av_malloc(cap + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf)
        return AVERROR(ENOMEM);
    off = 5;
    count = e[off++] & 0x1f;
    for (i = 0; i < count; i++) {
        int len;
        if (off + 2 > n)
            break;
        len = (e[off] << 8) | e[off + 1];
        off += 2;
        if (len <= 0 || off + len > n || out > cap - 4 - len)
            break;
        AV_WB32(buf + out, 1);
        out += 4;
        memcpy(buf + out, e + off, len);
        out += len;
        off += len;
    }
    if (off < n) {
        count = e[off++];
        for (i = 0; i < count; i++) {
            int len;
            if (off + 2 > n)
                break;
            len = (e[off] << 8) | e[off + 1];
            off += 2;
            if (len <= 0 || off + len > n || out > cap - 4 - len)
                break;
            AV_WB32(buf + out, 1);
            out += 4;
            memcpy(buf + out, e + off, len);
            out += len;
            off += len;
        }
    }
    if (out <= 0) {
        av_free(buf);
        return 0;
    }
    memset(buf + out, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    *dst = buf;
    *dst_size = out;
    return 1;
}

static int hvcc_extradata_annexb(const uint8_t *e, int n, uint8_t **dst, int *dst_size)
{
    int off, arrays, a, out = 0;
    uint8_t *buf;
    int cap;

    if (!e || n < 23 || e[0] != 1)
        return 0;
    cap = n + 4 * 64;
    buf = av_malloc(cap + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf)
        return AVERROR(ENOMEM);
    arrays = e[22];
    off = 23;
    for (a = 0; a < arrays; a++) {
        int count, i;
        if (off + 3 > n)
            break;
        off++;
        count = (e[off] << 8) | e[off + 1];
        off += 2;
        for (i = 0; i < count; i++) {
            int len;
            if (off + 2 > n)
                break;
            len = (e[off] << 8) | e[off + 1];
            off += 2;
            if (len <= 0 || off + len > n || out > cap - 4 - len)
                break;
            AV_WB32(buf + out, 1);
            out += 4;
            memcpy(buf + out, e + off, len);
            out += len;
            off += len;
        }
    }
    if (out <= 0) {
        av_free(buf);
        return 0;
    }
    memset(buf + out, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    *dst = buf;
    *dst_size = out;
    return 1;
}

static int avcc_to_annexb(McBsf *s, const uint8_t *src, int src_len,
                          uint8_t **dst, size_t *dst_size)
{
    int off = 0, out = 0;
    int ret;
    size_t need = (size_t)src_len + (size_t)src_len / 2 + 64;

    ret = buf_reserve(&s->in_au, &s->in_cap, need);
    if (ret < 0)
        return ret;
    while (off + s->nal_len_size <= src_len) {
        uint32_t nal_size = 0;
        int i;

        for (i = 0; i < s->nal_len_size; i++)
            nal_size = (nal_size << 8) | src[off + i];
        off += s->nal_len_size;
        if (nal_size > (uint32_t)(src_len - off))
            return AVERROR_INVALIDDATA;
        if ((size_t)out + 4 + nal_size > s->in_cap) {
            ret = buf_reserve(&s->in_au, &s->in_cap, (size_t)out + 4 + nal_size);
            if (ret < 0)
                return ret;
        }
        AV_WB32(s->in_au + out, 1);
        out += 4;
        memcpy(s->in_au + out, src + off, nal_size);
        out += nal_size;
        off += nal_size;
    }
    if (out <= 0)
        return AVERROR_INVALIDDATA;
    memset(s->in_au + out, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    *dst = s->in_au;
    *dst_size = (size_t)out;
    return 0;
}

static int load_au(McBsf *s, const AVPacket *pkt,
                   const uint8_t **au, size_t *au_size, int *key)
{
    const uint8_t *src;
    size_t src_size;
    int has_sps, ret;

    if (start_code_len(pkt->data, pkt->size)) {
        src = pkt->data;
        src_size = (size_t)pkt->size;
    } else if (s->nal_len_size) {
        uint8_t *dst = NULL;
        ret = avcc_to_annexb(s, pkt->data, pkt->size, &dst, &src_size);
        if (ret < 0)
            return ret;
        src = dst;
    } else {
        return AVERROR_INVALIDDATA;
    }

    au_scan(s, src, (int)src_size, key, &has_sps);
    if (*key && !has_sps && s->ps && s->ps_size > 0) {
        size_t need = (size_t)s->ps_size + src_size;
        size_t off = 0;
        int internal = s->in_au && src >= s->in_au && src < s->in_au + s->in_cap;

        if (internal)
            off = (size_t)(src - s->in_au);
        ret = buf_reserve(&s->in_au, &s->in_cap, need);
        if (ret < 0)
            return ret;
        if (internal)
            src = s->in_au + off;
        if (internal)
            memmove(s->in_au + s->ps_size, src, src_size);
        else
            memcpy(s->in_au + s->ps_size, src, src_size);
        memcpy(s->in_au, s->ps, s->ps_size);
        memset(s->in_au + need, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        src = s->in_au;
        src_size = need;
    }
    *au = src;
    *au_size = src_size;
    return 0;
}

static int put_packet(AVPacket *pkt, const uint8_t *src, int n)
{
    int grow = n - pkt->size;
    int ret = grow > 0 ? av_grow_packet(pkt, grow) : av_packet_make_writable(pkt);

    if (ret < 0)
        return ret;
    memcpy(pkt->data, src, n);
    pkt->size = n;
    memset(pkt->data + n, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    return 0;
}

static int mc_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    McBsf *s = ctx->priv_data;
    mc_streaming_input_t in;
    mc_streaming_output_t out;
    const uint8_t *au_data;
    size_t au_size, ysz, need;
    int key, rc, ret;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    ret = load_au(s, pkt, &au_data, &au_size, &key);
    if (ret < 0)
        goto fail;

    ysz = (size_t)s->width * (size_t)s->height;
    memset(&in, 0, sizeof(in));
    in.width = s->width;
    in.height = s->height;
    in.frame_type = key;
    in.bs = au_data;
    in.au_size = au_size;
    if (!s->dec_pic_reenc) {
        int64_t disp = au_size > INT_MAX ? -1 :
                       au_display_index(s, au_data, (int)au_size);
        if (disp < 0) {
            if (!s->poc_fallback_warned) {
                av_log(ctx, AV_LOG_WARNING,
                       "POC unavailable; pairing yuv in packet order\n");
                s->poc_fallback_warned = 1;
            }
            disp = s->index;
        }
        ret = read_display_frame(s, disp);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "yuv read failed at display frame %" PRId64 "\n", disp);
            goto fail;
        }
        in.y = s->frame;
        in.u = s->frame + ysz;
        in.v = s->frame + ysz + ysz / 4;
        in.stride_y = s->width;
        in.stride_u = s->width / 2;
        in.stride_v = s->width / 2;
    }

    need = au_size * 2 + 4096;
    if (s->frame_bytes && need < s->frame_bytes)
        need = s->frame_bytes;
    ret = buf_reserve(&s->out_au, &s->out_cap, need);
    if (ret < 0)
        goto fail;
    out.bs = s->out_au;
    out.bs_size = s->out_cap;
    rc = mc_streaming_enable(&s->mcs, &in, &out);
    if (out.bs_size == 0 || out.bs_size > INT_MAX) {
        av_log(ctx, AV_LOG_ERROR, "mc_streaming produced no AU (%d) at frame %" PRId64 "\n",
               rc, s->index);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    ret = put_packet(pkt, s->out_au, (int)out.bs_size);
    if (ret < 0)
        goto fail;
    s->index++;
    return 0;

fail:
    av_packet_unref(pkt);
    return ret;
}

static int mc_init(AVBSFContext *ctx)
{
    McBsf *s = ctx->priv_data;
    mc_streaming_ctrl_params_t ctrl;
    int w, h, ret;
    uint64_t y, bytes;

    s->hevc = s->mcs_codec >= MCS_H265_ZERO_DELAY_8BIT;
    if (ctx->par_in->codec_id == AV_CODEC_ID_HEVC && !s->hevc)
        av_log(ctx, AV_LOG_WARNING,
               "HEVC input with an H.264 mcs_codec; set mcs_codec=h265_zero_delay or h265_8bit\n");
    if (ctx->par_in->codec_id == AV_CODEC_ID_H264 && s->hevc)
        av_log(ctx, AV_LOG_WARNING,
               "H.264 input with an H.265 mcs_codec\n");
    av_log(ctx, AV_LOG_INFO, "mc_streaming %s codec=%d jnd=%d reenc=%d\n",
           mc_streaming_get_version(), s->mcs_codec, s->jnd_enable ? 1 : 0,
           s->dec_pic_reenc ? 1 : 0);

    if (!s->dec_pic_reenc && (!s->yuv_path || !s->yuv_path[0])) {
        av_log(ctx, AV_LOG_ERROR,
               "yuv=<I420 file> is required when dec_pic_reenc=0\n");
        return AVERROR(EINVAL);
    }
    w = s->width > 0 ? s->width : ctx->par_in->width;
    h = s->height > 0 ? s->height : ctx->par_in->height;
    if (w <= 0 || h <= 0 || (w & 1) || (h & 1) ||
        w > MC_BSF_MAX_DIM || h > MC_BSF_MAX_DIM) {
        av_log(ctx, AV_LOG_ERROR, "unsupported size %dx%d\n", w, h);
        return AVERROR(EINVAL);
    }
    y = (uint64_t)w * (uint64_t)h;
    bytes = y + y / 2;
    if (bytes > SIZE_MAX)
        return AVERROR(EINVAL);
    s->width = w;
    s->height = h;
    if (!s->dec_pic_reenc) {
        s->frame_bytes = (size_t)bytes;
        s->frame = av_malloc(s->frame_bytes);
        if (!s->frame)
            return AVERROR(ENOMEM);
        s->yuv = fopen(s->yuv_path, "rb");
        if (!s->yuv) {
            av_log(ctx, AV_LOG_ERROR, "cannot open yuv %s\n", s->yuv_path);
            return AVERROR(ENOENT);
        }
    }

    if (s->hevc && ctx->par_in->extradata && ctx->par_in->extradata_size >= 23 &&
        ctx->par_in->extradata[0] == 1) {
        s->nal_len_size = (ctx->par_in->extradata[21] & 3) + 1;
        ret = hvcc_extradata_annexb(ctx->par_in->extradata,
                                    ctx->par_in->extradata_size,
                                    &s->ps, &s->ps_size);
        if (ret < 0)
            return ret;
        s->ps_owned = s->ps != NULL;
    } else if (!s->hevc && ctx->par_in->extradata && ctx->par_in->extradata_size >= 5 &&
        ctx->par_in->extradata[0] == 1) {
        s->nal_len_size = (ctx->par_in->extradata[4] & 3) + 1;
        ret = avcc_extradata_annexb(ctx->par_in->extradata,
                                    ctx->par_in->extradata_size,
                                    &s->ps, &s->ps_size);
        if (ret < 0)
            return ret;
        s->ps_owned = s->ps != NULL;
    } else if (ctx->par_in->extradata &&
               start_code_len(ctx->par_in->extradata, ctx->par_in->extradata_size)) {
        s->ps = ctx->par_in->extradata;
        s->ps_size = ctx->par_in->extradata_size;
    }
    if (s->ps && s->ps_size > 0 && s->ps_size <= INT_MAX)
        note_sps(s, s->ps, (int)s->ps_size);

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.codec_type = (mc_streaming_codec_type_e)s->mcs_codec;
    ctrl.pic_csp = MCS_CSP_I420;
    ctrl.log_level = (mc_streaming_log_level_e)s->mcs_log;
    ctrl.jnd_enable = s->jnd_enable ? 1 : 0;
    ctrl.skin_skip = s->skin_skip ? 1 : 0;
    ctrl.jnd_strength_level = s->jnd_strength;
    ctrl.jnd_scale = s->jnd_scale > 0.f ? s->jnd_scale : 1.f;
    ctrl.dec_pic_reenc = s->dec_pic_reenc ? 1 : 0;
    ret = mc_streaming_control(&s->mcs, MCS_CMD_SET_PARAMS, &ctrl, NULL);
    if (ret == MCS_ERR_CODEC_UNAVAILABLE) {
        av_log(ctx, AV_LOG_ERROR,
               "H.265 plugin not loaded. Put libmcs_codec_h265 next to libmc_streaming or set MCS_PLUGIN_PATH.\n");
        return AVERROR_EXTERNAL;
    }
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "mc_streaming_control failed (%d)\n", ret);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static void mc_close(AVBSFContext *ctx)
{
    McBsf *s = ctx->priv_data;

    if (s->mcs) {
        mc_streaming_disable(s->mcs);
        s->mcs = NULL;
    }
    if (s->yuv) {
        fclose(s->yuv);
        s->yuv = NULL;
    }
    if (s->ps_owned)
        av_freep(&s->ps);
    s->ps_size = 0;
    av_freep(&s->frame);
    av_freep(&s->in_au);
    av_freep(&s->out_au);
    s->in_cap = s->out_cap = 0;
}

#define OFFSET(x) offsetof(McBsf, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_BSF_PARAM)

static const AVOption options[] = {
    { "yuv", "display-order I420 file; required when dec_pic_reenc=0", OFFSET(yuv_path), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { "width", "picture width; 0 uses the bitstream width", OFFSET(width), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, MC_BSF_MAX_DIM, FLAGS },
    { "height", "picture height; 0 uses the bitstream height", OFFSET(height), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, MC_BSF_MAX_DIM, FLAGS },
    { "mcs_codec", "mc_streaming profile", OFFSET(mcs_codec), AV_OPT_TYPE_INT, { .i64 = MCS_H264_ZERO_DELAY_8BIT }, 0, MCS_H265_8BIT, FLAGS, .unit = "mcs_codec" },
    { "zero_delay", "H.264 real-time, no B-frames", 0, AV_OPT_TYPE_CONST, { .i64 = MCS_H264_ZERO_DELAY_8BIT }, 0, 0, FLAGS, .unit = "mcs_codec" },
    { "h264_8bit", "H.264 with B-frames", 0, AV_OPT_TYPE_CONST, { .i64 = MCS_H264_8BIT }, 0, 0, FLAGS, .unit = "mcs_codec" },
    { "h265_zero_delay", "H.265 real-time, requires the mcs_codec_h265 plugin", 0, AV_OPT_TYPE_CONST, { .i64 = MCS_H265_ZERO_DELAY_8BIT }, 0, 0, FLAGS, .unit = "mcs_codec" },
    { "h265_8bit", "H.265 with B-frames, requires the mcs_codec_h265 plugin", 0, AV_OPT_TYPE_CONST, { .i64 = MCS_H265_8BIT }, 0, 0, FLAGS, .unit = "mcs_codec" },
    { "jnd", "1 runs JND on the decoded picture", OFFSET(jnd_enable), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "jnd_strength", "H.264 0..7; H.265 -1 auto or 0..2", OFFSET(jnd_strength), AV_OPT_TYPE_INT, { .i64 = 0 }, -1, 7, FLAGS },
    { "jnd_scale", "extra JND scale; 0 is treated as 1", OFFSET(jnd_scale), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0, 16, FLAGS },
    { "skin_skip", "1 skips JND on skin blocks", OFFSET(skin_skip), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "dec_pic_reenc", "1 re-encodes from the decoded picture and does not read yuv", OFFSET(dec_pic_reenc), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "mcs_log", "mc_streaming log level", OFFSET(mcs_log), AV_OPT_TYPE_INT, { .i64 = MCS_LOG_ERROR }, MCS_LOG_NONE, MCS_LOG_DEBUG, FLAGS, .unit = "mcs_log" },
    { "none", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MCS_LOG_NONE }, 0, 0, FLAGS, .unit = "mcs_log" },
    { "error", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MCS_LOG_ERROR }, 0, 0, FLAGS, .unit = "mcs_log" },
    { "warning", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MCS_LOG_WARNING }, 0, 0, FLAGS, .unit = "mcs_log" },
    { "info", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MCS_LOG_INFO }, 0, 0, FLAGS, .unit = "mcs_log" },
    { "debug", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MCS_LOG_DEBUG }, 0, 0, FLAGS, .unit = "mcs_log" },
    { NULL }
};

static const AVClass mc_class = {
    .class_name = "mc_streaming",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_mc_streaming_bsf = {
    .p.name         = "mc_streaming",
    .p.codec_ids    = codec_ids,
    .p.priv_class   = &mc_class,
    .priv_data_size = sizeof(McBsf),
    .init           = mc_init,
    .close          = mc_close,
    .filter         = mc_filter,
};
