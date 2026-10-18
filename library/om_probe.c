/* om_probe: openmedia.library's own parsers (stage 0); om_probe.h says
 * what they are and the specifications they follow. Every read goes
 * through rd(), which stops at the file's end and at the budget; every
 * length and count from a file is checked before it is used, and every
 * loop has a fixed bound, so a damaged or hostile file ends in an answer
 * (perhaps a partial one), never a hang or a crash.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#define OPENMEDIA_CONSTANTS_ONLY
#include "../include/openmedia/openmedia.h"
#include "om_probe.h"

#define BIG 0xFFFFFFFFUL
#define NOSIZE 0xFFFFFFFFFFFFFFFFULL

/* ---- bytes ---- */

static uint32_t be16(const unsigned char *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be24(const unsigned char *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
static uint32_t be32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t be64(const unsigned char *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
static uint32_t le16(const unsigned char *p) { return ((uint32_t)p[1] << 8) | p[0]; }
static uint32_t le24(const unsigned char *p) { return ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }
static uint32_t le32(const unsigned char *p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }
static uint64_t le64(const unsigned char *p) { return ((uint64_t)le32(p + 4) << 32) | le32(p); }

static int same(const unsigned char *p, const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != (unsigned char)s[i])
            return 0;
    return 1;
}

static void zero(void *p, uint32_t n)
{
    unsigned char *c = p;
    while (n--)
        *c++ = 0;
}

static void put_name(char *dst, const char *src)
{
    int i = 0;
    while (src && src[i] && i < OM_NAME - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* A four-letter code as text, printable characters only, trailing spaces gone. */
static void fourcc_name(char *dst, uint32_t code)
{
    int i, n = 0;
    for (i = 24; i >= 0; i -= 8) {
        char c = (char)((code >> i) & 0xFF);
        dst[n++] = c >= 32 && c < 127 ? c : '.';
    }
    while (n > 0 && dst[n - 1] == ' ')
        n--;
    dst[n] = 0;
}

static void hex_name(char *dst, const char *prefix, uint32_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    int n = 0, i;
    while (prefix[n] && n < 16) {
        dst[n] = prefix[n];
        n++;
    }
    for (i = 12; i >= 0; i -= 4)
        dst[n++] = hx[(v >> i) & 15];
    dst[n] = 0;
}

/* a * m / d, without overflowing, capped at 32 bits; 0 when d is 0 */
static uint32_t mdiv(uint64_t a, uint64_t m, uint64_t d)
{
    uint64_t r;
    if (!d)
        return 0;
    if (!m || a <= 0xFFFFFFFFFFFFFFFFULL / m)
        r = a * m / d;
    else
        r = a / d * m + (a % d) * m / d;
    return r > BIG ? BIG : (uint32_t)r;
}

static uint32_t gcd32(uint32_t a, uint32_t b)
{
    int n = 0;
    while (b && n++ < 64) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static void set_fps(om_info *o, uint32_t num, uint32_t den)
{
    uint32_t g;
    if (!num || !den)
        return;
    g = gcd32(num, den);
    if (g > 1) {
        num /= g;
        den /= g;
    }
    o->fps_num = num;
    o->fps_den = den;
}

/* An IEEE 754 number (its 4 or 8 bytes' bits in v) times mul, rounded down.
 * 0 for negatives, infinities and NaN; capped at 2^62. No floating point:
 * the library runs on any 68k, FPU or not. */
static uint64_t float_mul(uint64_t v, int bytes, uint32_t mul)
{
    uint64_t mant, hi, lo, cap = 1ULL << 62;
    int exp, s;
    if (bytes == 4) {
        uint32_t f = (uint32_t)v;
        if (f >> 31)
            return 0;
        exp = (int)((f >> 23) & 0xFF);
        mant = f & 0x7FFFFF;
        if (exp == 0xFF)
            return 0;
        if (exp == 0) {
            if (!mant)
                return 0;
            exp = 1;
        } else
            mant |= 0x800000;
        exp -= 127 + 23;
    } else if (bytes == 8) {
        if (v >> 63)
            return 0;
        exp = (int)((v >> 52) & 0x7FF);
        mant = v & 0xFFFFFFFFFFFFFULL;
        if (exp == 0x7FF)
            return 0;
        if (exp == 0) {
            if (!mant)
                return 0;
            exp = 1;
        } else
            mant |= 1ULL << 52;
        exp -= 1023 + 52;
    } else
        return 0;
    /* mant * mul * 2^exp, as (hi * 2^26 + lo) * 2^exp */
    hi = (mant >> 26) * mul;
    lo = (mant & 0x3FFFFFF) * mul;
    if (exp >= 0) {
        if (exp + 26 >= 62 || hi >= (1ULL << (62 - 26 - exp)) || lo >= (1ULL << (62 - exp)))
            return cap;
        return (hi << (26 + exp)) + (lo << exp);
    }
    s = -exp;
    if (s >= 26) {
        uint64_t a = s - 26 >= 64 ? 0 : hi >> (s - 26), b = s >= 64 ? 0 : lo >> s;
        return a + b;
    }
    if (hi >= (1ULL << (62 - (26 - s))))
        return cap;
    return (hi << (26 - s)) + (lo >> s);
}

/* ---- reading, within the budget ---- */

/* n bytes at pos into buf: what om_probe's first read already holds comes
 * from it, free; the rest is read and counted against the budget. */
static long rd(om_src *s, uint64_t pos, void *buf, long n)
{
    long got, cached = 0;
    if (n <= 0 || s->failed || pos >= s->size)
        return 0;
    if ((uint64_t)n > s->size - pos)
        n = (long)(s->size - pos);
    if (pos < s->headlen) {
        const unsigned char *from = s->head + pos;
        unsigned char *to = buf;
        long i;
        cached = (long)(s->headlen - pos);
        if (cached > n)
            cached = n;
        if (to != from)                 /* no parser reads into the head buffer itself */
            for (i = 0; i < cached; i++)
                to[i] = from[i];
        if (cached == n)
            return n;
        pos += (uint64_t)cached;
        buf = (unsigned char *)buf + cached;
        n -= cached;
    }
    if ((uint32_t)n > s->budget) {
        s->spent = 1;
        n = (long)s->budget;
        if (n <= 0)
            return cached;
    }
    got = s->read_at(s->user, pos, buf, n);
    if (got < 0) {
        s->failed = 1;
        return cached;
    }
    if (got > n)
        got = n;
    s->budget -= (uint32_t)got;
    return cached + got;
}

/* Exactly n bytes, or 0. */
static int rdx(om_src *s, uint64_t pos, void *buf, long n)
{
    return rd(s, pos, buf, n) == n;
}

/* ---- codecs ---- */

static const struct { uint32_t code; const char *name; } codec_names[] = {
    { OMC_H264, "H.264" }, { OMC_HEVC, "HEVC" }, { OMC_AV1, "AV1" }, { OMC_VP8, "VP8" }, { OMC_VP9, "VP9" },
    { OMC_MPEG1V, "MPEG-1 video" }, { OMC_MPEG2V, "MPEG-2 video" }, { OMC_MPEG4V, "MPEG-4 video" },
    { OMC_H263, "H.263" }, { OMC_THEORA, "Theora" }, { OMC_MJPEG, "Motion JPEG" }, { OMC_PRORES, "ProRes" },
    { OMC_VC1, "VC-1" }, { OMC_RAWVIDEO, "Uncompressed video" }, { OMC_ILBM, "IFF ILBM" },
    { OMC_PCM, "PCM" }, { OMC_PCMFLOAT, "PCM (float)" }, { OMC_ALAW, "A-law" }, { OMC_ULAW, "u-law" },
    { OMC_ADPCM, "ADPCM" }, { OMC_FIBDELTA, "Fibonacci delta" }, { OMC_MP1, "MPEG audio layer 1" },
    { OMC_MP2, "MPEG audio layer 2" }, { OMC_MP3, "MP3" }, { OMC_AAC, "AAC" }, { OMC_AC3, "AC-3" },
    { OMC_EAC3, "E-AC-3" }, { OMC_DTS, "DTS" }, { OMC_VORBIS, "Vorbis" }, { OMC_OPUS, "Opus" },
    { OMC_FLAC, "FLAC" }, { OMC_ALAC, "ALAC" }, { OMC_SPEEX, "Speex" },
};

const char *om_codec_name(uint32_t codec)
{
    unsigned i;
    for (i = 0; i < sizeof codec_names / sizeof codec_names[0]; i++)
        if (codec_names[i].code == codec)
            return codec_names[i].name;
    return 0;
}

static void set_vcodec(om_info *o, uint32_t c, const char *other)
{
    o->vcodec = c ? c : OMC_OTHER;
    put_name(o->vcodec_name, c ? om_codec_name(c) : other);
}

static void set_acodec(om_info *o, uint32_t c, const char *other)
{
    o->acodec = c ? c : OMC_OTHER;
    put_name(o->acodec_name, c ? om_codec_name(c) : other);
}

static void set_format(om_info *o, uint32_t kind, uint32_t container, const char *name)
{
    o->kind = kind;
    o->container = container;
    put_name(o->format, name);
}

/* A WAVE format tag (WAV, AVI): OMC_, or 0 when it isn't one we name. */
static uint32_t wave_codec(uint32_t tag)
{
    switch (tag) {
    case 0x0001: return OMC_PCM;
    case 0x0003: return OMC_PCMFLOAT;
    case 0x0006: return OMC_ALAW;
    case 0x0007: return OMC_ULAW;
    case 0x0002: case 0x0011: return OMC_ADPCM;
    case 0x0050: return OMC_MP2;
    case 0x0055: return OMC_MP3;
    case 0x00FF: case 0x1610: case 0x4143: case 0xA106: return OMC_AAC;
    case 0x2000: return OMC_AC3;
    case 0x2001: return OMC_DTS;
    case 0x674F: case 0x6750: case 0x6751: case 0x676F: case 0x6770: case 0x6771: return OMC_VORBIS;
    case 0x704F: return OMC_OPUS;
    case 0xF1AC: return OMC_FLAC;
    }
    return 0;
}

static void set_wave_acodec(om_info *o, uint32_t tag)
{
    char n[OM_NAME];
    hex_name(n, "format 0x", tag);
    set_acodec(o, wave_codec(tag), n);
}

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int fourcc_is(uint32_t code, const char *s)
{
    int i;
    for (i = 0; i < 4; i++)
        if (upper((int)((code >> (24 - 8 * i)) & 0xFF)) != upper(s[i]))
            return 0;
    return 1;
}

/* A video four-letter code (AVI's biCompression, written as its bytes). */
static uint32_t vfw_codec(uint32_t c)
{
    static const struct { const char *cc; uint32_t codec; } t[] = {
        { "H264", OMC_H264 }, { "X264", OMC_H264 }, { "AVC1", OMC_H264 }, { "DAVC", OMC_H264 },
        { "HEVC", OMC_HEVC }, { "H265", OMC_HEVC }, { "HVC1", OMC_HEVC }, { "HEV1", OMC_HEVC },
        { "XVID", OMC_MPEG4V }, { "DIVX", OMC_MPEG4V }, { "DX50", OMC_MPEG4V }, { "FMP4", OMC_MPEG4V },
        { "MP4V", OMC_MPEG4V }, { "MJPG", OMC_MJPEG }, { "VP80", OMC_VP8 }, { "VP90", OMC_VP9 },
        { "AV01", OMC_AV1 }, { "MPG1", OMC_MPEG1V }, { "MPG2", OMC_MPEG2V }, { "MPEG", OMC_MPEG1V },
        { "H263", OMC_H263 }, { "WVC1", OMC_VC1 }, { "THEO", OMC_THEORA },
    };
    unsigned i;
    if (c == 0 || fourcc_is(c, "RAW ") || fourcc_is(c, "DIB "))
        return OMC_RAWVIDEO;
    for (i = 0; i < sizeof t / sizeof t[0]; i++)
        if (fourcc_is(c, t[i].cc))
            return t[i].codec;
    return 0;
}

static void set_vfw_vcodec(om_info *o, uint32_t c)
{
    char n[OM_NAME];
    fourcc_name(n, c);
    set_vcodec(o, vfw_codec(c), n);
}

/* ---- elementary streams: MPEG audio, ADTS AAC, AC-3, MPEG video, H.264 ---- */

typedef struct mpa {
    int version;                /* 1 MPEG-1, 2 MPEG-2, 25 MPEG-2.5 */
    int layer;                  /* 1, 2 or 3 */
    uint32_t kbps, rate, len, spf, channels;
    int mono;
} mpa;

/* An MPEG audio frame header (ISO/IEC 11172-3 2.4.1.3, 13818-3): 1 if it is one. */
static int mpa_header(const unsigned char *p, mpa *m)
{
    static const unsigned short br[2][3][16] = {
        { { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 },
          { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 },
          { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 } },
        { { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 },
          { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 },
          { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 } } };
    static const unsigned short sr[3][3] = { { 44100, 48000, 32000 }, { 22050, 24000, 16000 }, { 11025, 12000, 8000 } };
    uint32_t h = be32(p);
    int v, l, bi, si, pad;
    if ((h >> 21) != 0x7FF)
        return 0;
    v = (int)((h >> 19) & 3);
    l = (int)((h >> 17) & 3);
    bi = (int)((h >> 12) & 15);
    si = (int)((h >> 10) & 3);
    if (v == 1 || l == 0 || bi == 0 || bi == 15 || si == 3)
        return 0;
    pad = (int)((h >> 9) & 1);
    m->version = v == 3 ? 1 : v == 2 ? 2 : 25;
    m->layer = 4 - l;
    m->kbps = br[m->version == 1 ? 0 : 1][m->layer - 1][bi];
    m->rate = sr[m->version == 1 ? 0 : m->version == 2 ? 1 : 2][si];
    m->mono = ((h >> 6) & 3) == 3;
    m->channels = m->mono ? 1 : 2;
    if (m->layer == 1) {
        m->spf = 384;
        m->len = (12 * m->kbps * 1000 / m->rate + pad) * 4;
    } else {
        m->spf = m->layer == 3 && m->version != 1 ? 576 : 1152;
        m->len = (m->spf / 8) * m->kbps * 1000 / m->rate + pad;
    }
    return m->len >= 4 && m->kbps > 0;
}

static uint32_t mpa_codec(const mpa *m) { return m->layer == 1 ? OMC_MP1 : m->layer == 2 ? OMC_MP2 : OMC_MP3; }

static const uint32_t aac_rates[16] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                                         16000, 12000, 11025, 8000, 7350, 0, 0, 0 };

/* An ADTS header (ISO/IEC 13818-7 6.2): 1 if it is one; its rate, channels and length. */
static int adts_header(const unsigned char *p, uint32_t *rate, uint32_t *ch, uint32_t *len)
{
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0)
        return 0;
    *rate = aac_rates[(p[2] >> 2) & 15];
    *ch = ((uint32_t)(p[2] & 1) << 2) | (p[3] >> 6);
    *len = ((uint32_t)(p[3] & 3) << 11) | ((uint32_t)p[4] << 3) | (p[5] >> 5);
    return *rate && *len >= 7;
}

/* An AC-3 or E-AC-3 sync frame's start (ATSC A/52; p[0..7]): its rate and
 * channels, the LFE channel counted; 0 if none. */
static uint32_t ac3_header(const unsigned char *p, uint32_t *rate, uint32_t *ch)
{
    static const unsigned char chans[8] = { 2, 1, 2, 3, 3, 4, 4, 5 };
    static const uint32_t r[3] = { 48000, 44100, 32000 };
    uint32_t bsid;
    if (p[0] != 0x0B || p[1] != 0x77)
        return 0;
    bsid = p[5] >> 3;
    if (bsid <= 10) {
        /* byte 6 on: acmod (3 bits), cmixlev (2) for a 3-front-channel
         * mode, surmixlev (2) with surrounds, dsurmod (2) for 2/0, lfeon */
        uint32_t acmod = p[6] >> 5, bit = 3, w = ((uint32_t)p[6] << 8) | p[7];
        if ((p[4] >> 6) == 3)
            return 0;
        if ((acmod & 1) && acmod != 1)
            bit += 2;
        if (acmod & 4)
            bit += 2;
        if (acmod == 2)
            bit += 2;
        *rate = r[p[4] >> 6];
        *ch = chans[acmod] + ((w >> (15 - bit)) & 1);
        return OMC_AC3;
    }
    if (bsid <= 16) {
        /* byte 4: fscod (2), fscod2 or numblkscod (2), acmod (3), lfeon (1) */
        uint32_t fscod = p[4] >> 6, fscod2 = (p[4] >> 4) & 3;
        if (fscod == 3 && fscod2 == 3)
            return 0;                   /* reserved */
        *rate = fscod == 3 ? r[fscod2] / 2 : r[fscod];
        *ch = chans[(p[4] >> 1) & 7] + (p[4] & 1);
        return OMC_EAC3;
    }
    return 0;
}

/* An MPEG-1 or MPEG-2 video sequence header at p (after 00 00 01 B3; 8 bytes). */
static void mpv_seq(om_info *o, const unsigned char *p)
{
    static const unsigned short fr[9][2] = { { 0, 0 }, { 24000, 1001 }, { 24, 1 }, { 25, 1 }, { 30000, 1001 },
                                             { 30, 1 }, { 50, 1 }, { 60000, 1001 }, { 60, 1 } };
    uint32_t frc = p[3] & 15;
    o->width = ((uint32_t)p[0] << 4) | (p[1] >> 4);
    o->height = ((uint32_t)(p[1] & 15) << 8) | p[2];
    if (frc >= 1 && frc <= 8)
        set_fps(o, fr[frc][0], fr[frc][1]);
}

/* Finds 00 00 01 code in p[0..n) from i: its position, or -1. */
static long find_code(const unsigned char *p, long n, long i, int code)
{
    for (; i + 3 < n; i++)
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && (code < 0 || p[i + 3] == code))
            return i;
    return -1;
}

/* MPEG video in p[0..n): its size and frame rate; MPEG-2 when a sequence extension follows. */
static int mpv_scan(om_info *o, const unsigned char *p, long n)
{
    long i = find_code(p, n, 0, 0xB3), j;
    if (i < 0 || i + 12 > n)
        return 0;
    mpv_seq(o, p + i + 4);
    for (j = i + 4; (j = find_code(p, n, j, 0xB5)) >= 0 && j + 5 <= n && j < i + 4096; j++)
        if ((p[j + 4] >> 4) == 1)
            return 2;
    return 1;
}

/* ---- H.264's sequence parameter set (ITU-T H.264 7.3.2.1.1) ---- */

typedef struct bitrd { const unsigned char *p; uint32_t n, pos; int err; } bitrd;

static uint32_t ub(bitrd *b, int k)
{
    uint32_t v = 0;
    while (k-- > 0) {
        if (b->pos >= b->n * 8) {
            b->err = 1;
            return 0;
        }
        v = (v << 1) | ((b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1);
        b->pos++;
    }
    return v;
}

static uint32_t ue(bitrd *b)
{
    int z = 0;
    while (!b->err && ub(b, 1) == 0)
        if (++z > 31) {
            b->err = 1;
            return 0;
        }
    if (b->err)
        return 0;
    return (z ? ((1UL << z) - 1) + ub(b, z) : 0);
}

static int32_t se(bitrd *b)
{
    uint32_t k = ue(b);
    return k & 1 ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
}

/* nal: an SPS NAL unit (its header byte first), n bytes. Fills the size; 1 if it read. */
static int h264_sps(om_info *o, const unsigned char *nal, long n)
{
    unsigned char r[256];
    uint32_t rn = 0, prof, chroma = 1, wmbs, hmap, frame_only, cl = 0, cr = 0, ct = 0, cb = 0, w, h, cx, cy;
    long i;
    int zeros = 0;
    bitrd b;
    /* the payload without emulation prevention bytes (00 00 03) */
    for (i = 1; i < n && rn < sizeof r; i++) {
        if (zeros >= 2 && nal[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = nal[i] == 0 ? zeros + 1 : 0;
        r[rn++] = nal[i];
    }
    b.p = r;
    b.n = rn;
    b.pos = 0;
    b.err = 0;
    prof = ub(&b, 8);
    ub(&b, 16);                 /* constraints, level */
    ue(&b);                     /* seq_parameter_set_id */
    if (prof == 100 || prof == 110 || prof == 122 || prof == 244 || prof == 44 || prof == 83 || prof == 86 ||
        prof == 118 || prof == 128 || prof == 138 || prof == 139 || prof == 134 || prof == 135) {
        chroma = ue(&b);
        if (chroma == 3)
            ub(&b, 1);
        ue(&b);
        ue(&b);                 /* bit depths */
        ub(&b, 1);
        if (ub(&b, 1)) {        /* scaling matrices */
            int lists = chroma != 3 ? 8 : 12, k;
            for (k = 0; k < lists && !b.err; k++)
                if (ub(&b, 1)) {
                    int size = k < 6 ? 16 : 64, j;
                    int32_t last = 8, next = 8;
                    for (j = 0; j < size && !b.err; j++) {
                        if (next)
                            next = (last + se(&b) + 256) % 256;
                        last = next ? next : last;
                    }
                }
        }
    }
    ue(&b);                     /* log2_max_frame_num_minus4 */
    i = ue(&b);                 /* pic_order_cnt_type */
    if (i == 0)
        ue(&b);
    else if (i == 1) {
        uint32_t k, cycle;
        ub(&b, 1);
        se(&b);
        se(&b);
        cycle = ue(&b);
        for (k = 0; k < cycle && k < 256 && !b.err; k++)
            se(&b);
    }
    ue(&b);                     /* max_num_ref_frames */
    ub(&b, 1);
    wmbs = ue(&b);
    hmap = ue(&b);
    frame_only = ub(&b, 1);
    if (!frame_only)
        ub(&b, 1);
    ub(&b, 1);
    if (ub(&b, 1)) {
        cl = ue(&b);
        cr = ue(&b);
        ct = ue(&b);
        cb = ue(&b);
    }
    if (b.err || wmbs > 1024 || hmap > 1024)
        return 0;
    /* the VUI's timing (Annex E.1.1): frames a second = time_scale / (2 * num_units_in_tick) */
    if (ub(&b, 1)) {
        if (ub(&b, 1) && ub(&b, 8) == 255)
            ub(&b, 32);                         /* aspect ratio, and its own numbers */
        if (ub(&b, 1))
            ub(&b, 1);                          /* overscan */
        if (ub(&b, 1)) {
            ub(&b, 4);
            if (ub(&b, 1))
                ub(&b, 24);                     /* video signal type, colour description */
        }
        if (ub(&b, 1)) {
            ue(&b);
            ue(&b);                             /* chroma location */
        }
        if (ub(&b, 1)) {
            uint32_t tick = (ub(&b, 16) << 16), scale;
            tick |= ub(&b, 16);
            scale = ub(&b, 16) << 16;
            scale |= ub(&b, 16);
            if (!b.err && tick && scale && tick < 0x80000000UL)
                set_fps(o, scale, tick * 2);
        }
    }
    cx = chroma == 0 || chroma == 3 ? 1 : 2;
    cy = (chroma == 1 ? 2 : 1) * (2 - frame_only);
    w = (wmbs + 1) * 16;
    h = (2 - frame_only) * (hmap + 1) * 16;
    if ((cl + cr) * cx < w)
        w -= (cl + cr) * cx;
    if ((ct + cb) * cy < h)
        h -= (ct + cb) * cy;
    o->width = w;
    o->height = h;
    if (!frame_only)
        o->flags |= OMIF_INTERLACED;
    return 1;
}

/* The first H.264 SPS in p[0..n) (Annex B start codes). */
static int h264_scan(om_info *o, const unsigned char *p, long n)
{
    long i;
    for (i = 0; (i = find_code(p, n, i, -1)) >= 0; i += 3)
        if ((p[i + 3] & 0x1F) == 7)
            return h264_sps(o, p + i + 3, n - i - 3);
    return 0;
}

/* nal: an HEVC SPS NAL unit (its two header bytes first), n bytes (ITU-T H.265 7.3.2.2). */
static int hevc_sps(om_info *o, const unsigned char *nal, long n)
{
    unsigned char r[256], pp[8], lp[8];
    uint32_t rn = 0, maxsub, i, chroma, w, h;
    long k;
    int zeros = 0;
    bitrd b;
    for (k = 2; k < n && rn < sizeof r; k++) {
        if (zeros >= 2 && nal[k] == 3) {
            zeros = 0;
            continue;
        }
        zeros = nal[k] == 0 ? zeros + 1 : 0;
        r[rn++] = nal[k];
    }
    b.p = r;
    b.n = rn;
    b.pos = 0;
    b.err = 0;
    ub(&b, 4);
    maxsub = ub(&b, 3);
    ub(&b, 1);
    /* profile_tier_level(1, maxsub): the general part is 96 bits */
    ub(&b, 32);
    ub(&b, 32);
    ub(&b, 32);
    for (i = 0; i < maxsub; i++) {
        pp[i] = (unsigned char)ub(&b, 1);
        lp[i] = (unsigned char)ub(&b, 1);
    }
    if (maxsub > 0)
        for (i = maxsub; i < 8; i++)
            ub(&b, 2);
    for (i = 0; i < maxsub; i++) {
        if (pp[i]) {
            ub(&b, 32);
            ub(&b, 32);
            ub(&b, 24);
        }
        if (lp[i])
            ub(&b, 8);
    }
    ue(&b);                     /* sps_seq_parameter_set_id */
    chroma = ue(&b);
    if (chroma == 3)
        ub(&b, 1);
    w = ue(&b);
    h = ue(&b);
    if (ub(&b, 1)) {            /* the conformance window */
        uint32_t cl = ue(&b), cr = ue(&b), ct = ue(&b), cb = ue(&b);
        uint32_t sw = chroma == 1 || chroma == 2 ? 2 : 1, sh = chroma == 1 ? 2 : 1;
        if (sw * (cl + cr) < w)
            w -= sw * (cl + cr);
        if (sh * (ct + cb) < h)
            h -= sh * (ct + cb);
    }
    if (b.err || !w || !h || w > 16384 || h > 16384)
        return 0;
    o->width = w;
    o->height = h;
    return 1;
}

static int hevc_scan(om_info *o, const unsigned char *p, long n)
{
    long i;
    for (i = 0; (i = find_code(p, n, i, -1)) >= 0; i += 3)
        if (i + 5 <= n && ((p[i + 3] >> 1) & 0x3F) == 33)
            return hevc_sps(o, p + i + 3, n - i - 3);
    return 0;
}

/* ---- RIFF: WAV, AVI, WebP ---- */

static int parse_wav(om_src *s, om_info *o, int rf64)
{
    unsigned char b[48];
    uint64_t pos = 12, end = s->size, data = NOSIZE, ds64_data = NOSIZE, samples = 0;
    uint32_t tag = 0, align = 0, avg = 0, n = 0;
    set_format(o, OMK_SOUND, OMF_WAV, rf64 ? "WAV (RF64)" : "WAV");
    o->atracks = 1;
    while (pos + 8 <= end && n++ < 256) {
        uint32_t id, size;
        if (!rdx(s, pos, b, 8))
            break;
        id = be32(b);
        size = le32(b + 4);
        if (id == OM_ID('f', 'm', 't', ' ')) {
            long k = rd(s, pos + 8, b, size < 40 ? size : 40);
            if (k >= 16) {
                tag = le16(b);
                o->channels = le16(b + 2);
                o->rate = le32(b + 4);
                avg = le32(b + 8);
                align = le16(b + 12);
                o->bits = le16(b + 14);
                if (tag == 0xFFFE && k >= 26)
                    tag = le16(b + 24);         /* WAVE_FORMAT_EXTENSIBLE: the sub-format's tag */
            }
        } else if (id == OM_ID('f', 'a', 'c', 't') && size >= 4 && rdx(s, pos + 8, b, 4)) {
            samples = le32(b);
        } else if (id == OM_ID('d', 's', '6', '4') && size >= 24 && rdx(s, pos + 8, b, 24)) {
            ds64_data = le64(b + 8);
            samples = le64(b + 16);
        } else if (id == OM_ID('d', 'a', 't', 'a')) {
            data = rf64 && size == 0xFFFFFFFFUL && ds64_data != NOSIZE ? ds64_data : size;
            if (data > s->size - (pos + 8)) {
                data = s->size - (pos + 8);
                o->flags |= OMIF_TRUNCATED;
            }
            break;                              /* the samples: nothing after them is needed */
        }
        pos += 8 + (uint64_t)size + (size & 1);
    }
    if (!tag)
        return 1;
    set_wave_acodec(o, tag);
    if (o->acodec != OMC_PCM && o->acodec != OMC_PCMFLOAT && o->acodec != OMC_ALAW && o->acodec != OMC_ULAW)
        o->bits = 0;
    o->bitrate = avg <= BIG / 8 ? avg * 8 : 0;
    if (data != NOSIZE && o->rate) {
        if (align && (o->acodec == OMC_PCM || o->acodec == OMC_PCMFLOAT || o->acodec == OMC_ALAW || o->acodec == OMC_ULAW))
            o->duration_ms = mdiv(data / align, 1000, o->rate);
        else if (samples)
            o->duration_ms = mdiv(samples, 1000, o->rate);
        else if (avg) {
            o->duration_ms = mdiv(data, 1000, avg);
            o->flags |= OMIF_DURATION_ESTIMATED;
        }
    }
    return 1;
}

typedef struct avi {
    uint32_t type, handler, scale, rate, length;
    int in_stream;
} avi;

static void avi_walk(om_src *s, om_info *o, avi *a, uint64_t pos, uint64_t end, int depth, int *count,
                     uint32_t *usec, uint32_t *total)
{
    unsigned char b[48];
    while (pos + 8 <= end && (*count)++ < 512) {
        uint32_t id, size;
        if (!rdx(s, pos, b, 12))
            return;
        id = be32(b);
        size = le32(b + 4);
        if (id == OM_ID('L', 'I', 'S', 'T') && size >= 4) {
            uint32_t t = be32(b + 8);
            uint64_t e = pos + 8 + (uint64_t)size;
            if (e > end)
                e = end;
            if (t == OM_ID('m', 'o', 'v', 'i'))
                return;                         /* the frames: the headers are all before them */
            if (depth < 4 && (t == OM_ID('h', 'd', 'r', 'l') || t == OM_ID('s', 't', 'r', 'l') || t == OM_ID('o', 'd', 'm', 'l'))) {
                if (t == OM_ID('s', 't', 'r', 'l')) {
                    zero(a, sizeof *a);
                    a->in_stream = 1;
                }
                avi_walk(s, o, a, pos + 12, e, depth + 1, count, usec, total);
                if (t == OM_ID('s', 't', 'r', 'l')) {
                    if (a->type == OM_ID('v', 'i', 'd', 's')) {
                        if (o->vtracks++ == 0) {
                            if (a->rate && a->scale)
                                set_fps(o, a->rate, a->scale);
                            if (!o->frames)
                                o->frames = a->length;
                            if (!o->vcodec)
                                set_vfw_vcodec(o, a->handler);
                        }
                    } else if (a->type == OM_ID('a', 'u', 'd', 's'))
                        o->atracks++;
                    else if (a->type == OM_ID('t', 'x', 't', 's'))
                        o->stracks++;
                    else
                        o->otracks++;
                    a->in_stream = 0;
                }
            }
        } else if (id == OM_ID('a', 'v', 'i', 'h') && rdx(s, pos + 8, b, 40)) {
            *usec = le32(b);
            *total = le32(b + 16);
            o->width = le32(b + 32);
            o->height = le32(b + 36);
        } else if (id == OM_ID('d', 'm', 'l', 'h') && rdx(s, pos + 8, b, 4)) {
            if (le32(b) > *total)
                *total = le32(b);
        } else if (id == OM_ID('s', 't', 'r', 'h') && a->in_stream && rdx(s, pos + 8, b, 36)) {
            a->type = be32(b);
            a->handler = be32(b + 4);
            a->scale = le32(b + 20);
            a->rate = le32(b + 24);
            a->length = le32(b + 32);
        } else if (id == OM_ID('s', 't', 'r', 'f') && a->in_stream) {
            if (a->type == OM_ID('v', 'i', 'd', 's') && o->vtracks == 0 && rdx(s, pos + 8, b, 20)) {
                int32_t h = (int32_t)le32(b + 8);
                if (!o->width)
                    o->width = le32(b + 4);
                if (!o->height)
                    o->height = (uint32_t)(h < 0 ? -h : h);
                o->depth = le16(b + 14);
                set_vfw_vcodec(o, be32(b + 16));
            } else if (a->type == OM_ID('a', 'u', 'd', 's') && o->atracks == 0 && rdx(s, pos + 8, b, 16)) {
                set_wave_acodec(o, le16(b));
                o->channels = le16(b + 2);
                o->rate = le32(b + 4);
                if (o->acodec == OMC_PCM)
                    o->bits = le16(b + 14);
            }
        }
        pos += 8 + (uint64_t)size + (size & 1);
    }
}

static int parse_avi(om_src *s, om_info *o, uint64_t end)
{
    avi a;
    int count = 0;
    uint32_t usec = 0, total = 0;
    set_format(o, OMK_VIDEO, OMF_AVI, "AVI");
    zero(&a, sizeof a);
    avi_walk(s, o, &a, 12, end, 0, &count, &usec, &total);
    if (total > o->frames)
        o->frames = total;
    if (o->frames && o->fps_num)
        o->duration_ms = mdiv((uint64_t)o->frames * o->fps_den, 1000, o->fps_num);
    else if (total && usec)
        o->duration_ms = mdiv(total, usec, 1000);
    if (!o->fps_num && usec)
        set_fps(o, 1000000, usec);
    if (!o->vtracks && o->atracks)
        o->kind = OMK_SOUND;
    return 1;
}

static int parse_webp(om_src *s, om_info *o, const unsigned char *h, long hn)
{
    const unsigned char *d = h + 20;
    set_format(o, OMK_PICTURE, OMF_WEBP, "WebP");
    (void)s;
    if (hn < 30)
        return 1;
    if (same(h + 12, "VP8 ", 4) && d[3] == 0x9D && d[4] == 0x01 && d[5] == 0x2A) {
        o->width = le16(d + 6) & 0x3FFF;
        o->height = le16(d + 8) & 0x3FFF;
        o->depth = 24;
    } else if (same(h + 12, "VP8L", 4) && d[0] == 0x2F) {
        uint32_t v = le32(d + 1);
        o->width = (v & 0x3FFF) + 1;
        o->height = ((v >> 14) & 0x3FFF) + 1;
        o->depth = (v >> 28) & 1 ? 32 : 24;
        if (o->depth == 32)
            o->flags |= OMIF_ALPHA;
    } else if (same(h + 12, "VP8X", 4)) {
        o->width = le24(d + 4) + 1;
        o->height = le24(d + 7) + 1;
        o->depth = d[0] & 0x10 ? 32 : 24;
        if (d[0] & 0x10)
            o->flags |= OMIF_ALPHA;
        if (d[0] & 0x02)
            o->flags |= OMIF_ANIMATED;
    }
    return 1;
}

/* ---- IFF: AIFF and AIFF-C, 8SVX, ANIM, ILBM ---- */

/* An 80-bit IEEE extended number (AIFF's sample rate), as a whole number. */
static uint32_t ext80(const unsigned char *p)
{
    int e = (int)(be16(p) & 0x7FFF) - 16383;
    uint64_t m = be64(p + 2);
    if ((p[0] & 0x80) || !m || e < 0 || e > 31)
        return 0;
    return (uint32_t)(m >> (63 - e));
}

static int parse_aiff(om_src *s, om_info *o, uint64_t end, int aifc)
{
    unsigned char b[32];
    uint64_t pos = 12;
    uint32_t n = 0, frames = 0;
    set_format(o, OMK_SOUND, aifc ? OMF_AIFC : OMF_AIFF, aifc ? "AIFF-C" : "AIFF");
    o->atracks = 1;
    while (pos + 8 <= end && n++ < 256) {
        uint32_t id, size;
        if (!rdx(s, pos, b, 8))
            break;
        id = be32(b);
        size = be32(b + 4);
        if (id == OM_ID('C', 'O', 'M', 'M') && size >= 18) {
            long k = rd(s, pos + 8, b, size < 22 ? size : 22);
            if (k >= 18) {
                o->channels = be16(b);
                frames = be32(b + 2);
                o->bits = be16(b + 6);
                o->rate = ext80(b + 8);
                set_acodec(o, OMC_PCM, 0);
                if (aifc && k >= 22) {
                    uint32_t c = be32(b + 18);
                    char nm[OM_NAME];
                    fourcc_name(nm, c);
                    if (c == OM_ID('N', 'O', 'N', 'E') || c == OM_ID('t', 'w', 'o', 's') || c == OM_ID('s', 'o', 'w', 't') ||
                        c == OM_ID('r', 'a', 'w', ' ') || c == OM_ID('i', 'n', '2', '4') || c == OM_ID('i', 'n', '3', '2'))
                        set_acodec(o, OMC_PCM, 0);
                    else if (fourcc_is(c, "FL32") || fourcc_is(c, "FL64"))
                        set_acodec(o, OMC_PCMFLOAT, 0);
                    else if (fourcc_is(c, "ULAW"))
                        set_acodec(o, OMC_ULAW, 0);
                    else if (fourcc_is(c, "ALAW"))
                        set_acodec(o, OMC_ALAW, 0);
                    else if (fourcc_is(c, "IMA4"))
                        set_acodec(o, OMC_ADPCM, 0);
                    else
                        set_acodec(o, 0, nm);
                    if (o->acodec != OMC_PCM && o->acodec != OMC_PCMFLOAT)
                        o->bits = 0;
                }
            }
            break;
        }
        pos += 8 + (uint64_t)size + (size & 1);
    }
    if (o->rate)
        o->duration_ms = mdiv(frames, 1000, o->rate);
    if (o->duration_ms && o->rate && o->bits && o->channels)
        o->bitrate = mdiv((uint64_t)o->rate * o->channels, o->bits, 1);
    return 1;
}

static int parse_8svx(om_src *s, om_info *o, uint64_t end, int sixteen)
{
    unsigned char b[20];
    uint64_t pos = 12, body = 0;
    uint32_t n = 0, samples = 0, comp = 0;
    set_format(o, OMK_SOUND, sixteen ? OMF_16SV : OMF_8SVX, sixteen ? "IFF 16SV" : "IFF 8SVX");
    o->atracks = 1;
    o->channels = 1;
    o->bits = sixteen ? 16 : 8;
    while (pos + 8 <= end && n++ < 256) {
        uint32_t id, size;
        if (!rdx(s, pos, b, 8))
            break;
        id = be32(b);
        size = be32(b + 4);
        if (id == OM_ID('V', 'H', 'D', 'R') && size >= 20 && rdx(s, pos + 8, b, 20)) {
            samples = be32(b) + be32(b + 4);    /* the one-shot part and the repeating part */
            if (samples < be32(b))
                samples = be32(b);
            o->rate = be16(b + 12);
            comp = b[15];
        } else if (id == OM_ID('C', 'H', 'A', 'N') && size >= 4 && rdx(s, pos + 8, b, 4)) {
            if (be32(b) == 6)
                o->channels = 2;
        } else if (id == OM_ID('B', 'O', 'D', 'Y')) {
            body = size;
            break;
        }
        pos += 8 + (uint64_t)size + (size & 1);
    }
    set_acodec(o, comp == 1 ? OMC_FIBDELTA : OMC_PCM, 0);
    if (comp == 1)
        o->bits = 0;
    if (!samples && body)
        samples = (uint32_t)(body / o->channels / (sixteen ? 2 : 1));
    if (o->rate)
        o->duration_ms = mdiv(samples, 1000, o->rate);
    if (o->rate)
        o->bitrate = o->rate * o->channels * (sixteen ? 16 : comp == 1 ? 4 : 8);
    return 1;
}

/* An ILBM's (or PBM's) BMHD and CAMG, from pos to end; stops at its BODY. */
static void ilbm_chunks(om_src *s, om_info *o, uint64_t pos, uint64_t end)
{
    unsigned char b[20];
    uint32_t n = 0;
    while (pos + 8 <= end && n++ < 128) {
        uint32_t id, size;
        if (!rdx(s, pos, b, 8))
            return;
        id = be32(b);
        size = be32(b + 4);
        if (id == OM_ID('B', 'M', 'H', 'D') && size >= 20 && rdx(s, pos + 8, b, 20)) {
            o->width = be16(b);
            o->height = be16(b + 2);
            o->depth = b[8];
        } else if (id == OM_ID('C', 'A', 'M', 'G') && size >= 4 && rdx(s, pos + 8, b, 4)) {
            uint32_t mode = be32(b);
            if (mode & 0x800)
                o->flags |= OMIF_HAM;
            else if (mode & 0x80)
                o->flags |= OMIF_EHB;
        } else if (id == OM_ID('B', 'O', 'D', 'Y') || id == OM_ID('D', 'L', 'T', 'A'))
            return;
        pos += 8 + (uint64_t)size + (size & 1);
    }
}

static int parse_anim(om_src *s, om_info *o, uint64_t end)
{
    static const char *const ops[] = { "IFF ILBM", "XOR", "Long delta", "Short delta", "General delta",
                                       "ANIM-5", "ANIM-6", "ANIM-7", "ANIM-8" };
    unsigned char b[40];
    uint64_t pos = 12, sum = 0;
    uint32_t frames = 0, first_rel = 0, n = 0;
    int op = -1;
    set_format(o, OMK_ANIMATION, OMF_ANIM, "IFF ANIM");
    o->vtracks = 1;
    while (pos + 12 <= end && n++ < 100000) {
        uint32_t size;
        if (!rdx(s, pos, b, 12))
            break;
        size = be32(b + 4);
        if (be32(b) == OM_ID('F', 'O', 'R', 'M')) {
            uint64_t e = pos + 8 + (uint64_t)size;
            if (e > end)
                e = end;
            frames++;
            if (frames == 1)
                ilbm_chunks(s, o, pos + 12, e);
            /* each frame's ANHD (it comes first in a frame): its operation and how long it shows */
            if (pos + 12 + 8 + 18 <= e && rdx(s, pos + 12, b, 8 + 18) && be32(b) == OM_ID('A', 'N', 'H', 'D')) {
                uint32_t rel = be32(b + 8 + 14);
                if (op < 0 && frames > 1)
                    op = b[8];
                if (rel && rel < 60 * 60 * 60) {
                    sum += rel;
                    if (!first_rel)
                        first_rel = rel;
                }
            }
        }
        pos += 8 + (uint64_t)size + (size & 1);
    }
    o->frames = frames;
    if (op >= 0 && op < (int)(sizeof ops / sizeof ops[0]))
        set_vcodec(o, OMC_ILBM, ops[op]);
    else if (op >= 0) {
        char nm[OM_NAME];
        hex_name(nm, "ANIM op 0x", (uint32_t)op);
        set_vcodec(o, OMC_ILBM, nm);
    } else
        set_vcodec(o, OMC_ILBM, 0);
    if (first_rel)
        set_fps(o, 60, first_rel);              /* ANHD's times are in sixtieths of a second */
    if (sum)
        o->duration_ms = mdiv(sum, 1000, 60);
    return 1;
}

static int parse_iff(om_src *s, om_info *o, const unsigned char *h)
{
    uint32_t type = be32(h + 8);
    uint64_t end = 8 + (uint64_t)be32(h + 4);
    if (end > s->size) {
        end = s->size;
        o->flags |= OMIF_TRUNCATED;
    }
    switch (type) {
    case OM_ID('A', 'I', 'F', 'F'): return parse_aiff(s, o, end, 0);
    case OM_ID('A', 'I', 'F', 'C'): return parse_aiff(s, o, end, 1);
    case OM_ID('8', 'S', 'V', 'X'): return parse_8svx(s, o, end, 0);
    case OM_ID('1', '6', 'S', 'V'): return parse_8svx(s, o, end, 1);
    case OM_ID('A', 'N', 'I', 'M'): return parse_anim(s, o, end);
    case OM_ID('I', 'L', 'B', 'M'):
    case OM_ID('P', 'B', 'M', ' '):
    case OM_ID('A', 'C', 'B', 'M'):
        set_format(o, OMK_PICTURE, OMF_ILBM, type == OM_ID('P', 'B', 'M', ' ') ? "IFF PBM" :
                   type == OM_ID('A', 'C', 'B', 'M') ? "IFF ACBM" : "IFF ILBM");
        ilbm_chunks(s, o, 12, end);
        return 1;
    }
    return 0;
}

/* ---- FLAC, MP3 and ADTS ---- */

/* FLAC's STREAMINFO (RFC 9639 8.2), 18 bytes at p (after its block header). */
static void flac_streaminfo(om_info *o, const unsigned char *p)
{
    uint64_t total;
    o->rate = ((uint32_t)p[10] << 12) | ((uint32_t)p[11] << 4) | (p[12] >> 4);
    o->channels = ((p[12] >> 1) & 7) + 1;
    o->bits = ((((uint32_t)p[12] & 1) << 4) | (p[13] >> 4)) + 1;
    total = ((uint64_t)(p[13] & 15) << 32) | be32(p + 14);
    if (o->rate && total)
        o->duration_ms = mdiv(total, 1000, o->rate);
}

static int parse_flac(om_src *s, om_info *o, uint64_t at)
{
    unsigned char b[48];
    set_format(o, OMK_SOUND, OMF_FLAC, "FLAC");
    set_acodec(o, OMC_FLAC, 0);
    o->atracks = 1;
    if (rdx(s, at, b, 4 + 4 + 18) && (b[4] & 0x7F) == 0 && be24(b + 5) >= 34)
        flac_streaminfo(o, b + 8);
    return 1;
}

/* The size of an ID3v2 tag at p (10 bytes there), 0 when none. */
static uint32_t id3v2_size(const unsigned char *p)
{
    if (!same(p, "ID3", 3) || p[3] == 0xFF || ((p[6] | p[7] | p[8] | p[9]) & 0x80))
        return 0;
    return 10 + (((uint32_t)p[6] << 21) | ((uint32_t)p[7] << 14) | ((uint32_t)p[8] << 7) | p[9]) + (p[5] & 0x10 ? 10 : 0);
}

/* MPEG audio frames from start (the header is at buf[i], with n bytes in buf from file offset base). */
static int parse_mpa(om_src *s, om_info *o, const unsigned char *buf, long n, long i, uint64_t base, const mpa *m)
{
    uint32_t frames = 0, bytes = 0;
    long side;
    uint64_t audio, end = s->size;
    unsigned char t[4];
    set_format(o, OMK_SOUND, OMF_MP3, m->layer == 3 ? "MP3" : "MPEG audio");
    set_acodec(o, mpa_codec(m), 0);
    o->atracks = 1;
    o->rate = m->rate;
    o->channels = m->channels;
    /* a Xing or Info header (VBR) in the first frame, else a VBRI header */
    side = m->version == 1 ? (m->mono ? 17 : 32) : (m->mono ? 9 : 17);
    if (i + 4 + side + 16 <= n && (same(buf + i + 4 + side, "Xing", 4) || same(buf + i + 4 + side, "Info", 4))) {
        const unsigned char *x = buf + i + 4 + side;
        uint32_t flags = be32(x + 4);
        long k = 8;
        if (flags & 1) {
            frames = be32(x + k);
            k += 4;
        }
        if ((flags & 2) && i + 4 + side + k + 4 <= n)
            bytes = be32(x + k);
        if (same(x, "Xing", 4))
            o->flags |= OMIF_VBR;
    } else if (i + 36 + 18 <= n && same(buf + i + 36, "VBRI", 4)) {
        bytes = be32(buf + i + 36 + 10);
        frames = be32(buf + i + 36 + 14);
        o->flags |= OMIF_VBR;
    }
    if (end >= 128 && rdx(s, end - 128, t, 3) && same(t, "TAG", 3))
        end -= 128;                             /* an ID3v1 tag at the end */
    audio = end > base + (uint64_t)i ? end - (base + (uint64_t)i) : 0;
    if (frames) {
        o->duration_ms = mdiv((uint64_t)frames * m->spf, 1000, m->rate);
        if (o->duration_ms)
            o->bitrate = mdiv(bytes ? bytes : audio, 8000, o->duration_ms);
    } else {
        o->bitrate = m->kbps * 1000;
        o->duration_ms = mdiv(audio, 8, m->kbps);
        o->flags |= OMIF_DURATION_ESTIMATED;
    }
    return 1;
}

static int parse_adts(om_src *s, om_info *o, const unsigned char *buf, long n, long i, uint64_t base)
{
    uint32_t rate, ch, len, frames = 0, bytes = 0, k = 0;
    long p = i;
    set_format(o, OMK_SOUND, OMF_ADTS, "AAC (ADTS)");
    set_acodec(o, OMC_AAC, 0);
    o->atracks = 1;
    if (!adts_header(buf + i, &rate, &ch, &len))
        return 1;
    o->rate = rate;
    o->channels = ch;
    /* the frames in the bytes already read give the average bit rate */
    while (p + 7 <= n && k++ < 4096 && adts_header(buf + p, &rate, &ch, &len)) {
        frames++;
        bytes += len;
        p += len;
    }
    if (frames && bytes) {
        o->bitrate = mdiv((uint64_t)bytes * 8, o->rate, (uint64_t)frames * 1024);
        if (o->bitrate)
            o->duration_ms = mdiv(s->size - base - (uint64_t)i, 8000, o->bitrate);
        o->flags |= OMIF_DURATION_ESTIMATED | OMIF_VBR;
    }
    return 1;
}

/* After any ID3v2 tag: FLAC, MPEG audio (two frames in a row) or ADTS. */
static int parse_audio_stream(om_src *s, om_info *o, unsigned char *h, long hn, unsigned char *work)
{
    uint64_t base = 0;
    unsigned char *b = h;
    long n = hn, i, lim;
    uint32_t tag = hn >= 10 ? id3v2_size(h) : 0;
    if (tag) {
        if ((uint64_t)tag >= s->size)
            return 0;
        base = tag;
        if (tag < (uint32_t)hn) {
            b = h + tag;
            n = hn - (long)tag;
        } else {
            b = work;
            n = rd(s, base, work, OM_WORK);
        }
    }
    if (n >= 4 && same(b, "fLaC", 4))
        return parse_flac(s, o, base);
    lim = tag ? (n < 8192 ? n : 8192) : 4;      /* without a tag the stream must start at once */
    for (i = 0; i < lim && i + 6 <= n; i++) {
        mpa m, m2;
        uint32_t r, c, l, r2, c2, l2;
        if (b[i] != 0xFF)
            continue;
        if (mpa_header(b + i, &m)) {
            if (i + (long)m.len + 4 <= n) {
                if (mpa_header(b + i + m.len, &m2) && m2.layer == m.layer && m2.rate == m.rate)
                    return parse_mpa(s, o, b, n, i, base, &m);
            } else if (i + (long)m.len >= n && (uint64_t)n == s->size - base)
                return parse_mpa(s, o, b, n, i, base, &m);     /* a file of one frame */
        }
        if (adts_header(b + i, &r, &c, &l) && i + (long)l + 6 <= n && adts_header(b + i + l, &r2, &c2, &l2) && r2 == r)
            return parse_adts(s, o, b, n, i, base);
    }
    return 0;
}

/* ---- pictures: PNG, JPEG, GIF, BMP ---- */

static int parse_png(om_src *s, om_info *o)
{
    unsigned char b[32];
    uint64_t pos = 8;
    uint32_t n = 0;
    set_format(o, OMK_PICTURE, OMF_PNG, "PNG");
    while (pos + 12 <= s->size && n++ < 64) {
        uint32_t len, type;
        if (!rdx(s, pos, b, 8))
            break;
        len = be32(b);
        type = be32(b + 4);
        if (len > 0x7FFFFFFFUL)
            break;
        if (type == OM_ID('I', 'H', 'D', 'R') && len >= 13 && rdx(s, pos + 8, b, 13)) {
            static const unsigned char chans[7] = { 1, 0, 3, 1, 2, 0, 4 };
            o->width = be32(b);
            o->height = be32(b + 4);
            /* bits per sample 1, 2, 4, 8 or 16, times the colour type's samples */
            o->depth = b[9] < 7 && b[8] && b[8] <= 16 && !(b[8] & (b[8] - 1)) ? b[8] * chans[b[9]] : 0;
            if (b[9] == 4 || b[9] == 6)
                o->flags |= OMIF_ALPHA;
        } else if (type == OM_ID('a', 'c', 'T', 'L') && len >= 8 && rdx(s, pos + 8, b, 8)) {
            o->frames = be32(b);
            o->flags |= OMIF_ANIMATED;
            o->format[0] = 0;
            put_name(o->format, "PNG (animated)");
        } else if (type == OM_ID('t', 'R', 'N', 'S'))
            o->flags |= OMIF_ALPHA;
        else if (type == OM_ID('I', 'D', 'A', 'T') || type == OM_ID('I', 'E', 'N', 'D'))
            break;
        pos += 12 + (uint64_t)len;
    }
    return 1;
}

static int parse_jpeg(om_src *s, om_info *o)
{
    unsigned char b[12];
    uint64_t pos = 2;
    uint32_t n = 0;
    set_format(o, OMK_PICTURE, OMF_JPEG, "JPEG");
    while (pos + 4 <= s->size && n++ < 512) {
        uint32_t m, len;
        if (!rdx(s, pos, b, 4))
            break;
        if (b[0] != 0xFF)
            break;
        m = b[1];
        if (m == 0xFF) {                        /* fill */
            pos++;
            continue;
        }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            pos += 2;
            continue;
        }
        if (m == 0xD9 || m == 0xDA)
            break;                              /* the end, or the scan: the frame header comes before it */
        len = be16(b + 2);
        if (len < 2)
            break;
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC && len >= 8 && rdx(s, pos + 4, b, 6)) {
            o->height = be16(b + 1);
            o->width = be16(b + 3);
            o->depth = (uint32_t)b[0] * b[5];
            if (m == 0xC2 || m == 0xC6 || m == 0xCA)
                put_name(o->format, "JPEG (progressive)");
            break;
        }
        pos += 2 + (uint64_t)len;
    }
    return 1;
}

static int parse_gif(om_info *o, const unsigned char *h)
{
    set_format(o, OMK_PICTURE, OMF_GIF, "GIF");
    o->width = le16(h + 6);
    o->height = le16(h + 8);
    o->depth = h[10] & 0x80 ? (h[10] & 7) + 1u : ((h[10] >> 4) & 7) + 1u;
    return 1;
}

static int parse_bmp(om_info *o, const unsigned char *h, long hn)
{
    uint32_t dib = le32(h + 14);
    if (hn < 30 || (dib != 12 && dib != 16 && dib != 40 && dib != 52 && dib != 56 && dib != 64 && dib != 108 && dib != 124))
        return 0;
    set_format(o, OMK_PICTURE, OMF_BMP, "BMP");
    if (dib == 12) {
        o->width = le16(h + 18);
        o->height = le16(h + 20);
        o->depth = le16(h + 24);
    } else {
        int32_t ht = (int32_t)le32(h + 22);
        o->width = le32(h + 18);
        o->height = (uint32_t)(ht < 0 ? -ht : ht);
        o->depth = le16(h + 28);
    }
    return 1;
}

/* ---- ISO base media files: MP4, QuickTime, 3GP, HEIF and AVIF ---- */

typedef struct bmff {
    om_src *s;
    om_info *o;
    uint32_t boxes;                     /* boxes looked at, of 4096 */
    uint32_t mv_scale;
    uint64_t mv_dur, frag_dur;
    /* the track being read */
    uint32_t handler, codec_fourcc, tw, th, vw, vh, ch, rate, bits, samples, md_scale, oti;
    uint64_t md_dur;
    int qt_v2_rate;
    uint64_t longest_ms;
    uint32_t ispe_w, ispe_h;
    unsigned char h[16], d[512];        /* a box's header and its data: one for every level (the walk recurses) */
} bmff;

/* The decoder config's objectTypeIndication in an esds box's descriptors (ISO/IEC 14496-1 7.2.6), or 0. */
static uint32_t esds_oti(const unsigned char *p, long n)
{
    long i = 4;                                 /* after the box's version and flags */
    int tag, k;
    uint32_t len;
    for (k = 0; k < 2 && i < n; k++) {
        int j;
        tag = p[i++];
        len = 0;
        for (j = 0; j < 4 && i < n; j++) {
            uint32_t c = p[i++];
            len = (len << 7) | (c & 0x7F);
            if (!(c & 0x80))
                break;
        }
        if (tag == 0x03) {                      /* ES_Descriptor */
            uint32_t fl;
            if (i + 3 > n)
                return 0;
            fl = p[i + 2];
            i += 3;
            if (fl & 0x80)
                i += 2;
            if ((fl & 0x40) && i < n)
                i += 1 + p[i];
            if (fl & 0x20)
                i += 2;
        } else if (tag == 0x04)                 /* DecoderConfigDescriptor */
            return i < n ? p[i] : 0;
        else
            return 0;
    }
    return 0;
}

/* The codec for a sample entry's four-letter code (and its esds, when it has one). */
static void bmff_codec(bmff *b, int video)
{
    static const struct { const char *cc; uint32_t codec; } t[] = {
        { "avc1", OMC_H264 }, { "avc2", OMC_H264 }, { "avc3", OMC_H264 }, { "avc4", OMC_H264 },
        { "hvc1", OMC_HEVC }, { "hev1", OMC_HEVC }, { "av01", OMC_AV1 }, { "vp08", OMC_VP8 }, { "vp09", OMC_VP9 },
        { "mp4v", OMC_MPEG4V }, { "s263", OMC_H263 }, { "h263", OMC_H263 }, { "jpeg", OMC_MJPEG },
        { "mjpa", OMC_MJPEG }, { "mjpb", OMC_MJPEG }, { "apch", OMC_PRORES }, { "apcn", OMC_PRORES },
        { "apcs", OMC_PRORES }, { "apco", OMC_PRORES }, { "ap4h", OMC_PRORES }, { "ap4x", OMC_PRORES },
        { "mp1v", OMC_MPEG1V }, { "mp2v", OMC_MPEG2V }, { "vc-1", OMC_VC1 },
        { "mp4a", OMC_AAC }, { ".mp3", OMC_MP3 }, { "ac-3", OMC_AC3 }, { "ec-3", OMC_EAC3 }, { "Opus", OMC_OPUS },
        { "fLaC", OMC_FLAC }, { "alac", OMC_ALAC }, { "lpcm", OMC_PCM }, { "sowt", OMC_PCM }, { "twos", OMC_PCM },
        { "in24", OMC_PCM }, { "in32", OMC_PCM }, { "NONE", OMC_PCM }, { "fl32", OMC_PCMFLOAT },
        { "fl64", OMC_PCMFLOAT }, { "ulaw", OMC_ULAW }, { "alaw", OMC_ALAW }, { "ima4", OMC_ADPCM },
        { "dtsc", OMC_DTS }, { "dtsh", OMC_DTS }, { "dtsl", OMC_DTS }, { "dtse", OMC_DTS },
    };
    uint32_t c = 0, i;
    char nm[OM_NAME];
    for (i = 0; i < sizeof t / sizeof t[0]; i++)
        if (b->codec_fourcc == OM_ID(t[i].cc[0], t[i].cc[1], t[i].cc[2], t[i].cc[3]))
            c = t[i].codec;
    if (b->codec_fourcc == OM_ID('r', 'a', 'w', ' '))
        c = video ? OMC_RAWVIDEO : OMC_PCM;
    if (b->codec_fourcc == OM_ID('m', 's', 0, 0x55))
        c = OMC_MP3;
    /* MPEG-4 Systems' object types (the MP4 registration authority's list) */
    if (b->codec_fourcc == OM_ID('m', 'p', '4', 'a') || b->codec_fourcc == OM_ID('m', 'p', '4', 'v')) {
        switch (b->oti) {
        case 0x40: case 0x66: case 0x67: case 0x68: c = OMC_AAC; break;
        case 0x69: case 0x6B: c = OMC_MP3; break;
        case 0xA5: c = OMC_AC3; break;
        case 0xA6: c = OMC_EAC3; break;
        case 0xA9: c = OMC_DTS; break;
        case 0xDD: c = OMC_VORBIS; break;
        case 0x20: c = OMC_MPEG4V; break;
        case 0x21: c = OMC_H264; break;
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: c = OMC_MPEG2V; break;
        case 0x6A: c = OMC_MPEG1V; break;
        case 0x6C: c = OMC_MJPEG; break;
        }
    }
    fourcc_name(nm, b->codec_fourcc);
    if (video)
        set_vcodec(b->o, c, nm);
    else
        set_acodec(b->o, c, nm);
}

/* The first sample entry of an stsd box (data: its bytes after the box header). */
static void bmff_stsd(bmff *b, const unsigned char *d, long n)
{
    const unsigned char *e = d + 8;
    long en, i;
    if (n < 16 || be32(d + 4) == 0)
        return;
    en = (long)be32(e);
    if (en > n - 8 || en < 8)
        en = n - 8;
    b->codec_fourcc = be32(e + 4);
    if (b->handler == OM_ID('v', 'i', 'd', 'e') && en >= 36) {
        b->vw = be16(e + 32);
        b->vh = be16(e + 34);
    } else if (b->handler == OM_ID('s', 'o', 'u', 'n') && en >= 36) {
        uint32_t version = be16(e + 16);
        b->ch = be16(e + 24);
        b->bits = be16(e + 26);
        b->rate = be32(e + 32) >> 16;
        if (version == 2 && en >= 60) {         /* QuickTime's version 2 sound description */
            b->rate = (uint32_t)float_mul(be64(e + 40), 8, 1);
            b->ch = be32(e + 48);
            b->bits = be32(e + 56);
        }
    }
    /* an esds box inside the entry: the object type says what mp4a or mp4v hold */
    for (i = 28; i + 8 <= en; i++)
        if (same(e + i + 4, "esds", 4) && be32(e + i) >= 12 && (long)be32(e + i) <= en - i) {
            b->oti = esds_oti(e + i + 8, (long)be32(e + i) - 8);
            break;
        }
}

static void bmff_track_end(bmff *b)
{
    om_info *o = b->o;
    uint64_t ms = b->md_scale ? (uint64_t)mdiv(b->md_dur, 1000, b->md_scale) : 0;
    if (ms > b->longest_ms)
        b->longest_ms = ms;
    if (b->handler == OM_ID('v', 'i', 'd', 'e')) {
        if (o->vtracks++ == 0) {
            bmff_codec(b, 1);
            o->width = b->vw ? b->vw : b->tw;
            o->height = b->vh ? b->vh : b->th;
            o->frames = b->samples;
            if (b->samples && b->md_dur && b->md_scale) {
                /* frames a second: samples * timescale / duration, to a thousandth */
                uint32_t mfps = mdiv((uint64_t)b->samples * b->md_scale, 1000, b->md_dur);
                if (mfps % 1000 == 0)
                    set_fps(o, mfps / 1000, 1);
                else
                    set_fps(o, mfps, 1000);
            }
        }
    } else if (b->handler == OM_ID('s', 'o', 'u', 'n')) {
        if (o->atracks++ == 0) {
            bmff_codec(b, 0);
            o->channels = b->ch;
            o->rate = b->rate;
            if (o->acodec == OMC_PCM || o->acodec == OMC_PCMFLOAT)
                o->bits = b->bits;
        }
    } else if (b->handler == OM_ID('t', 'e', 'x', 't') || b->handler == OM_ID('s', 'b', 't', 'l') ||
               b->handler == OM_ID('s', 'u', 'b', 't') || b->handler == OM_ID('c', 'l', 'c', 'p'))
        o->stracks++;
    else if (b->handler)
        o->otracks++;
}

static void bmff_walk(bmff *b, uint64_t pos, uint64_t end, int depth)
{
    unsigned char *h = b->h, *d = b->d;
    while (pos + 8 <= end && b->boxes++ < 4096) {
        uint64_t size;
        uint32_t type, hl = 8;
        long n;
        if (!rdx(b->s, pos, h, 16) && !rdx(b->s, pos, h, 8))
            return;
        size = be32(h);
        type = be32(h + 4);
        if (size == 1) {
            if (pos + 16 > end)
                return;
            size = be64(h + 8);
            hl = 16;
        } else if (size == 0)
            size = end - pos;                   /* to the end of the file */
        if (size < hl)
            return;
        if (size > end - pos) {
            b->o->flags |= OMIF_TRUNCATED;
            size = end - pos;
        }
        switch (type) {
        case OM_ID('m', 'o', 'o', 'v'): case OM_ID('m', 'd', 'i', 'a'): case OM_ID('m', 'i', 'n', 'f'):
        case OM_ID('s', 't', 'b', 'l'): case OM_ID('m', 'v', 'e', 'x'): case OM_ID('i', 'p', 'r', 'p'):
        case OM_ID('i', 'p', 'c', 'o'):
            if (depth < 8)
                bmff_walk(b, pos + hl, pos + size, depth + 1);
            break;
        case OM_ID('m', 'e', 't', 'a'):         /* a full box: its children start after version and flags */
            if (depth < 8 && size >= hl + 4)
                bmff_walk(b, pos + hl + 4, pos + size, depth + 1);
            break;
        case OM_ID('t', 'r', 'a', 'k'):
            b->handler = b->codec_fourcc = b->tw = b->th = b->vw = b->vh = 0;
            b->ch = b->rate = b->bits = b->samples = b->md_scale = b->oti = 0;
            b->md_dur = 0;
            if (depth < 8)
                bmff_walk(b, pos + hl, pos + size, depth + 1);
            bmff_track_end(b);
            break;
        case OM_ID('m', 'v', 'h', 'd'): case OM_ID('m', 'd', 'h', 'd'):
            if ((n = rd(b->s, pos + hl, d, 32)) >= 20) {
                uint32_t scale;
                uint64_t dur;
                if (d[0] == 1 && n >= 32) {
                    scale = be32(d + 20);
                    dur = be64(d + 24);
                } else {
                    scale = be32(d + 12);
                    dur = be32(d + 16);
                    if (dur == 0xFFFFFFFFUL)
                        dur = 0;
                }
                if (type == OM_ID('m', 'v', 'h', 'd')) {
                    b->mv_scale = scale;
                    b->mv_dur = dur;
                } else {
                    b->md_scale = scale;
                    b->md_dur = dur;
                }
            }
            break;
        case OM_ID('m', 'e', 'h', 'd'):
            if ((n = rd(b->s, pos + hl, d, 12)) >= 8)
                b->frag_dur = d[0] == 1 && n >= 12 ? be64(d + 4) : be32(d + 4);
            break;
        case OM_ID('t', 'k', 'h', 'd'):
            if ((n = rd(b->s, pos + hl, d, 96)) >= 84) {
                int at = d[0] == 1 ? 88 : 76;
                if (n >= at + 8) {
                    b->tw = be32(d + at) >> 16;
                    b->th = be32(d + at + 4) >> 16;
                }
            }
            break;
        case OM_ID('h', 'd', 'l', 'r'):
            /* QuickTime has a second one in minf, a data handler ('dhlr', "alis"): not the track's kind */
            if (rdx(b->s, pos + hl, d, 12) && be32(d + 4) != OM_ID('d', 'h', 'l', 'r'))
                b->handler = be32(d + 8);
            break;
        case OM_ID('s', 't', 's', 'd'):
            n = rd(b->s, pos + hl, d, size - hl < sizeof b->d ? (long)(size - hl) : (long)sizeof b->d);
            bmff_stsd(b, d, n);
            break;
        case OM_ID('s', 't', 's', 'z'): case OM_ID('s', 't', 'z', '2'):
            if (rdx(b->s, pos + hl, d, 12))
                b->samples = be32(d + 8);
            break;
        case OM_ID('i', 's', 'p', 'e'):
            if (!b->ispe_w && rdx(b->s, pos + hl, d, 12)) {
                b->ispe_w = be32(d + 4);
                b->ispe_h = be32(d + 8);
            }
            break;
        }
        pos += size;
    }
}

static int parse_bmff(om_src *s, om_info *o, const unsigned char *h, long hn, void *aux)
{
    bmff *bp = aux;
    uint32_t brand = 0;
    zero(bp, sizeof *bp);
    bp->s = s;
    bp->o = o;
    if (hn >= 12 && same(h + 4, "ftyp", 4))
        brand = be32(h + 8);
    bmff_walk(bp, 0, s->size, 0);
    if (fourcc_is(brand, "avif") || fourcc_is(brand, "avis") || fourcc_is(brand, "heic") || fourcc_is(brand, "heix") ||
        fourcc_is(brand, "mif1") || fourcc_is(brand, "msf1") || fourcc_is(brand, "heim") || fourcc_is(brand, "heis")) {
        if (!o->vtracks) {
            set_format(o, OMK_PICTURE, OMF_HEIF, fourcc_is(brand, "avif") || fourcc_is(brand, "avis") ? "AVIF" : "HEIF");
            o->width = bp->ispe_w;
            o->height = bp->ispe_h;
            return 1;
        }
    }
    if (brand == OM_ID('q', 't', ' ', ' ') || !brand)
        set_format(o, OMK_VIDEO, OMF_MOV, "QuickTime");
    else if ((brand >> 16) == (('3' << 8) | 'g'))
        set_format(o, OMK_VIDEO, OMF_3GP, "3GP");
    else
        set_format(o, OMK_VIDEO, OMF_MP4, "MP4");
    if (!o->vtracks && (o->atracks || brand == OM_ID('M', '4', 'A', ' ') || brand == OM_ID('M', '4', 'B', ' ')))
        o->kind = OMK_SOUND;
    if (bp->mv_scale && bp->mv_dur)
        o->duration_ms = mdiv(bp->mv_dur, 1000, bp->mv_scale);
    else if (bp->mv_scale && bp->frag_dur)
        o->duration_ms = mdiv(bp->frag_dur, 1000, bp->mv_scale);
    else
        o->duration_ms = (uint32_t)bp->longest_ms;
    return 1;
}

/* ---- Matroska and WebM (RFC 8794, RFC 9559) ---- */

enum { MK_SEGMENT = 1, MK_SEEKHEAD, MK_SEEK, MK_INFO, MK_TRACKS, MK_TRACKENTRY, MK_VIDEO, MK_AUDIO };

#define ID_SEGMENT 0x18538067UL
#define ID_SEEKHEAD 0x114D9B74UL
#define ID_INFO 0x1549A966UL
#define ID_TRACKS 0x1654AE6BUL
#define ID_CLUSTER 0x1F43B675UL

typedef struct mkv {
    om_src *s;
    om_info *o;
    uint32_t elems;                     /* elements looked at, of 8192 */
    uint64_t scale;                     /* TimestampScale: nanoseconds a tick */
    uint64_t dur_bits;
    int dur_bytes, got_info, got_tracks;
    uint64_t info_at, tracks_at;        /* from the SeekHead, from the segment's data */
    uint32_t seek_id;
    uint64_t seek_pos;
    /* the track being read */
    uint32_t ttype, w, h, ch, bits;
    uint64_t default_dur, rate_bits, fr_bits;
    int rate_bytes, fr_bytes;
    char codec[OM_NAME];
    unsigned char b[16];
} mkv;

/* An element's ID and size at pos; size NOSIZE when the file says "unknown". 0 if none there. */
static int ebml_head(mkv *m, uint64_t pos, uint64_t end, uint32_t *id, uint64_t *size, uint32_t *hl)
{
    unsigned char *b = m->b;
    long n, want = end - pos < 12 ? (long)(end - pos) : 12;
    int il, len = 1, i, ones;
    uint32_t mask = 0x80;
    uint64_t v;
    if (pos >= end || want < 2 || (n = rd(m->s, pos, b, want)) < 2)
        return 0;
    il = b[0] >= 0x80 ? 1 : b[0] >= 0x40 ? 2 : b[0] >= 0x20 ? 3 : b[0] >= 0x10 ? 4 : 0;
    if (!il || n < il + 1)
        return 0;
    for (*id = 0, i = 0; i < il; i++)
        *id = (*id << 8) | b[i];
    while (len <= 8 && !(b[il] & mask)) {
        len++;
        mask >>= 1;
    }
    if (len > 8 || n < il + len)
        return 0;
    v = b[il] & (mask - 1);
    ones = v == mask - 1;
    for (i = 1; i < len; i++) {
        v = (v << 8) | b[il + i];
        if (b[il + i] != 0xFF)
            ones = 0;
    }
    *size = ones ? NOSIZE : v;
    *hl = (uint32_t)(il + len);
    return 1;
}

static uint64_t ebml_uint(mkv *m, uint64_t pos, uint64_t size)
{
    uint64_t v = 0;
    uint32_t i;
    if (!size || size > 8 || !rdx(m->s, pos, m->b, (long)size))
        return 0;
    for (i = 0; i < size; i++)
        v = (v << 8) | m->b[i];
    return v;
}

static int ebml_float(mkv *m, uint64_t pos, uint64_t size, uint64_t *bits)
{
    if ((size != 4 && size != 8) || !rdx(m->s, pos, m->b, (long)size))
        return 0;
    *bits = size == 4 ? be32(m->b) : be64(m->b);
    return (int)size;
}

static void ebml_str(mkv *m, uint64_t pos, uint64_t size, char *out)
{
    long n = size < OM_NAME - 1 ? (long)size : OM_NAME - 1, i;
    n = rd(m->s, pos, out, n);
    out[n] = 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)out[i] < 32 || (unsigned char)out[i] >= 127)
            out[i] = out[i] ? '.' : 0;
}

static uint32_t mkv_codec(const char *id)
{
    static const struct { const char *p; uint32_t codec; } t[] = {
        { "V_MPEG4/ISO/AVC", OMC_H264 }, { "V_MPEGH/ISO/HEVC", OMC_HEVC }, { "V_AV1", OMC_AV1 }, { "V_VP8", OMC_VP8 },
        { "V_VP9", OMC_VP9 }, { "V_MPEG1", OMC_MPEG1V }, { "V_MPEG2", OMC_MPEG2V }, { "V_MPEG4/ISO/", OMC_MPEG4V },
        { "V_THEORA", OMC_THEORA }, { "V_MJPEG", OMC_MJPEG }, { "V_PRORES", OMC_PRORES }, { "V_UNCOMPRESSED", OMC_RAWVIDEO },
        { "A_AAC", OMC_AAC }, { "A_MPEG/L3", OMC_MP3 }, { "A_MPEG/L2", OMC_MP2 }, { "A_MPEG/L1", OMC_MP1 },
        { "A_AC3", OMC_AC3 }, { "A_EAC3", OMC_EAC3 }, { "A_DTS", OMC_DTS }, { "A_VORBIS", OMC_VORBIS },
        { "A_OPUS", OMC_OPUS }, { "A_FLAC", OMC_FLAC }, { "A_ALAC", OMC_ALAC }, { "A_PCM/INT", OMC_PCM },
        { "A_PCM/FLOAT", OMC_PCMFLOAT },
    };
    unsigned i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        const char *p = t[i].p, *q = id;
        while (*p && *p == *q) {
            p++;
            q++;
        }
        if (!*p)
            return t[i].codec;
    }
    return 0;
}

