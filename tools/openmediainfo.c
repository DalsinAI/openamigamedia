/* OpenMediaInfo: what openmedia.library says a file is, from the Shell.
 *
 *   OpenMediaInfo FILE/M/A,DECODE/S,NODATATYPES/S,MAXREAD/K/N,LINE/S
 *
 * FILE takes names and patterns ("Work:Films/#?.mp4").
 *
 * DECODE lets a datatype load a picture or sound when that is the only way
 * to learn its size (OMA_Decode); NODATATYPES asks only openmedia.library's
 * own parsers; MAXREAD is OMA_MaxRead in bytes; LINE prints one line a
 * file, in the host tests' form, so a lab's answers compare with the host's.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/dosasl.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/openmedia.h>

struct Library *OpenMediaBase;

static const char version[] __attribute__((used)) = "$VER: OpenMediaInfo 1.0 (18.10.2026) MIT, Copyright (c) 2026 AmigaChrome Team";

static const char *const kinds[] = { "unknown", "picture", "sound", "video", "animation", "music", "text", "document", "other" };

/* A four-letter code as text ("-" for none, '_' for a space at the end, as the host tests write it). */
static void cc(char *out, ULONG v, int under)
{
    int i;
    if (!v) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    for (i = 0; i < 4; i++) {
        char c = (char)(v >> (24 - 8 * i));
        out[i] = c == ' ' && under ? '_' : c;
    }
    out[4] = 0;
    for (i = 3; under && i > 0 && out[i] == '_'; i--)
        out[i] = 0;
}

static void show(const char *name, struct OMMediaInfo *mi, int line)
{
    char c[8], v[8], a[8];
    const char *k = mi->omi_Kind < 9 ? kinds[mi->omi_Kind] : "?";
    cc(c, mi->omi_Container, 1);
    cc(v, mi->omi_VideoCodec, 1);
    cc(a, mi->omi_AudioCodec, 1);
    if (line) {
        Printf("%s ok %s %s %s v=%s %s a=%s %s %lux%lu d%lu fps=%lu/%lu fr=%lu ch=%lu %luHz %lubit %lums %lubps tr=%lu/%lu/%lu/%lu fl=%lx dt=%s\n",
               (ULONG)name, (ULONG)k, (ULONG)c, (ULONG)mi->omi_FormatName, (ULONG)v,
               (ULONG)(mi->omi_VideoCodecName ? mi->omi_VideoCodecName : (STRPTR)"-"), (ULONG)a,
               (ULONG)(mi->omi_AudioCodecName ? mi->omi_AudioCodecName : (STRPTR)"-"), mi->omi_Width, mi->omi_Height,
               mi->omi_Depth, mi->omi_FrameRateNum, mi->omi_FrameRateDen, mi->omi_Frames, mi->omi_Channels,
               mi->omi_SampleRate, mi->omi_SampleBits, mi->omi_Duration, mi->omi_BitRate, (ULONG)mi->omi_VideoTracks,
               (ULONG)mi->omi_AudioTracks, (ULONG)mi->omi_SubtitleTracks, (ULONG)mi->omi_OtherTracks, mi->omi_Flags,
               (ULONG)(mi->omi_DataType ? mi->omi_DataType : (STRPTR)"-"));
        return;
    }
    Printf("%s\n  Kind        %s\n  Format      %s\n", (ULONG)name, (ULONG)k, (ULONG)mi->omi_FormatName);
    if (mi->omi_DataType)
        Printf("  Datatype    %s\n", (ULONG)mi->omi_DataType);
    Printf("  Size        %lu bytes\n", mi->omi_FileSizeLo);
    if (mi->omi_Duration) {
        ULONG ms = mi->omi_Duration;
        Printf("  Length      %lu:%02lu:%02lu.%03lu%s\n", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000,
               (ULONG)(mi->omi_Flags & OMIF_DURATION_ESTIMATED ? " (estimated)" : ""));
    }
    if (mi->omi_Width)
        Printf("  Pixels      %lu x %lu, %lu bits\n", mi->omi_Width, mi->omi_Height, mi->omi_Depth);
    if (mi->omi_FrameRateNum)
        Printf("  Frame rate  %lu/%lu a second\n", mi->omi_FrameRateNum, mi->omi_FrameRateDen);
    if (mi->omi_Frames)
        Printf("  Frames      %lu\n", mi->omi_Frames);
    if (mi->omi_VideoCodec)
        Printf("  Video       %s\n", (ULONG)(mi->omi_VideoCodecName ? mi->omi_VideoCodecName : (STRPTR)"?"));
    if (mi->omi_AudioCodec)
        Printf("  Sound       %s, %lu channels, %lu Hz, %lu bits\n",
               (ULONG)(mi->omi_AudioCodecName ? mi->omi_AudioCodecName : (STRPTR)"?"), mi->omi_Channels,
               mi->omi_SampleRate, mi->omi_SampleBits);
    if (mi->omi_BitRate)
        Printf("  Bit rate    %lu a second\n", mi->omi_BitRate);
    if (mi->omi_VideoTracks || mi->omi_AudioTracks || mi->omi_SubtitleTracks || mi->omi_OtherTracks)
        Printf("  Tracks      %lu video, %lu sound, %lu subtitles, %lu other\n", (ULONG)mi->omi_VideoTracks,
               (ULONG)mi->omi_AudioTracks, (ULONG)mi->omi_SubtitleTracks, (ULONG)mi->omi_OtherTracks);
}

