/* openmedia.library 1.0: stage 0, what a file is (DESIGN.md section 6).
 *
 * OM_ObtainMediaInfoA opens the file (by name, from a lock, or a file the
 * caller has open), lets om_probe.c's parsers read its headers within a
 * read budget (1 MB unless OMA_MaxRead says otherwise), and asks
 * datatypes.library about anything they don't know, and about pictures, so
 * the answer names the datatype that reads it. With OMA_Decode the datatype
 * may load the whole file when that is the only way to learn a picture's
 * size or a sound's length; without it nothing is ever decoded.
 *
 * The answer is a block of the library's own (struct OMMediaInfo and the
 * names it points to), given back with OM_ReleaseMediaInfo, so later
 * versions can add fields at the end. OM_CodecName names a codec.
 *
 * The calls run on the caller's stack and use little of it (well under
 * 2 KB); with OMA_Decode the datatype's own needs come on top. Every
 * buffer is allocated per call; the library keeps no writable globals.
 * Built bare (no startup code or C library) by library/build.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <utility/tagitem.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <datatypes/animationclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/datatypes.h>

#include "../include/openmedia/openmedia.h"
#include "om_probe.h"

#define REG(r, decl) register decl __asm(#r)   /* an argument in a register */
#define LIB_VERSION 1
#define LIB_REVISION 0

/* sound.datatype V44's sample layout, when the system's headers predate it */
#ifndef SDTA_SampleType
#define SDTA_SampleType (SDTA_Dummy + 30)
#endif
#define SAMPLETYPE_STEREO_8 1
#define SAMPLETYPE_MONO_16 2
#define SAMPLETYPE_STEREO_16 3

struct OpenMediaBase {
    struct Library lib;
    BPTR seglist;
    struct ExecBase *sys;
    struct DosLibrary *dos;
};

/* The block OM_ObtainMediaInfoA gives: the public struct, then its names. */
struct omi_block {
    struct OMMediaInfo mi;
    ULONG magic;
    char format[OM_NAME], datatype[OM_NAME], vname[OM_NAME], aname[OM_NAME];
};
#define OMI_MAGIC 0x4F4D4931UL      /* 'OMI1' */

#define EXEC(base) struct ExecBase *SysBase = (base)->sys
#define DOS(base) struct DosLibrary *DOSBase = (base)->dos

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "openmedia.library";
static const char lib_id[] = "openmedia.library 1.0 (18.10.2026) OpenMedia, AmigaChrome Team\r\n";
static const char lib_ver[] __attribute__((used)) = "\0$VER: openmedia.library 1.0 (18.10.2026) OpenMedia, AmigaChrome Team";

