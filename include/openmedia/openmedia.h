/* openmedia.library's public interface.
 *
 * Stage 0 (version 1): what a file is. One call reads only the bytes it
 * needs from a file's headers and says what the file holds: a picture, a
 * sound, a video or an animation; its container; its codecs; its size in
 * pixels, its length, its channels and rate.
 *
 *   struct OMMediaInfo *mi = OM_ObtainMediaInfo(OMA_Name, (ULONG)"Work:Film.mp4", TAG_DONE);
 *   if (mi) {
 *       Printf("%s, %s, %lu x %lu, %lu ms\n", mi->omi_FormatName,
 *              mi->omi_VideoCodecName ? mi->omi_VideoCodecName : "no video",
 *              mi->omi_Width, mi->omi_Height, mi->omi_Duration);
 *       OM_ReleaseMediaInfo(mi);
 *   }
 *
 * The decode and encode sessions of DESIGN.md section 1 come in version 2,
 * at the LVOs DESIGN.md section 6 sets aside after these.
 *
 * Define OPENMEDIA_CONSTANTS_ONLY before including this file to get only
 * the numbers (OMK_, OMF_, OMC_, OMERR_), with no AmigaOS types: the host
 * tests do.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENMEDIA_OPENMEDIA_H
#define OPENMEDIA_OPENMEDIA_H

#define OPENMEDIA_NAME    "openmedia.library"
#define OPENMEDIA_VERSION 1     /* stage 0: OM_ObtainMediaInfoA and its two companions */

/* A four-letter code, as IFF's MAKE_ID */
#define OM_ID(a, b, c, d) (((unsigned long)(a) << 24) | ((unsigned long)(b) << 16) | ((unsigned long)(c) << 8) | (unsigned long)(d))

/* ---- omi_Kind: what the file holds ---- */
#define OMK_UNKNOWN    0        /* nothing openmedia.library or a datatype knows */
#define OMK_PICTURE    1
#define OMK_SOUND      2        /* sampled sound */
#define OMK_VIDEO      3        /* moving pictures in a container, with or without sound */
#define OMK_ANIMATION  4        /* an Amiga animation (IFF ANIM, or an animation datatype) */
#define OMK_MUSIC      5        /* a module or score (a datatype's GID_MUSIC or GID_INSTRUMENT) */
#define OMK_TEXT       6        /* a datatype's GID_TEXT */
#define OMK_DOCUMENT   7        /* a datatype's GID_DOCUMENT */
#define OMK_OTHER      8        /* a datatype knows it, in another group */

/* ---- omi_Container: the file's own format ---- */
#define OMF_UNKNOWN    0
#define OMF_MP4        OM_ID('M','P','4',' ')   /* ISO base media file (MP4, M4A, M4V) */
#define OMF_MOV        OM_ID('M','O','V',' ')   /* QuickTime */
#define OMF_3GP        OM_ID('3','G','P',' ')
#define OMF_HEIF       OM_ID('H','E','I','F')   /* HEIF and AVIF pictures (ISO base media) */
#define OMF_MATROSKA   OM_ID('M','K','V',' ')
#define OMF_WEBM       OM_ID('W','E','B','M')
#define OMF_AVI        OM_ID('A','V','I',' ')
#define OMF_MPEGPS     OM_ID('M','P','S',' ')   /* MPEG program stream (.mpg, .vob) */
#define OMF_MPEGTS     OM_ID('M','T','S',' ')   /* MPEG transport stream (.ts, .m2ts) */
#define OMF_MPEGVIDEO  OM_ID('M','P','V',' ')   /* an MPEG-1 or MPEG-2 video stream on its own (.m1v, .m2v) */
#define OMF_H264       OM_ID('2','6','4',' ')   /* an H.264 stream on its own (.h264) */
#define OMF_H265       OM_ID('2','6','5',' ')   /* an HEVC stream on its own (.h265, .hevc) */
#define OMF_OGG        OM_ID('O','G','G',' ')
#define OMF_WAV        OM_ID('W','A','V','E')
#define OMF_AIFF       OM_ID('A','I','F','F')
#define OMF_AIFC       OM_ID('A','I','F','C')
#define OMF_8SVX       OM_ID('8','S','V','X')
#define OMF_16SV       OM_ID('1','6','S','V')
#define OMF_ANIM       OM_ID('A','N','I','M')
#define OMF_FLAC       OM_ID('F','L','A','C')
#define OMF_MP3        OM_ID('M','P','3',' ')   /* MPEG audio frames (layers 1, 2 and 3) */
#define OMF_ADTS       OM_ID('A','D','T','S')   /* AAC in ADTS frames (.aac) */
#define OMF_ILBM       OM_ID('I','L','B','M')
#define OMF_PNG        OM_ID('P','N','G',' ')
#define OMF_JPEG       OM_ID('J','P','E','G')
#define OMF_GIF        OM_ID('G','I','F',' ')
#define OMF_BMP        OM_ID('B','M','P',' ')
#define OMF_WEBP       OM_ID('W','E','B','P')
#define OMF_DATATYPE   OM_ID('D','T','Y','P')   /* known only to a datatype: omi_DataType names it */

