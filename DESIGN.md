# OpenMedia: design

We, 4 October 2026: "can we port VLC to the Amiga based on our OpenRTG,
OpenGPU, and the not specified openmediahardware project; that work will
drive that media hardware idea, i.e. expose a graphics chip's H.264, H.265
etc hardware encoding". OpenMedia is that project. As the browser drives
OpenRTG, OpenGPU and OpenSocket, VLC drives OpenMedia.

## 1. openmedia.library

One library with a driver per piece of video hardware, in the same shape as
OpenGPU (OpenRTG's design, section 5):

- **Sessions.** A program opens a decoder or an encoder for a codec (H.264,
  H.265/HEVC, VP9, AV1, MPEG-2, VC-1, JPEG) and a profile. It asks first what
  the hardware can do: codecs, profiles, the largest size, bit depths, decode
  or encode. Each driver answers fully, partly or not, as Warp3D's queries
  do.
- **Surfaces.** Decoded frames go into surfaces in video RAM (NV12, P010 or
  RGB). OpenGPU converts, scales and composites them onto an OpenRTG screen
  with no copy through the CPU. The same surfaces feed an encoder.
- **Buffers in, frames out.** Compressed data goes in one access unit at a
  time, and frames come back with their times. The library never parses
  containers; that is the player's job.
- **The CPU fallback is a separate, optional module** (FFmpeg, LGPL, loaded at
  run time). `openmedia.library` itself stays MIT, and the fallback keeps its
  own licence.

## 2. Drivers

Drivers live in `LIBS:OpenMedia/`, named for their hardware, the way
OpenGPU's are (`ACRTG.gpu`). The names below are proposals.

| Driver | Hardware | How |
| --- | --- | --- |
| `ACRTG.media` | AmigaChrome | The runtime's media unit on the ACRTG board. Each session runs on the PC's own video hardware through VA-API (Intel and AMD) or NVDEC and NVENC, with FFmpeg on the host as the fallback. Frames land straight in the board's video RAM, so the Amiga gets fast hardware decode and encode while the PC does the work. |
| `VideoCore.media` | PiStorm | The Raspberry Pi's video decoders through Emu68, as a provider, the way OpenMulticore's providers work. A Pi 4 (BCM2711) decodes H.264 and HEVC. A Pi 5 or CM5 (BCM2712) decodes HEVC only: it has no H.264 decoder and no hardware encoder, so H.264 there goes to `CPU.media` on the Pi's ARM cores (checked 4 October 2026). |
| `CPU.media` | Any Amiga | The FFmpeg module: LGPL, its own file, its source with it. |
| Real cards | Where a chip's video engine is documented | Most classic Amiga cards have none, so the CPU module or a PiStorm is the path there. |

## 3. Its users

- **VLC for AmigaOS** (`DalsinAI/openamigavlc`): its OpenMedia decoder module,
  hardware first (H.264, then HEVC). Its video output composites OpenMedia's
  surfaces through OpenRTG and OpenGPU.
- **The screen recorder** (an AmigaChrome idea): an instance's picture encoded
  by the PC's hardware through OpenMedia's encoder.
- Any program can use it: a viewer, a video editor, a datatype.

## 4. Phases

| Phase | Delivers | Done when |
| --- | --- | --- |
| 1 | The interface and headers; `ACRTG.media` with VA-API and FFmpeg on the host; H.264 decode into a surface; a test player | A 1080p H.264 file plays smoothly on an OpenRTG monitor in AmigaChrome |
| 2 | What VLC's port needs (openamigavlc phase 2) | VLC plays a local file and a network stream through OpenMedia |
| 3 | HEVC, VP9 and AV1; encode (H.264, HEVC) | A screen recorded from an instance, encoded by the PC's hardware |
| 4 | `VideoCore.media` for the PiStorm; `CPU.media` | Playback on a PiStorm, and on a real Amiga through the CPU module |
| 5 | The Installer; Aminet (util/libs) | |

## 6. Stage 0: what a file is (version 1, 18 October 2026)

Dale, 18 October 2026: OpenFiles' Information window gets "file properties
and image format. This will come from our openmedia.library". Before any
hardware decode, the library's first call tells what a file is: a small,
stable public call that every later version keeps. It reads containers'
headers only to say what they hold; it still never takes a container apart
(section 1: that stays the player's job).

- **One call.** `OM_ObtainMediaInfoA` takes tags (`OMA_Name`, `OMA_Lock` or
  `OMA_FileHandle`; `OMA_DataTypes`, `OMA_Decode`, `OMA_MaxRead`, `OMA_Error`)
  and gives a `struct OMMediaInfo` of the library's own: the kind (picture,
  sound, video, animation...), the container and its name, the datatype that
  reads it, the size in bytes, the length, the pixel size and depth, the
  frame rate and frames, the video and sound codecs with their names, the
  channels, sample rate and bits, the bit rate and the tracks of each kind.
  `OM_ReleaseMediaInfo` gives it back; the library allocates it, so later
  versions add fields at the end (`omi_Version`, `omi_Size`).
  `OM_CodecName` names a codec. `include/openmedia/openmedia.h` is the
  interface, `docs/openmedia.doc` its autodoc.
- **Our own parsers first** (`library/om_probe.c`), written from the formats'
  public specifications and nothing else (no FFmpeg, libav or MediaInfo code):
  - **video:** MP4, QuickTime and 3GP (ISO base media), Matroska and WebM,
    AVI, MPEG program and transport streams, Ogg with Theora, and MPEG-1/2,
    H.264 and HEVC streams on their own;
  - **sound:** WAV (and RF64), AIFF and AIFF-C, IFF 8SVX and 16SV, FLAC, MP3
    and the other MPEG audio layers, AAC in ADTS, Ogg Vorbis, Opus, FLAC and
    Speex;
  - **Amiga animation:** IFF ANIM (size, depth, frames, timing, compression);
  - **pictures:** IFF ILBM (with HAM and EHB), PNG (and APNG), JPEG, GIF,
    BMP, WebP, HEIF and AVIF: the size and depth from the header alone.
- **datatypes.library next.** A file the parsers don't know goes to
  `ObtainDataType`, and its group gives the kind and its name the format.
  Pictures are asked too, so the answer names the datatype that shows them.
  With `OMA_Decode` (off by default) a datatype may load the whole file when
  that is the only way to learn a picture's size or a sound's length: a full
  decode, which costs seconds for a large JPEG on a 68040 and the picture's
  memory while it runs. The autodoc says so; OpenFiles asks for it only on a
  picture under 1 MB.
- **A damaged or hostile file costs little.** Every read goes through one
  reader that stops at the file's end and at a budget (1 MB unless
  `OMA_MaxRead` says otherwise, never under 64 KB; the first 64 KB count
  once however often the parsers look at them again). Every length and count
  read from a file is checked before it is used, and every loop has a fixed
  bound, so the worst file gets a partial answer, never a hang or a crash.
  Nothing big goes on the stack: the calls run on the caller's, which may be
  4 KB (the deepest chain is about 1.5 KB; `build.sh` lists the largest
  frames on every build). The work buffers (112 KB) are allocated per call.
