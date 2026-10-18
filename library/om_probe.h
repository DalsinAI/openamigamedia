/* om_probe: openmedia.library's own parsers (stage 0). Plain C with no
 * operating system calls: the library gives them a read callback over a DOS
 * file, the host tests one over a stdio file. Each parser reads only the
 * header bytes it needs, through om_src, which counts what is read and
 * stops at a budget, so a damaged or hostile file can cost no more than
 * that. Every length and count read from a file is checked against the
 * bytes there before it is used.
 *
 * Written from the formats' public specifications: ISO/IEC 14496-12 and
 * -14 (MP4), Apple's QuickTime File Format, Matroska's and EBML's
 * specifications (RFC 8794, RFC 9559), Microsoft's AVI RIFF and WAVE
 * documents, ISO/IEC 11172-1/-2/-3 and 13818-1/-2 (MPEG-1 and MPEG-2
 * systems, video and audio), ITU-T H.264 section 7.3.2.1 (its sequence
 * parameter set), RFC 3533 and the Vorbis, Opus (RFC 7845), Theora and
 * Ogg FLAC mappings, the FLAC format (RFC 9639), Apple's AIFF and AIFF-C,
 * Electronic Arts' EA IFF 85 with ILBM, 8SVX and ANIM, PNG (ISO/IEC 15948),
 * JPEG (ITU-T T.81), GIF89a, the BMP headers, and WebP's RIFF container.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OM_PROBE_H
#define OM_PROBE_H

#include <stdint.h>

/* The scratch buffer om_probe is given: the file's first OM_HEAD bytes,
 * which every parser looks at first, then OM_WORK bytes for reads further
 * on (the end of a file, a transport stream's packets), then OM_AUX bytes
 * for the parsers' tables. Nothing big goes on the stack: the library runs
 * on its caller's stack, which may be 4 KB. */
#define OM_HEAD 65536
#define OM_WORK 32768
#define OM_AUX 16384
#define OM_SCRATCH (OM_HEAD + OM_WORK + OM_AUX)
/* The least and the default read budget. */
#define OM_BUDGET_MIN (64UL * 1024)
#define OM_BUDGET_DEFAULT (1024UL * 1024)

typedef struct om_src {
    void *user;
    /* n bytes at pos into buf: the bytes read (fewer at the end), or -1. */
    long (*read_at)(void *user, uint64_t pos, void *buf, long n);
    uint64_t size;              /* the file's size */
    uint32_t budget;            /* bytes the parsers may still read; om_probe counts it down */
    int spent;                  /* the budget ran out on the way */
    int failed;                 /* a read failed */
} om_src;

#define OM_NAME 32

typedef struct om_info {
    uint32_t kind, container, flags;    /* OMK_, OMF_, OMIF_ (openmedia/openmedia.h) */
    char format[OM_NAME];               /* the container's name, "" when unknown */
    uint64_t size;
    uint32_t duration_ms;
    uint32_t width, height, depth;
    uint32_t fps_num, fps_den;
    uint32_t frames;
    uint32_t vcodec, acodec;            /* OMC_ */
    char vcodec_name[OM_NAME], acodec_name[OM_NAME];
    uint32_t channels, rate, bits, bitrate;
    uint16_t vtracks, atracks, stracks, otracks;
} om_info;

/* Looks at the file s reads. scratch is OM_SCRATCH bytes.
 * 1 when the format is one of ours (info filled), 0 when not (info has the
 * size only; the library asks datatypes.library next). */
int om_probe(om_src *s, om_info *info, unsigned char *scratch);

/* A codec's name ("H.264"), or NULL for OMC_NONE and codes not known. */
const char *om_codec_name(uint32_t codec);

#endif