int main(void)
{
    LONG args[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rda = ReadArgs((STRPTR)"FILE/M/A,DECODE/S,NODATATYPES/S,MAXREAD/K/N,LINE/S", args, NULL);
    int rc = RETURN_OK;
    STRPTR *files;
    if (!rda) {
        PrintFault(IoErr(), (STRPTR)"OpenMediaInfo");
        return RETURN_FAIL;
    }
    if (!(OpenMediaBase = OpenLibrary((STRPTR)OPENMEDIA_NAME, OPENMEDIA_VERSION))) {
        PutStr((STRPTR)"OpenMediaInfo: openmedia.library 1 isn't in LIBS:\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    for (files = (STRPTR *)args[0]; *files && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C); files++) {
        /* a pattern ("#?.mp4") is every file it matches, in the order the drawer has them */
        struct AnchorPath *ap = AllocVec(sizeof(struct AnchorPath) + 256, MEMF_CLEAR);
        LONG more;
        if (!ap) {
            PrintFault(ERROR_NO_FREE_STORE, (STRPTR)"OpenMediaInfo");
            rc = RETURN_FAIL;               /* the rest of the files aren't looked at */
            break;
        }
        ap->ap_Strlen = 256;
        for (more = MatchFirst(*files, ap); more == 0; more = MatchNext(ap)) {
            LONG err = 0;
            struct OMMediaInfo *mi;
            if (ap->ap_Info.fib_DirEntryType > 0)
                continue;
            mi = OM_ObtainMediaInfo(OMA_Name, (ULONG)ap->ap_Buf, OMA_Decode, (ULONG)(args[1] != 0),
                                    OMA_DataTypes, (ULONG)(args[2] == 0), OMA_Error, (ULONG)&err,
                                    OMA_MaxRead, args[3] ? (ULONG) * (LONG *)args[3] : 1024UL * 1024, TAG_DONE);
            if (!mi) {
                Printf("%s: no answer (error %ld)\n", (ULONG)ap->ap_Buf, err);
                rc = RETURN_WARN;
                continue;
            }
            show((const char *)ap->ap_Info.fib_FileName, mi, args[4] != 0);
            OM_ReleaseMediaInfo(mi);
            if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
                break;
        }
        if (more != 0 && more != ERROR_NO_MORE_ENTRIES) {
            PrintFault(more, *files);
            rc = RETURN_WARN;
        }
        MatchEnd(ap);
        FreeVec(ap);
    }
    CloseLibrary(OpenMediaBase);
    FreeArgs(rda);
    (void)version;
    return rc;
}