static void mkv_track_end(mkv *m)
{
    om_info *o = m->o;
    if (m->ttype == 1) {
        if (o->vtracks++ == 0) {
            set_vcodec(o, mkv_codec(m->codec), m->codec);
            o->width = m->w;
            o->height = m->h;
            if (m->default_dur && m->default_dur <= 0xFFFFFFFFUL)
                set_fps(o, 1000000000UL, (uint32_t)m->default_dur);
            else if (m->fr_bytes) {
                uint64_t mfps = float_mul(m->fr_bits, m->fr_bytes, 1000);
                if (mfps && mfps <= 0xFFFFFFFFUL)
                    set_fps(o, (uint32_t)mfps, 1000);
            }
        }
    } else if (m->ttype == 2) {
        if (o->atracks++ == 0) {
            uint64_t r = m->rate_bytes ? float_mul(m->rate_bits, m->rate_bytes, 1) : 8000;
            set_acodec(o, mkv_codec(m->codec), m->codec);
            o->rate = r > BIG ? 0 : (uint32_t)r;
            o->channels = m->ch ? m->ch : 1;
            if (o->acodec == OMC_PCM || o->acodec == OMC_PCMFLOAT)
                o->bits = m->bits;
        }
    } else if (m->ttype == 17)
        o->stracks++;
    else if (m->ttype)
        o->otracks++;
}