/* ---- omi_VideoCodec and omi_AudioCodec ---- */
#define OMC_NONE       0
#define OMC_OTHER      OM_ID('?','?','?','?')   /* the name field holds the file's own code for it */
/* video */
#define OMC_H264       OM_ID('H','2','6','4')
#define OMC_HEVC       OM_ID('H','E','V','C')
#define OMC_AV1        OM_ID('A','V','0','1')
#define OMC_VP8        OM_ID('V','P','8','0')
#define OMC_VP9        OM_ID('V','P','9','0')
#define OMC_MPEG1V     OM_ID('M','P','1','V')
#define OMC_MPEG2V     OM_ID('M','P','2','V')
#define OMC_MPEG4V     OM_ID('M','P','4','V')   /* MPEG-4 part 2 (DivX, Xvid) */
#define OMC_H263       OM_ID('H','2','6','3')
#define OMC_THEORA     OM_ID('T','H','E','O')
#define OMC_MJPEG      OM_ID('M','J','P','G')
#define OMC_PRORES     OM_ID('P','R','E','S')
#define OMC_VC1        OM_ID('V','C','-','1')
#define OMC_RAWVIDEO   OM_ID('R','A','W','V')
#define OMC_ILBM       OM_ID('I','L','B','M')   /* IFF ANIM's frames */
/* audio */
#define OMC_PCM        OM_ID('P','C','M',' ')   /* integer samples */
#define OMC_PCMFLOAT   OM_ID('P','C','M','F')   /* floating-point samples */
#define OMC_ALAW       OM_ID('A','L','A','W')
#define OMC_ULAW       OM_ID('U','L','A','W')
#define OMC_ADPCM      OM_ID('A','D','P','C')
#define OMC_FIBDELTA   OM_ID('F','I','B','D')   /* 8SVX's Fibonacci delta */
#define OMC_MP1        OM_ID('M','P','1','A')
#define OMC_MP2        OM_ID('M','P','2','A')
#define OMC_MP3        OM_ID('M','P','3','A')
#define OMC_AAC        OM_ID('A','A','C',' ')
#define OMC_AC3        OM_ID('A','C','-','3')
#define OMC_EAC3       OM_ID('E','A','C','3')
#define OMC_DTS        OM_ID('D','T','S',' ')
#define OMC_VORBIS     OM_ID('V','O','R','B')
#define OMC_OPUS       OM_ID('O','P','U','S')
#define OMC_FLAC       OM_ID('F','L','A','C')
#define OMC_ALAC       OM_ID('A','L','A','C')
#define OMC_SPEEX      OM_ID('S','P','X',' ')

/* ---- omi_Flags ---- */
#define OMIF_DURATION_ESTIMATED 0x0001  /* worked out from the bit rate, not read */
#define OMIF_TRUNCATED          0x0002  /* the file ends before its headers say it should */
#define OMIF_DECODED            0x0004  /* a datatype loaded the file to answer (OMA_Decode) */
#define OMIF_ALPHA              0x0008  /* the picture has a transparency channel */
#define OMIF_INTERLACED         0x0010  /* the video is interlaced */
#define OMIF_HAM                0x0020  /* an Amiga HAM picture or animation */
#define OMIF_EHB                0x0040  /* an Amiga Extra Half-Brite picture */
#define OMIF_VBR                0x0080  /* the sound's bit rate varies */
#define OMIF_ANIMATED           0x0100  /* a picture format holding several frames (APNG, animated WebP) */