static struct Library *lib_init(REG(d0, struct OpenMediaBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OpenMediaBase *base));
static BPTR lib_close(REG(a6, struct OpenMediaBase *base));
static BPTR lib_expunge(REG(a6, struct OpenMediaBase *base));
static ULONG lib_null(void);
static struct OMMediaInfo *OM_ObtainMediaInfoA(REG(a0, struct TagItem *tags), REG(a6, struct OpenMediaBase *base));
static void OM_ReleaseMediaInfo(REG(a0, struct OMMediaInfo *info), REG(a6, struct OpenMediaBase *base));
static CONST_STRPTR OM_CodecName(REG(d0, ULONG codec), REG(a6, struct OpenMediaBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    /* stage 0, -30 to -42 */
    (APTR)OM_ObtainMediaInfoA, (APTR)OM_ReleaseMediaInfo, (APTR)OM_CodecName,
    /* -48 and -54: kept for stage 0 (==reserve 2 in the SFD); the sessions start at -60 */
    (APTR)lib_null, (APTR)lib_null,
    (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct OpenMediaBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

static struct Library *lib_init(REG(d0, struct OpenMediaBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    struct ExecBase *SysBase = sys;
    base->sys = sys;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    if (!(base->dos = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37))) {
        FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
        return NULL;
    }
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct OpenMediaBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct OpenMediaBase *base))
{
    if (--base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct OpenMediaBase *base))
{
    EXEC(base);
    BPTR seg = base->seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    Remove(&base->lib.lib_Node);
    CloseLibrary((struct Library *)base->dos);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seg;
}

static ULONG lib_null(void) { return 0; }

/* ---- reading the file, for om_probe ---- */

struct reader {
    struct DosLibrary *dos;
    BPTR fh;
    uint64_t at;                /* where the file is now, or ~0 when not known */
};

static long read_at(void *user, uint64_t pos, void *buf, long n)
{
    struct reader *r = user;
    struct DosLibrary *DOSBase = r->dos;
    LONG got;
    if (pos > 0x7FFFFFFFUL)
        return -1;
    if (r->at != pos) {
        if (Seek(r->fh, (LONG)pos, OFFSET_BEGINNING) < 0) {
            r->at = ~0ULL;
            return -1;
        }
    }
    got = Read(r->fh, buf, n);
    if (got < 0) {
        r->at = ~0ULL;
        return -1;
    }
    r->at = pos + (uint64_t)got;
    return got;
}

/* ---- tags, without utility.library ---- */

static ULONG tag_data(struct TagItem *tags, ULONG tag, ULONG def)
{
    int n;
    for (n = 0; tags && n < 256; n++) {
        if (tags->ti_Tag == TAG_DONE)
            break;
        if (tags->ti_Tag == TAG_MORE) {
            tags = (struct TagItem *)tags->ti_Data;
            continue;
        }
        if (tags->ti_Tag == TAG_SKIP) {
            tags += 1 + tags->ti_Data;
            continue;
        }
        if (tags->ti_Tag == tag)
            return tags->ti_Data;
        tags++;
    }
    return def;
}

static void copy_name(char *dst, const char *src)
{
    int i = 0;
    while (src && src[i] && i < OM_NAME - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* ---- datatypes.library: what it knows, and with OMA_Decode what loading the file says ---- */

static ULONG group_kind(ULONG gid)
{
    switch (gid) {
    case GID_PICTURE: return OMK_PICTURE;
    case GID_SOUND: return OMK_SOUND;
    case GID_INSTRUMENT: case GID_MUSIC: return OMK_MUSIC;
    case GID_ANIMATION: return OMK_ANIMATION;
    case GID_MOVIE: return OMK_VIDEO;
    case GID_TEXT: return OMK_TEXT;
    case GID_DOCUMENT: return OMK_DOCUMENT;
    }
    return OMK_OTHER;
}

static void ask_datatypes(struct OpenMediaBase *base, BPTR lock, om_info *o, int known, int decode, char *dtname,
                          char *path, LONG pathsize)
{
    EXEC(base);
    DOS(base);
    struct Library *DataTypesBase;
    struct DataType *dtn;
    ULONG gid = 0;
    if (!(DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39)))
        return;
    if ((dtn = ObtainDataType(DTST_FILE, (APTR)lock, TAG_DONE))) {
        struct DataTypeHeader *dth = dtn->dtn_Header;
        gid = dth->dth_GroupID;
        copy_name(dtname, (const char *)dth->dth_Name);
        ReleaseDataType(dtn);
        if (!known) {
            o->kind = group_kind(gid);
            o->container = OMF_DATATYPE;
            copy_name(o->format, dtname);
        }
    }
    /* loading the file is the only way left to learn the size or the length */
    if (decode && gid && ((gid == GID_PICTURE && !o->width) || (!known && (gid == GID_SOUND || gid == GID_ANIMATION))) &&
        NameFromLock(lock, (STRPTR)path, pathsize)) {
        Object *obj = NewDTObject((APTR)path, DTA_SourceType, DTST_FILE, DTA_GroupID, gid, PDTA_Remap, FALSE, TAG_DONE);
        if (obj) {
            if (gid == GID_PICTURE) {
                struct BitMapHeader *bmh = NULL;
                if (GetDTAttrs(obj, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) && bmh) {
                    o->width = bmh->bmh_Width;
                    o->height = bmh->bmh_Height;
                    o->depth = bmh->bmh_Depth;
                }
            } else if (gid == GID_SOUND) {
                ULONG len = 0, rate = 0, period = 0, type = 0;
                GetDTAttrs(obj, SDTA_SampleLength, (ULONG)&len, SDTA_SamplesPerSec, (ULONG)&rate,
                           SDTA_Period, (ULONG)&period, TAG_DONE);
                if (GetDTAttrs(obj, SDTA_SampleType, (ULONG)&type, TAG_DONE) != 1)
                    type = 0;
                if (!rate && period)
                    rate = 3546895UL / period;  /* the PAL colour clock over the Paula period */
                o->rate = rate;
                o->channels = type == SAMPLETYPE_STEREO_8 || type == SAMPLETYPE_STEREO_16 ? 2 : 1;
                o->bits = type == SAMPLETYPE_MONO_16 || type == SAMPLETYPE_STEREO_16 ? 16 : 8;
                if (rate)
                    o->duration_ms = (ULONG)((uint64_t)len * 1000 / rate);
                o->atracks = 1;
            } else {
                ULONG w = 0, h = 0, d = 0, frames = 0, fps = 0;
                GetDTAttrs(obj, ADTA_Width, (ULONG)&w, ADTA_Height, (ULONG)&h, ADTA_Depth, (ULONG)&d,
                           ADTA_Frames, (ULONG)&frames, ADTA_FramesPerSecond, (ULONG)&fps, TAG_DONE);
                o->width = w;
                o->height = h;
                o->depth = d;
                o->frames = frames;
                if (fps) {
                    o->fps_num = fps;
                    o->fps_den = 1;
                    o->duration_ms = (ULONG)((uint64_t)frames * 1000 / fps);
                }
                o->vtracks = 1;
            }
            o->flags |= OMIF_DECODED;
            DisposeDTObject(obj);
        }
    }
    CloseLibrary(DataTypesBase);
}

/* ---- the calls ---- */

static struct OMMediaInfo *OM_ObtainMediaInfoA(REG(a0, struct TagItem *tags), REG(a6, struct OpenMediaBase *base))
{
    EXEC(base);
    DOS(base);
    STRPTR name = (STRPTR)tag_data(tags, OMA_Name, 0);
    BPTR lock = (BPTR)tag_data(tags, OMA_Lock, 0), fh = (BPTR)tag_data(tags, OMA_FileHandle, 0), mylock = 0;
    LONG *err = (LONG *)tag_data(tags, OMA_Error, 0), start = -1;
    ULONG use_dt = tag_data(tags, OMA_DataTypes, TRUE), decode = tag_data(tags, OMA_Decode, FALSE);
    ULONG budget = tag_data(tags, OMA_MaxRead, OM_BUDGET_DEFAULT);
    struct FileInfoBlock *fib = NULL;
    struct omi_block *blk = NULL;
    unsigned char *scratch = NULL;
    struct reader rdr;
    om_src src;
    om_info info;
    int own_fh = 0, known = 0;
    LONG e = OMERR_OK;
    char dtname[OM_NAME];
    dtname[0] = 0;
    if (fh) {
        start = Seek(fh, 0, OFFSET_CURRENT);
    } else if (lock) {
        BPTR dup = DupLock(lock);
        if (dup && !(fh = OpenFromLock(dup)))
            UnLock(dup);
        own_fh = fh != 0;
        if (!fh)
            e = OMERR_NOFILE;
    } else if (name) {
        own_fh = (fh = Open(name, MODE_OLDFILE)) != 0;
        if (!fh)
            e = OMERR_NOFILE;
    } else
        e = OMERR_ARGS;
    if (e)
        goto out;
    if (!(fib = AllocDosObject(DOS_FIB, NULL)) || !(scratch = AllocVec(OM_SCRATCH, MEMF_ANY)) ||
        !(blk = AllocVec(sizeof *blk, MEMF_ANY | MEMF_CLEAR))) {
        e = OMERR_NOMEM;
        goto out;
    }
    if (!ExamineFH(fh, fib)) {
        e = OMERR_READ;
        goto out;
    }
    rdr.dos = DOSBase;
    rdr.fh = fh;
    rdr.at = ~0ULL;
    src.user = &rdr;
    src.read_at = read_at;
    src.size = (ULONG)fib->fib_Size;
    src.budget = budget < OM_BUDGET_MIN ? OM_BUDGET_MIN : budget;
    src.spent = src.failed = 0;
    known = om_probe(&src, &info, scratch);
    if (src.failed && !known) {
        e = OMERR_READ;
        goto out;
    }
    if (use_dt && (!known || info.kind == OMK_PICTURE || info.kind == OMK_ANIMATION)) {
        /* datatypes.library wants a lock: the caller's, or one on the file */
        BPTR l = lock ? DupLock(lock) : DupLockFromFH(fh);
        if (!l && name)
            l = Lock(name, SHARED_LOCK);
        if (l) {
            mylock = l;
            ask_datatypes(base, l, &info, known, (int)decode, dtname, (char *)scratch, 512);
        }
    }
    blk->magic = OMI_MAGIC;
    blk->mi.omi_Version = 1;
    blk->mi.omi_Size = sizeof(struct OMMediaInfo);
    blk->mi.omi_Kind = info.kind;
    blk->mi.omi_Container = info.container;
    blk->mi.omi_Flags = info.flags;
    copy_name(blk->format, info.format[0] ? info.format : "Unknown");
    blk->mi.omi_FormatName = (STRPTR)blk->format;
    if (dtname[0]) {
        copy_name(blk->datatype, dtname);
        blk->mi.omi_DataType = (STRPTR)blk->datatype;
    }
    blk->mi.omi_FileSizeHi = (ULONG)(info.size >> 32);
    blk->mi.omi_FileSizeLo = (ULONG)info.size;
    blk->mi.omi_Duration = info.duration_ms;
    blk->mi.omi_Width = info.width;
    blk->mi.omi_Height = info.height;
    blk->mi.omi_Depth = info.depth;
    blk->mi.omi_FrameRateNum = info.fps_num;
    blk->mi.omi_FrameRateDen = info.fps_den;
    blk->mi.omi_Frames = info.frames;
    blk->mi.omi_VideoCodec = info.vcodec;
    if (info.vcodec_name[0]) {
        copy_name(blk->vname, info.vcodec_name);
        blk->mi.omi_VideoCodecName = (STRPTR)blk->vname;
    }
    blk->mi.omi_AudioCodec = info.acodec;
    if (info.acodec_name[0]) {
        copy_name(blk->aname, info.acodec_name);
        blk->mi.omi_AudioCodecName = (STRPTR)blk->aname;
    }
    blk->mi.omi_Channels = info.channels;
    blk->mi.omi_SampleRate = info.rate;
    blk->mi.omi_SampleBits = info.bits;
    blk->mi.omi_BitRate = info.bitrate;
    blk->mi.omi_VideoTracks = info.vtracks;
    blk->mi.omi_AudioTracks = info.atracks;
    blk->mi.omi_SubtitleTracks = info.stracks;
    blk->mi.omi_OtherTracks = info.otracks;
out:
    if (mylock)
        UnLock(mylock);
    if (fh && own_fh)
        Close(fh);
    else if (fh && start >= 0)
        Seek(fh, start, OFFSET_BEGINNING);  /* the caller's file, back where it was */
    if (scratch)
        FreeVec(scratch);
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    if (e && blk) {
        FreeVec(blk);
        blk = NULL;
    }
    if (err)
        *err = e;
    return blk ? &blk->mi : NULL;
}

static void OM_ReleaseMediaInfo(REG(a0, struct OMMediaInfo *info), REG(a6, struct OpenMediaBase *base))
{
    EXEC(base);
    struct omi_block *blk = (struct omi_block *)info;
    if (blk && blk->magic == OMI_MAGIC) {
        blk->magic = 0;
        FreeVec(blk);
    }
}

static CONST_STRPTR OM_CodecName(REG(d0, ULONG codec), REG(a6, struct OpenMediaBase *base))
{
    return (CONST_STRPTR)om_codec_name(codec);
}