static void mkv_walk(mkv *m, uint64_t pos, uint64_t end, int parent)
{
    while (pos < end && m->elems++ < 8192) {
        uint32_t id, hl;
        uint64_t size, data;
        if (!ebml_head(m, pos, end, &id, &size, &hl))
            return;
        data = pos + hl;
        if (size == NOSIZE) {
            if (parent == MK_SEGMENT)
                return;                         /* a cluster written live: the headers are all before it */
            size = end - data;
        }
        if (data > end || size > end - data) {
            m->o->flags |= OMIF_TRUNCATED;
            size = data > end ? 0 : end - data;
        }
        switch (parent) {
        case MK_SEGMENT:
            if (id == ID_SEEKHEAD)
                mkv_walk(m, data, data + size, MK_SEEKHEAD);
            else if (id == ID_INFO) {
                mkv_walk(m, data, data + size, MK_INFO);
                m->got_info = 1;
            } else if (id == ID_TRACKS) {
                mkv_walk(m, data, data + size, MK_TRACKS);
                m->got_tracks = 1;
            } else if (id == ID_CLUSTER)
                return;                         /* the frames: the SeekHead says where anything else is */
            break;
        case MK_SEEKHEAD:
            if (id == 0x4DBB) {
                m->seek_id = 0;
                m->seek_pos = NOSIZE;
                mkv_walk(m, data, data + size, MK_SEEK);
                if (m->seek_id == ID_INFO)
                    m->info_at = m->seek_pos;
                else if (m->seek_id == ID_TRACKS)
                    m->tracks_at = m->seek_pos;
            }
            break;
        case MK_SEEK:
            if (id == 0x53AB && size <= 4)
                m->seek_id = (uint32_t)ebml_uint(m, data, size);
            else if (id == 0x53AC)
                m->seek_pos = ebml_uint(m, data, size);
            break;
        case MK_INFO:
            if (id == 0x2AD7B1) {
                uint64_t v = ebml_uint(m, data, size);
                if (v)
                    m->scale = v;
            } else if (id == 0x4489)
                m->dur_bytes = ebml_float(m, data, size, &m->dur_bits);
            break;
        case MK_TRACKS:
            if (id == 0xAE) {
                m->ttype = m->w = m->h = m->ch = m->bits = 0;
                m->default_dur = m->rate_bits = m->fr_bits = 0;
                m->rate_bytes = m->fr_bytes = 0;
                m->codec[0] = 0;
                mkv_walk(m, data, data + size, MK_TRACKENTRY);
                mkv_track_end(m);
            }
            break;
        case MK_TRACKENTRY:
            if (id == 0x83)
                m->ttype = (uint32_t)ebml_uint(m, data, size);
            else if (id == 0x86)
                ebml_str(m, data, size, m->codec);
            else if (id == 0x23E383)
                m->default_dur = ebml_uint(m, data, size);
            else if (id == 0xE0)
                mkv_walk(m, data, data + size, MK_VIDEO);
            else if (id == 0xE1)
                mkv_walk(m, data, data + size, MK_AUDIO);
            break;
        case MK_VIDEO:
            if (id == 0xB0)
                m->w = (uint32_t)ebml_uint(m, data, size);
            else if (id == 0xBA)
                m->h = (uint32_t)ebml_uint(m, data, size);
            else if (id == 0x2383E3)
                m->fr_bytes = ebml_float(m, data, size, &m->fr_bits);
            else if (id == 0x9A && ebml_uint(m, data, size) == 1)
                m->o->flags |= OMIF_INTERLACED;
            break;
        case MK_AUDIO:
            if (id == 0xB5)
                m->rate_bytes = ebml_float(m, data, size, &m->rate_bits);
            else if (id == 0x9F)
                m->ch = (uint32_t)ebml_uint(m, data, size);
            else if (id == 0x6264)
                m->bits = (uint32_t)ebml_uint(m, data, size);
            break;
        }
        pos = data + size;
    }
}