/* ---- OMA_Error's answers ---- */
#define OMERR_OK        0
#define OMERR_NOFILE    1       /* the file couldn't be opened; IoErr() says why */
#define OMERR_NOMEM     2
#define OMERR_READ      3       /* the file couldn't be read */
#define OMERR_ARGS      4       /* no file given (OMA_Name, OMA_Lock or OMA_FileHandle) */

#ifndef OPENMEDIA_CONSTANTS_ONLY

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

/* ---- OM_ObtainMediaInfoA's tags ---- */
#define OMA_Dummy      (TAG_USER + 0x4F4D0000UL)
#define OMA_Name       (OMA_Dummy + 1)  /* CONST_STRPTR: the file, by name */
#define OMA_Lock       (OMA_Dummy + 2)  /* BPTR: a shared lock on the file (kept, not unlocked) */
#define OMA_FileHandle (OMA_Dummy + 3)  /* BPTR: an open file (kept open; its position is put back) */
#define OMA_DataTypes  (OMA_Dummy + 4)  /* BOOL, default TRUE: ask datatypes.library about what our own
                                         * parsers don't know, and name the datatype that reads a picture */
#define OMA_Decode     (OMA_Dummy + 5)  /* BOOL, default FALSE: let a datatype load the whole file when
                                         * that is the only way to learn its size, depth or length. A full
                                         * decode: seconds for a large JPEG on a 68040, and the picture's
                                         * memory while it runs. Our own parsers never need it for IFF,
                                         * PNG, JPEG, GIF, BMP, WebP or any sound or video format they know */
#define OMA_Error      (OMA_Dummy + 6)  /* LONG *: OMERR_ for a NULL answer, OMERR_OK otherwise */
#define OMA_MaxRead    (OMA_Dummy + 7)  /* ULONG: the most bytes the parsers read from the file, default
                                         * 1 MB, at least 64 KB. A damaged file costs no more than this */

/* What a file is: from OM_ObtainMediaInfoA, given back with
 * OM_ReleaseMediaInfo. The library allocates it, so later versions add
 * fields at the end: check omi_Version (or omi_Size) before reading a field
 * newer than version 1. Everything is read-only. A value not known is 0, a
 * name not known is NULL. */
struct OMMediaInfo {
    UWORD  omi_Version;         /* 1 */
    UWORD  omi_Size;            /* sizeof(struct OMMediaInfo) in that version */
    ULONG  omi_Kind;            /* OMK_ */
    ULONG  omi_Container;       /* OMF_ */
    ULONG  omi_Flags;           /* OMIF_ */
    STRPTR omi_FormatName;      /* "MP4", "Matroska", "WAV", "PNG"... never NULL ("Unknown") */
    STRPTR omi_DataType;        /* the name of the datatype that reads the file, or NULL */
    ULONG  omi_FileSizeHi;      /* the file's size in bytes, high and low longs */
    ULONG  omi_FileSizeLo;
    ULONG  omi_Duration;        /* milliseconds */
    ULONG  omi_Width;           /* pixels: a picture, a video's picture, an animation */
    ULONG  omi_Height;
    ULONG  omi_Depth;           /* bits per pixel (pictures and animations) */
    ULONG  omi_FrameRateNum;    /* frames a second = omi_FrameRateNum / omi_FrameRateDen */
    ULONG  omi_FrameRateDen;
    ULONG  omi_Frames;          /* frames, when the file says */
    ULONG  omi_VideoCodec;      /* OMC_, for the first video track */
    STRPTR omi_VideoCodecName;  /* "H.264", or the file's own code ("XVID") for OMC_OTHER */
    ULONG  omi_AudioCodec;      /* OMC_, for the first sound track */
    STRPTR omi_AudioCodecName;
    ULONG  omi_Channels;
    ULONG  omi_SampleRate;      /* Hz */
    ULONG  omi_SampleBits;      /* bits per sample, for uncompressed sound */
    ULONG  omi_BitRate;         /* bits a second, the whole file or its sound */
    UWORD  omi_VideoTracks;
    UWORD  omi_AudioTracks;
    UWORD  omi_SubtitleTracks;
    UWORD  omi_OtherTracks;
};

#endif /* OPENMEDIA_CONSTANTS_ONLY */

#endif
