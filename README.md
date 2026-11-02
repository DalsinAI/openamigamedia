# GFOpenMedia

Video hardware for Amiga programs: `gfopenmedia.library` exposes a graphics
chip's video engines (H.264, H.265/HEVC and more, decode and encode) through
one interface with a driver per chip. Decoded frames go straight into
GFOpenRTG's video RAM, where GFOpenGPU converts, scales and composites them, and
the CPU never copies a frame.

We, 4 October 2026: "expose a graphics chip's H.264, H.265 etc hardware
encoding", with VLC's port driving it. VLC for AmigaOS
(https://github.com/DalsinAI/gfopenvlc) is its first user, and its needs
decide what GFOpenMedia offers first. `DESIGN.md` is the design.

Status, 18 October 2026: **stage 0 is built**: `gfopenmedia.library` 1.0
tells what a file is. One call (`OM_ObtainMediaInfoA`) reads a file's
headers and gives its kind, container, codecs, pixel size and depth,
length, channels and rate; our own parsers know MP4, QuickTime, Matroska
and WebM, AVI, MPEG program and transport streams, Ogg, WAV, AIFF, IFF
8SVX, ANIM and ILBM, FLAC, MP3, AAC, PNG, JPEG, GIF, BMP, WebP and AVIF, and
datatypes.library answers for the rest. GFOpenFiles' Information window is
its first user. The decode and encode sessions (phase 1) come next, at the
LVOs `DESIGN.md` section 6 keeps for them.

- `include/gfopenmedia/gfopenmedia.h`: the interface; `docs/gfopenmedia.doc`: the autodoc
- `library/`: the library (`build.sh`: the os32-gcc16 stove, the 68040), its SFD and the parsers
- `tools/openmediainfo.c`: `C:GFOpenMediaInfo FILE/M`, the answer from the Shell
- `tests/host/run.sh`: the parsers' tests on samples made with FFmpeg's command-line tool, and fuzz cases

It goes in `LIBS:` (about 40 KB).

Part of the GFOpen family, beside GFOpenRTG and GFOpenGPU
(https://github.com/DalsinAI/gfopenrtg), GFOpenSocket and GFOpenMulticore.
It works on AmiCradle, PiStorm and real Amigas.

## Licence

MIT, Copyright (c) 2026 AmiCradle Team (`LICENSE`). This covers the library,
the drivers, the headers and the test tools. The CPU fallback is a separate
optional module built on FFmpeg; it keeps FFmpeg's LGPL and ships as its own
file with its source.

If you use or build on this work, we ask (we do not require) that you credit
the AmiCradle Team, for example "based on GFOpenMedia by the AmiCradle
Team".

## Contributors

OpenMedia is created and maintained by [SacredTrees](https://github.com/SacredTrees) with The AmiCradle Team, copyright AmiCradle Team. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