/* A level-1 element the SeekHead points to, read if it wasn't met on the way. */
static void mkv_seek(mkv *m, uint64_t seg, uint64_t seg_end, uint64_t at, uint32_t want, int as)
{
    uint32_t id, hl;
    uint64_t size;
    if (at == NOSIZE || at >= seg_end - seg)
        return;
    if (!ebml_head(m, seg + at, seg_end, &id, &size, &hl) || id != want)
        return;
    if (size == NOSIZE || size > seg_end - (seg + at + hl))
        size = seg_end - (seg + at + hl);
    mkv_walk(m, seg + at + hl, seg + at + hl + size, as);
}

static int parse_mkv(om_src *s, om_info *o, void *aux)
{
    mkv *m = aux;
    uint32_t id, hl, k;
    uint64_t size, pos, end, seg = 0, seg_end = 0;
    char doctype[OM_NAME];
    zero(m, sizeof *m);
    m->s = s;
    m->o = o;
    m->scale = 1000000;
    m->info_at = m->tracks_at = NOSIZE;
    doctype[0] = 0;
    if (!ebml_head(m, 0, s->size, &id, &size, &hl) || id != 0x1A45DFA3UL || size == NOSIZE)
        return 0;
    end = hl + size < s->size ? hl + size : s->size;
    for (pos = hl, k = 0; pos < end && k < 32; k++) {
        uint32_t cid, chl;
        uint64_t csize;
        if (!ebml_head(m, pos, end, &cid, &csize, &chl) || csize == NOSIZE)
            break;
        if (cid == 0x4282)
            ebml_str(m, pos + chl, csize, doctype);
        pos += chl + csize;
    }
    if (same((const unsigned char *)doctype, "webm", 5))
        set_format(o, OMK_VIDEO, OMF_WEBM, "WebM");
    else
        set_format(o, OMK_VIDEO, OMF_MATROSKA, "Matroska");
    for (pos = end, k = 0; pos < s->size && k < 8; k++) {
        if (!ebml_head(m, pos, s->size, &id, &size, &hl))
            break;
        if (id == ID_SEGMENT) {
            seg = pos + hl;
            seg_end = size == NOSIZE || size > s->size - seg ? s->size : seg + size;
            break;
        }
        if (size == NOSIZE)
            break;
        pos += hl + size;
    }
    if (seg) {
        mkv_walk(m, seg, seg_end, MK_SEGMENT);
        if (!m->got_info)
            mkv_seek(m, seg, seg_end, m->info_at, ID_INFO, MK_INFO);
        if (!m->got_tracks)
            mkv_seek(m, seg, seg_end, m->tracks_at, ID_TRACKS, MK_TRACKS);
    }
    if (!o->vtracks && o->atracks)
        o->kind = OMK_SOUND;
    if (m->dur_bytes) {
        uint64_t ms;
        if (m->scale % 1000 == 0 && m->scale / 1000 <= 0xFFFFFFFFUL)
            ms = float_mul(m->dur_bits, m->dur_bytes, (uint32_t)(m->scale / 1000)) / 1000;
        else
            ms = float_mul(m->dur_bits, m->dur_bytes, (uint32_t)(m->scale > BIG ? BIG : m->scale)) / 1000000;
        o->duration_ms = ms > BIG ? BIG : (uint32_t)ms;
    }
    return 1;
}