- **Where it lives:** `LIBS:openmedia.library` on disk (40 KB). The ROM is
  full; OpenUp installs the library with OpenFiles.
- **Tests.** `tests/host/run.sh` makes 59 sample files with FFmpeg's
  command-line tool and `make_iff.py` (none is kept in the repository, and no
  FFmpeg code is in the library), checks each against `samples.txt` (497
  checks: the values come from how each file was made, and the files of 64 KB
  or more again with the least budget), then cuts every file
  short at hundreds of lengths, changes bytes in its headers thousands of
  times, and feeds made-up hostile headers (boxes inside boxes, sizes of
  2^63, a million empty elements, lacing that never ends), all under
  AddressSanitizer, checking each probe stays within its read budget.
  `C:OpenMediaInfo` (`tools/`) prints the answer on the Amiga, one line a
  file with `LINE` in the host tests' form, so a lab's answers compare with
  the host's.

### The LVOs, now and to come

| LVO | Call | Version |
| --- | --- | --- |
| -30 | `OM_ObtainMediaInfoA(tags)(a0)` | 1 |
| -36 | `OM_ReleaseMediaInfo(info)(a0)` | 1 |
| -42 | `OM_CodecName(codec)(d0)` | 1 |
| -48, -54 | kept for stage 0's own growth (`==reserve 2`) | |
| -60 | `OM_QueryA`: what the hardware can do (section 1) | 2 |
| -66 | `OM_OpenDecoderA` | 2 |
| -72 | `OM_OpenEncoderA` | 2 |
| -78 | `OM_CloseSession` | 2 |
| -84 | `OM_SubmitA`: one access unit in | 2 |
| -90 | `OM_GetFrameA`: a frame out, with its time | 2 |
| -96 | `OM_ReleaseFrame` | 2 |
| -102 | `OM_AllocSurfaceA` | 2 |
| -108 | `OM_FreeSurface` | 2 |
| -114 | `OM_SessionAttrsA` | 2 |

The version 2 names and their order are the plan, not yet in the SFD: they
are written in when phase 1 builds them, at these offsets.

## 7. Licences

The library, drivers, headers and test tools are MIT, Copyright (c) 2026
AmigaChrome Team. The FFmpeg fallback module is LGPL, built and shipped as its
own file with its source. VLC's port is in its own repository; our files
there are MIT, and VLC's keep VLC's licences.