/* ---- Ogg (RFC 3533) with Vorbis, Opus, Theora, FLAC and Speex ---- */

typedef struct ogg_stream {
    uint32_t serial, codec, rate, preskip, frn, frd, shift, channels, bitrate, w, h;
    uint64_t last;
    int has_last;
} ogg_stream;

typedef struct ogg {
    ogg_stream st[8];
    int n;
    unsigned char pg[27 + 255], pkt[64];
} ogg;

static void ogg_ident(ogg *g, uint32_t serial, const unsigned char *p, long n)
{
    ogg_stream *t;
    if (g->n >= 8)
        return;
    t = &g->st[g->n];
    zero(t, sizeof *t);
    t->serial = serial;
    if (n >= 28 && same(p, "\x01vorbis", 7)) {
        t->codec = OMC_VORBIS;
        t->channels = p[11];
        t->rate = le32(p + 12);
        t->bitrate = (int32_t)le32(p + 20) > 0 ? le32(p + 20) : 0;
    } else if (n >= 19 && same(p, "OpusHead", 8)) {
        t->codec = OMC_OPUS;
        t->channels = p[9];
        t->preskip = le16(p + 10);
        t->rate = 48000;                        /* Opus's granule positions always count at 48 kHz */
    } else if (n >= 42 && same(p, "\x80theora", 7)) {
        t->codec = OMC_THEORA;
        t->w = be24(p + 14);
        t->h = be24(p + 17);
        t->frn = be32(p + 22);
        t->frd = be32(p + 26);
        t->shift = ((uint32_t)(p[40] & 3) << 3) | (p[41] >> 5);
    } else if (n >= 35 && same(p, "\x7F" "FLAC", 5) && same(p + 9, "fLaC", 4)) {
        om_info tmp;
        zero(&tmp, sizeof tmp);
        flac_streaminfo(&tmp, p + 17);
        t->codec = OMC_FLAC;
        t->rate = tmp.rate;
        t->channels = tmp.channels;
    } else if (n >= 52 && same(p, "Speex   ", 8)) {
        t->codec = OMC_SPEEX;
        t->rate = le32(p + 36);
        t->channels = le32(p + 48);
    } else
        t->codec = OMC_OTHER;
    g->n++;
}

static int parse_ogg(om_src *s, om_info *o, unsigned char *work, void *aux)
{
    ogg *g = aux;
    uint64_t pos = 0, tail;
    uint32_t pages = 0;
    long n, i;
    ogg_stream *a = 0, *v = 0;
    zero(g, sizeof *g);
    set_format(o, OMK_SOUND, OMF_OGG, "Ogg");
    /* the first pages: each stream begins with a page of its own (BOS) */
    while (pos + 27 <= s->size && pages++ < 32) {
        uint32_t nseg, body = 0, first = 0, k;
        int ended = 0;
        n = rd(s, pos, g->pg, sizeof g->pg);
        if (n < 27 || !same(g->pg, "OggS", 4) || g->pg[4] != 0)
            break;
        nseg = g->pg[26];
        if (27 + (long)nseg > n)
            break;
        for (k = 0; k < nseg; k++) {
            body += g->pg[27 + k];
            if (!ended) {
                first += g->pg[27 + k];
                ended = g->pg[27 + k] < 255;
            }
        }
        if (!(g->pg[5] & 2))
            break;                              /* the streams' first pages are all at the start */
        n = rd(s, pos + 27 + nseg, g->pkt, first < sizeof g->pkt ? (long)first : (long)sizeof g->pkt);
        ogg_ident(g, le32(g->pg + 14), g->pkt, n);
        pos += 27 + nseg + body;
    }
    /* the last granule position of each stream, from the end of the file */
    tail = s->size > OM_WORK ? s->size - OM_WORK : 0;
    n = rd(s, tail, work, OM_WORK);
    for (i = 0; i + 27 <= n; i++)
        if (work[i] == 'O' && same(work + i, "OggS", 4) && work[i + 4] == 0) {
            uint32_t serial = le32(work + i + 14);
            uint64_t gp = le64(work + i + 6);
            int k;
            if (gp == 0xFFFFFFFFFFFFFFFFULL)
                continue;
            for (k = 0; k < g->n; k++)
                if (g->st[k].serial == serial) {
                    g->st[k].last = gp;
                    g->st[k].has_last = 1;
                }
        }
    for (i = 0; i < g->n; i++) {
        ogg_stream *t = &g->st[i];
        if (t->codec == OMC_THEORA) {
            if (!v)
                v = t;
            o->vtracks++;
        } else if (t->codec != OMC_OTHER) {
            if (!a)
                a = t;
            o->atracks++;
        } else
            o->otracks++;
    }
    if (v) {
        o->kind = OMK_VIDEO;
        set_vcodec(o, OMC_THEORA, 0);
        o->width = v->w;
        o->height = v->h;
        set_fps(o, v->frn, v->frd);
        if (v->has_last && v->frn && v->shift < 32) {
            uint64_t frames = (v->last >> v->shift) + (v->last & ((1ULL << v->shift) - 1));
            o->frames = frames > BIG ? BIG : (uint32_t)frames;
            o->duration_ms = mdiv(frames * v->frd, 1000, v->frn);
        }
    }
    if (a) {
        set_acodec(o, a->codec, 0);
        o->channels = a->channels;
        o->rate = a->codec == OMC_OPUS ? 48000 : a->rate;
        o->bitrate = a->bitrate;
        if (a->has_last && a->rate) {
            uint64_t smp = a->last > a->preskip ? a->last - a->preskip : 0;
            o->duration_ms = mdiv(smp, 1000, a->rate);
        }
        if (o->bitrate == 0 && o->duration_ms)
            o->bitrate = mdiv(s->size, 8000, o->duration_ms);
    }
    return 1;
}

/* ---- MPEG program streams (ISO/IEC 11172-1, 13818-1) ---- */

/* A pack header's system clock (90 kHz) at p (00 00 01 BA, 10 bytes there):
 * *ok 2 for MPEG-2, 1 for MPEG-1, 0 when it isn't one. */
static uint64_t scr_of(const unsigned char *p, int *ok)
{
    if ((p[4] & 0xC4) == 0x44 && (p[6] & 4) && (p[8] & 4) && (p[9] & 1)) {
        *ok = 2;
        return ((uint64_t)(p[4] & 0x38) << 27) | ((uint64_t)(p[4] & 3) << 28) | ((uint64_t)p[5] << 20) |
               ((uint64_t)(p[6] & 0xF8) << 12) | ((uint64_t)(p[6] & 3) << 13) | ((uint64_t)p[7] << 5) | (p[8] >> 3);
    }
    if ((p[4] & 0xF1) == 0x21 && (p[6] & 1) && (p[8] & 1)) {
        *ok = 1;
        return ((uint64_t)(p[4] & 0x0E) << 29) | ((uint64_t)p[5] << 22) | ((uint64_t)(p[6] & 0xFE) << 14) |
               ((uint64_t)p[7] << 7) | (p[8] >> 1);
    }
    *ok = 0;
    return 0;
}

/* Where a PES packet's data starts (p at 00 00 01 id, n bytes there), or -1. */
static long pes_payload(const unsigned char *p, long n)
{
    long k = 6;
    if (n < 9)
        return -1;
    if ((p[6] & 0xC0) == 0x80) {
        k = 9 + p[8];
        return k <= n ? k : -1;
    }
    while (k < n && p[k] == 0xFF && k < 6 + 16)
        k++;                                    /* MPEG-1: stuffing */
    if (k < n && (p[k] & 0xC0) == 0x40)
        k += 2;
    if (k < n && (p[k] & 0xF0) == 0x20)
        k += 5;
    else if (k < n && (p[k] & 0xF0) == 0x30)
        k += 10;
    else if (k < n && p[k] == 0x0F)
        k++;
    return k <= n ? k : -1;
}

/* A PES packet's presentation time (p at 00 00 01 id, n bytes there), MPEG-1 or MPEG-2: *ok 1 if it has one. */
static uint64_t pes_pts_any(const unsigned char *p, long n, int *ok)
{
    long k = 6;
    *ok = 0;
    if (n < 14)
        return 0;
    if ((p[6] & 0xC0) == 0x80) {
        if (!(p[7] & 0x80) || p[8] < 5)
            return 0;
        k = 9;
    } else {
        while (k < n && p[k] == 0xFF && k < 6 + 16)
            k++;
        if (k < n && (p[k] & 0xC0) == 0x40)
            k += 2;
        if (k + 5 > n || ((p[k] & 0xF0) != 0x20 && (p[k] & 0xF0) != 0x30))
            return 0;
    }
    if (k + 5 > n || !(p[k] & 1) || !(p[k + 2] & 1) || !(p[k + 4] & 1))
        return 0;
    *ok = 1;
    return ((uint64_t)((p[k] >> 1) & 7) << 30) | ((uint64_t)p[k + 1] << 22) | ((uint64_t)(p[k + 2] >> 1) << 15) |
           ((uint64_t)p[k + 3] << 7) | (p[k + 4] >> 1);
}

/* A length from the first and last presentation times (90 kHz), and one frame more when the rate is known. */
static uint32_t pts_span(const om_info *o, uint64_t a, uint64_t b)
{
    uint64_t ms;
    if (b < a)
        b += 1ULL << 33;                        /* the clock wrapped */
    ms = (b - a) / 90;
    if (o->fps_num)
        ms += mdiv(o->fps_den, 1000, o->fps_num);
    return ms > BIG ? BIG : (uint32_t)ms;
}

static int bitcount(uint32_t v)
{
    int n = 0;
    while (v) {
        n += (int)(v & 1);
        v >>= 1;
    }
    return n;
}

/* MPEG audio, ADTS or AC-3 at the start of p[0..n): the first sound track's codec, rate and channels. */
static void audio_es(om_info *o, const unsigned char *p, long n, uint32_t hint)
{
    long i;
    for (i = 0; i + 8 <= n && i < 512; i++) {
        mpa m;
        uint32_t r, c, l, codec;
        if ((hint == 0 || hint == OMC_MP3) && mpa_header(p + i, &m)) {
            set_acodec(o, mpa_codec(&m), 0);
            o->rate = m.rate;
            o->channels = m.channels;
            return;
        }
        if ((hint == 0 || hint == OMC_AAC) && adts_header(p + i, &r, &c, &l)) {
            set_acodec(o, OMC_AAC, 0);
            o->rate = r;
            o->channels = c;
            return;
        }
        if ((hint == 0 || hint == OMC_AC3 || hint == OMC_EAC3) && (codec = ac3_header(p + i, &r, &c))) {
            set_acodec(o, codec, 0);
            o->rate = r;
            o->channels = c;
            return;
        }
    }
    if (hint)
        set_acodec(o, hint, 0);
}

static int parse_ps(om_src *s, om_info *o, const unsigned char *h, long hn, unsigned char *work)
{
    long i, n;
    int ok, kind = 0, first_ok = 0, have_v = 0, have_a = 0;
    uint64_t first = 0, last = 0, tail, vpts = 0, apts = 0, pts;
    uint32_t vids = 0, auds = 0, subs = 0, privs = 0;
    set_format(o, OMK_VIDEO, OMF_MPEGPS, "MPEG-PS");
    for (i = 0; (i = find_code(h, hn, i, -1)) >= 0 && i + 14 <= hn;) {
        uint32_t c = h[i + 3], len;
        if (c == 0xBA) {
            uint64_t scr = scr_of(h + i, &ok);
            if (ok && !first_ok) {
                first = scr;
                first_ok = 1;
                kind = ok;
            }
            i += ok == 2 ? 14 + (h[i + 13] & 7) : ok ? 12 : 4;
            continue;
        }
        if (c < 0xBB) {
            i += 4;
            continue;
        }
        len = be16(h + i + 4);
        if (c >= 0xE0 && c <= 0xEF) {
            vids |= 1UL << (c - 0xE0);
            if (!have_v && (pts = pes_pts_any(h + i, hn - i, &ok), ok)) {
                vpts = pts;
                have_v = 1;
            }
            i += 4;                             /* its sequence header is looked for below */
            continue;
        }
        if ((c >= 0xC0 && c <= 0xDF) || c == 0xBD) {
            if (!have_a && (pts = pes_pts_any(h + i, hn - i, &ok), ok)) {
                apts = pts;
                have_a = 1;
            }
        }
        if (c >= 0xC0 && c <= 0xDF) {
            long k = pes_payload(h + i, hn - i);
            if (!auds && !privs && k > 0)
                audio_es(o, h + i + k, (i + 6 + (long)len <= hn ? 6 + (long)len : hn - i) - k, OMC_MP3);
            auds |= 1UL << (c - 0xC0);
        } else if (c == 0xBD) {
            long k = pes_payload(h + i, hn - i);
            if (k > 0 && i + k + 8 <= hn) {
                uint32_t sub = h[i + k];
                if (sub >= 0x20 && sub <= 0x3F)
                    subs |= 1UL << (sub - 0x20);
                else if (sub >= 0x80 && sub <= 0xAF) {
                    if (!auds && !privs) {
                        if (sub <= 0x87)
                            audio_es(o, h + i + k + 4, (i + 6 + (long)len <= hn ? 6 + (long)len : hn - i) - k - 4, OMC_AC3);
                        else if (sub <= 0x8F)
                            set_acodec(o, OMC_DTS, 0);
                        else if (sub >= 0xA0) {
                            uint32_t b = h[i + k + 5];
                            set_acodec(o, OMC_PCM, 0);
                            o->bits = 16 + 4 * ((b >> 6) & 3);
                            o->rate = (b >> 4) & 3 ? 96000 : 48000;
                            o->channels = (b & 7) + 1;
                        }
                    }
                    privs |= 1UL << ((sub - 0x80) & 31);
                }
            }
        }
        i += 6 + (long)len;
    }
    (void)kind;
    o->vtracks = (uint16_t)bitcount(vids);
    o->atracks = (uint16_t)(bitcount(auds) + bitcount(privs));
    o->stracks = (uint16_t)bitcount(subs);
    if (vids) {
        int v = mpv_scan(o, h, hn);
        if (v)
            set_vcodec(o, v == 2 ? OMC_MPEG2V : OMC_MPEG1V, 0);
        else if (h264_scan(o, h, hn))
            set_vcodec(o, OMC_H264, 0);
    } else if (auds || privs)
        o->kind = OMK_SOUND;
    /* the length: the latest presentation time near the end of the file (B frames come out of
     * order, so the largest, not the last), video first, then sound; else the pack clocks */
    tail = s->size > OM_WORK ? s->size - OM_WORK : 0;
    n = rd(s, tail, work, OM_WORK);
    {
        uint64_t vmax = 0, amax = 0;
        int vfound = 0, afound = 0;
        for (i = 0; i + 14 <= n; i++)
            if (work[i] == 0 && work[i + 1] == 0 && work[i + 2] == 1) {
                uint32_t c = work[i + 3];
                if ((c >= 0xC0 && c <= 0xEF) || c == 0xBD) {
                    pts = pes_pts_any(work + i, n - i, &ok);
                    if (ok && c >= 0xE0 && (!vfound || pts > vmax)) {
                        vmax = pts;
                        vfound = 1;
                    } else if (ok && c < 0xE0 && (!afound || pts > amax)) {
                        amax = pts;
                        afound = 1;
                    }
                } else if (c == 0xBA) {
                    uint64_t scr = scr_of(work + i, &ok);
                    if (ok)
                        last = scr;
                }
            }
        if (have_v && vfound)
            o->duration_ms = pts_span(o, vpts, vmax);
        else if (have_a && afound) {
            uint32_t fps_num = o->fps_num;
            o->fps_num = 0;                     /* no frame added for sound */
            o->duration_ms = pts_span(o, apts, amax);
            o->fps_num = fps_num;
        } else if (first_ok && last > first)
            o->duration_ms = (uint32_t)((last - first) / 90);
    }
    return 1;
}

/* An MPEG video stream on its own (.m1v, .m2v), or an H.264 or HEVC one. */
static int parse_es_video(om_src *s, om_info *o, const unsigned char *h, long hn)
{
    int v = mpv_scan(o, h, hn);
    (void)s;
    if (v) {
        set_format(o, OMK_VIDEO, OMF_MPEGVIDEO, "MPEG video");
        set_vcodec(o, v == 2 ? OMC_MPEG2V : OMC_MPEG1V, 0);
    } else if (hevc_scan(o, h, hn)) {
        set_format(o, OMK_VIDEO, OMF_H265, "HEVC stream");
        set_vcodec(o, OMC_HEVC, 0);
    } else if (h264_scan(o, h, hn)) {
        set_format(o, OMK_VIDEO, OMF_H264, "H.264 stream");
        set_vcodec(o, OMC_H264, 0);
    } else
        return 0;
    o->vtracks = 1;
    return 1;
}

/* ---- MPEG transport streams (ISO/IEC 13818-1 2.4.3, 2.4.4) ---- */

typedef struct ts_es { uint16_t pid; uint8_t kind; uint32_t codec; } ts_es;

typedef struct ts {
    uint32_t pkt, off;                  /* the packet's size, and where its sync byte is */
    int pmt_pid, pcr_pid, have_pmt, nes;
    ts_es es[16];
    int vpid, apid, vstate, astate;     /* the first video and sound streams: 0 waiting, 1 collecting, 2 done */
    uint32_t vcodec, acodec;
    long vn, an;
    uint64_t first_pcr, last_pcr, first_pts, last_pts;
    int have_first_pcr, have_last_pcr, have_first_pts, have_last_pts;
    unsigned char vbuf[4096], abuf[1024];
} ts;

static uint64_t pes_pts(const unsigned char *p, long n, int *ok)
{
    *ok = 0;
    if (n < 14 || p[0] || p[1] || p[2] != 1 || (p[6] & 0xC0) != 0x80 || !(p[7] & 0x80) || p[8] < 5)
        return 0;
    *ok = 1;
    return ((uint64_t)((p[9] >> 1) & 7) << 30) | ((uint64_t)p[10] << 22) | ((uint64_t)(p[11] >> 1) << 15) |
           ((uint64_t)p[12] << 7) | (p[13] >> 1);
}

/* A stream type (and its descriptors) as a kind (1 video, 2 sound, 3 subtitles, 0 other) and a codec. */
static int ts_type(uint32_t type, const unsigned char *d, long dn, uint32_t *codec)
{
    long i;
    *codec = 0;
    switch (type) {
    case 0x01: *codec = OMC_MPEG1V; return 1;
    case 0x02: *codec = OMC_MPEG2V; return 1;
    case 0x10: *codec = OMC_MPEG4V; return 1;
    case 0x1B: *codec = OMC_H264; return 1;
    case 0x24: *codec = OMC_HEVC; return 1;
    case 0xEA: *codec = OMC_VC1; return 1;
    case 0x03: case 0x04: *codec = OMC_MP3; return 2;   /* the layer is read from the frames */
    case 0x0F: *codec = OMC_AAC; return 2;
    case 0x11: *codec = OMC_AAC; return 2;
    case 0x81: *codec = OMC_AC3; return 2;
    case 0x87: *codec = OMC_EAC3; return 2;
    case 0x82: case 0x85: *codec = OMC_DTS; return 2;
    case 0x80: *codec = OMC_PCM; return 2;
    case 0x90: case 0x92: return 3;
    case 0x06:
        for (i = 0; i + 2 <= dn && i + 2 + d[i + 1] <= dn; i += 2 + d[i + 1]) {
            uint32_t tag = d[i], l = d[i + 1];
            if (tag == 0x6A) { *codec = OMC_AC3; return 2; }
            if (tag == 0x7A) { *codec = OMC_EAC3; return 2; }
            if (tag == 0x7B) { *codec = OMC_DTS; return 2; }
            if (tag == 0x59 || tag == 0x56) return 3;
            if (tag == 0x05 && l >= 4) {
                if (same(d + i + 2, "AC-3", 4)) { *codec = OMC_AC3; return 2; }
                if (same(d + i + 2, "EAC3", 4)) { *codec = OMC_EAC3; return 2; }
                if (same(d + i + 2, "Opus", 4)) { *codec = OMC_OPUS; return 2; }
                if (same(d + i + 2, "DTS", 3)) { *codec = OMC_DTS; return 2; }
                if (same(d + i + 2, "HEVC", 4)) { *codec = OMC_HEVC; return 1; }
            }
        }
        return 0;
    }
    return 0;
}

static void ts_pmt(ts *t, const unsigned char *sec, long n)
{
    long slen, i, end, pil;
    if (n < 12 || sec[0] != 2)
        return;
    slen = (long)(be16(sec + 1) & 0xFFF);
    end = 3 + slen - 4;                         /* before the CRC */
    if (end > n)
        end = n;
    t->pcr_pid = (int)(be16(sec + 8) & 0x1FFF);
    pil = (long)(be16(sec + 10) & 0xFFF);
    for (i = 12 + pil; i + 5 <= end && t->nes < 16; ) {
        uint32_t type = sec[i], pid = be16(sec + i + 1) & 0x1FFF, codec;
        long eil = (long)(be16(sec + i + 3) & 0xFFF);
        int kind;
        if (i + 5 + eil > end)
            eil = end - i - 5;
        kind = ts_type(type, sec + i + 5, eil, &codec);
        t->es[t->nes].pid = (uint16_t)pid;
        t->es[t->nes].kind = (uint8_t)kind;
        t->es[t->nes].codec = codec;
        t->nes++;
        if (kind == 1 && t->vpid < 0) {
            t->vpid = (int)pid;
            t->vcodec = codec;
        } else if (kind == 2 && t->apid < 0) {
            t->apid = (int)pid;
            t->acodec = codec;
        }
        i += 5 + eil;
    }
    t->have_pmt = 1;
}

static void ts_collect(ts *t, int video, const unsigned char *pl, long pn, int pusi)
{
    int *state = video ? &t->vstate : &t->astate;
    unsigned char *buf = video ? t->vbuf : t->abuf;
    long *bn = video ? &t->vn : &t->an, cap = video ? (long)sizeof t->vbuf : (long)sizeof t->abuf;
    if (*state == 2)
        return;
    if (pusi) {
        long k;
        int ok;
        uint64_t pts;
        if (*state == 1) {
            *state = 2;                         /* the next packet begins: the first is all there */
            return;
        }
        if (pn < 9 || pl[0] || pl[1] || pl[2] != 1)
            return;
        pts = pes_pts(pl, pn, &ok);
        if (ok && !t->have_first_pts && video == (t->vpid >= 0)) {
            t->first_pts = pts;
            t->have_first_pts = 1;
        }
        k = 9 + pl[8];
        if (k >= pn)
            return;
        pl += k;
        pn -= k;
        *state = 1;
    } else if (*state != 1)
        return;
    if (pn > cap - *bn)
        pn = cap - *bn;
    {
        long i;
        for (i = 0; i < pn; i++)
            buf[*bn + i] = pl[i];
    }
    *bn += pn;
    if (*bn >= cap)
        *state = 2;
}

/* One packet at p (pkt bytes, sync byte first). tail: only the clocks are wanted. */
static void ts_packet(ts *t, const unsigned char *p, int tail)
{
    uint32_t pid = be16(p + 1) & 0x1FFF, afc = (p[3] >> 4) & 3, off = 4;
    int pusi = (p[1] & 0x40) != 0;
    if (p[0] != 0x47)
        return;
    if (afc & 2) {
        uint32_t al = p[4];
        if (al >= 7 && al <= 183 && (p[5] & 0x10) && (int)pid == t->pcr_pid) {
            uint64_t pcr = ((uint64_t)p[6] << 25) | ((uint64_t)p[7] << 17) | ((uint64_t)p[8] << 9) | ((uint64_t)p[9] << 1) | (p[10] >> 7);
            if (!tail && !t->have_first_pcr) {
                t->first_pcr = pcr;
                t->have_first_pcr = 1;
            }
            if (tail) {
                t->last_pcr = pcr;
                t->have_last_pcr = 1;
            }
        }
        off = 5 + al;
    }
    if (!(afc & 1) || off >= 188)
        return;
    if (tail) {
        int ok;
        uint64_t pts;
        if (pusi && (int)pid == (t->vpid >= 0 ? t->vpid : t->apid)) {
            pts = pes_pts(p + off, 188 - (long)off, &ok);
            if (ok && (!t->have_last_pts || pts > t->last_pts)) {
                t->last_pts = pts;              /* the largest: B frames come out of order */
                t->have_last_pts = 1;
            }
        }
        return;
    }
    if (pid == 0 && pusi && t->pmt_pid < 0) {
        const unsigned char *sec = p + off + 1 + p[off];
        long n = 188 - (long)(sec - p), slen, i;
        if (n >= 8 && sec[0] == 0) {
            slen = (long)(be16(sec + 1) & 0xFFF);
            for (i = 8; i + 4 <= 3 + slen - 4 && i + 4 <= n; i += 4)
                if (be16(sec + i) != 0) {
                    t->pmt_pid = (int)(be16(sec + i + 2) & 0x1FFF);
                    break;
                }
        }
    } else if ((int)pid == t->pmt_pid && pusi && !t->have_pmt) {
        const unsigned char *sec = p + off + 1 + p[off];
        long n = 188 - (long)(sec - p);
        if (n > 0)
            ts_pmt(t, sec, n);
    } else if ((int)pid == t->vpid)
        ts_collect(t, 1, p + off, 188 - (long)off, pusi);
    else if ((int)pid == t->apid)
        ts_collect(t, 0, p + off, 188 - (long)off, pusi);
}

/* The first packet's sync byte in p[0..n) with pkt-byte packets: its offset, or -1. */
static long ts_sync(const unsigned char *p, long n, uint32_t pkt)
{
    long i;
    for (i = 0; i < (long)pkt && i + 2 * (long)pkt < n; i++)
        if (p[i] == 0x47 && p[i + pkt] == 0x47 && p[i + 2 * pkt] == 0x47)
            return i;
    return -1;
}

static int parse_ts(om_src *s, om_info *o, const unsigned char *h, long hn, unsigned char *work, void *aux, uint32_t pkt)
{
    ts *t = aux;
    uint64_t pos, tail;
    long i, n, start;
    int k, rounds = 0;
    zero(t, sizeof *t);
    t->pkt = pkt;
    t->pmt_pid = t->pcr_pid = t->vpid = t->apid = -1;
    set_format(o, OMK_VIDEO, OMF_MPEGTS, pkt == 192 ? "MPEG-TS (M2TS)" : "MPEG-TS");
    if ((start = ts_sync(h, hn, pkt)) < 0)
        return 0;
    for (i = start; i + 188 <= hn; i += pkt)
        ts_packet(t, h + i, 0);
    /* on through the file until both streams' first packets are in */
    pos = (uint64_t)i;
    while (rounds++ < 64 && !s->spent && pos < s->size &&
           (!t->have_pmt || (t->vpid >= 0 && t->vstate != 2) || (t->apid >= 0 && t->astate != 2))) {
        long want = (long)(OM_WORK / pkt * pkt);
        n = rd(s, pos, work, want);
        if (n < (long)pkt)
            break;
        for (i = 0; i + 188 <= n; i += pkt)
            ts_packet(t, work + i, 0);
        pos += (uint64_t)(n / (long)pkt * (long)pkt);
    }
    for (k = 0; k < t->nes; k++) {
        if (t->es[k].kind == 1)
            o->vtracks++;
        else if (t->es[k].kind == 2)
            o->atracks++;
        else if (t->es[k].kind == 3)
            o->stracks++;
        else
            o->otracks++;
    }
    if (t->vpid >= 0) {
        set_vcodec(o, t->vcodec, 0);
        if (t->vcodec == OMC_MPEG1V || t->vcodec == OMC_MPEG2V)
            mpv_scan(o, t->vbuf, t->vn);
        else if (t->vcodec == OMC_H264)
            h264_scan(o, t->vbuf, t->vn);
        else if (t->vcodec == OMC_HEVC)
            hevc_scan(o, t->vbuf, t->vn);
    } else if (t->apid >= 0)
        o->kind = OMK_SOUND;
    if (t->apid >= 0)
        audio_es(o, t->abuf, t->an, t->acodec);
    /* the last clock, from the end of the file */
    tail = s->size > OM_WORK ? s->size - OM_WORK : 0;
    n = rd(s, tail, work, OM_WORK);
    if ((start = ts_sync(work, n, pkt)) >= 0)
        for (i = start; i + 188 <= n; i += pkt)
            ts_packet(t, work + i, 1);
    if (t->have_first_pts && t->have_last_pts)
        o->duration_ms = pts_span(o, t->first_pts, t->last_pts);
    else if (t->have_first_pcr && t->have_last_pcr) {
        uint64_t a = t->first_pcr, b = t->last_pcr;
        if (b < a)
            b += 1ULL << 33;
        o->duration_ms = (uint32_t)((b - a) / 90);
    }
    return 1;
}

/* ---- what the first bytes say ---- */

int om_probe(om_src *s, om_info *o, unsigned char *scratch)
{
    unsigned char *h = scratch, *work = scratch + OM_HEAD;
    void *aux = scratch + OM_HEAD + OM_WORK;
    long hn;
    int r = 0;
    zero(o, sizeof *o);
    o->size = s->size;
    if (s->budget < OM_BUDGET_MIN)
        s->budget = OM_BUDGET_MIN;
    s->head = 0;
    s->headlen = 0;
    hn = rd(s, 0, h, OM_HEAD);
    if (hn < 4)
        return 0;
    s->head = h;                        /* the parsers' rereads of these bytes cost nothing */
    s->headlen = (uint32_t)hn;
    if (hn >= 8 && (same(h + 4, "ftyp", 4) ||
                    ((same(h + 4, "moov", 4) || same(h + 4, "mdat", 4) || same(h + 4, "wide", 4) ||
                      same(h + 4, "free", 4) || same(h + 4, "skip", 4)) && be32(h) >= 8)))
        r = parse_bmff(s, o, h, hn, aux);
    else if (be32(h) == 0x1A45DFA3UL)
        r = parse_mkv(s, o, aux);
    else if (hn >= 12 && (same(h, "RIFF", 4) || same(h, "RF64", 4))) {
        uint64_t end = 8 + (uint64_t)le32(h + 4);
        if (end > s->size || le32(h + 4) == 0 || le32(h + 4) == 0xFFFFFFFFUL)
            end = s->size;
        if (same(h + 8, "WAVE", 4))
            r = parse_wav(s, o, same(h, "RF64", 4));
        else if (same(h + 8, "AVI ", 4) || same(h + 8, "AVIX", 4))
            r = parse_avi(s, o, end);
        else if (same(h + 8, "WEBP", 4))
            r = parse_webp(s, o, h, hn);
    } else if (hn >= 12 && same(h, "FORM", 4))
        r = parse_iff(s, o, h);
    else if (same(h, "OggS", 4))
        r = parse_ogg(s, o, work, aux);
    else if (hn >= 8 && same(h, "\x89PNG\r\n\x1a\n", 8))
        r = parse_png(s, o);
    else if (hn >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF)
        r = parse_jpeg(s, o);
    else if (hn >= 13 && (same(h, "GIF87a", 6) || same(h, "GIF89a", 6)))
        r = parse_gif(o, h);
    else if (hn >= 30 && h[0] == 'B' && h[1] == 'M')
        r = parse_bmp(o, h, hn);
    else if (hn >= 14 && h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0xBA)
        r = parse_ps(s, o, h, hn, work);
    else if (hn >= 8 && h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0xB3)
        r = parse_es_video(s, o, h, hn);
    else if (hn >= 8 && h[0] == 0 && h[1] == 0 && h[2] == 0 && h[3] == 1 &&
             ((h[4] & 0x1F) == 7 || (h[4] & 0x1F) == 9 || h[4] == 0x40 || h[4] == 0x46))
        r = parse_es_video(s, o, h, hn);       /* H.264 (SPS or delimiter first) or HEVC (VPS or delimiter) */
    else if (hn >= 8 && h[0] == 0 && h[1] == 0 && h[2] == 1 && (h[3] & 0x1F) == 7)
        r = parse_es_video(s, o, h, hn);
    else if (hn >= 3 * 188 && h[0] == 0x47 && h[188] == 0x47 && h[376] == 0x47)
        r = parse_ts(s, o, h, hn, work, aux, 188);
    else if (hn >= 4 + 3 * 192 && h[4] == 0x47 && h[196] == 0x47 && h[388] == 0x47)
        r = parse_ts(s, o, h, hn, work, aux, 192);
    else
        r = parse_audio_stream(s, o, h, hn, work);
    if (!r) {
        zero(o, sizeof *o);
        o->size = s->size;
        return 0;
    }
    if (s->spent && !o->duration_ms)
        o->flags |= OMIF_DURATION_ESTIMATED;
    if (o->duration_ms && !o->bitrate && (o->kind == OMK_VIDEO || o->kind == OMK_SOUND))
        o->bitrate = mdiv(s->size, 8000, o->duration_ms);
    return 1;
}
