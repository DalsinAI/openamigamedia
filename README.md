# OpenMedia

Video hardware for Amiga programs: `openmedia.library` exposes a graphics
chip's video engines (H.264, H.265/HEVC and more, decode and encode) through
one interface with a driver per chip. Decoded frames go straight into
OpenRTG's video RAM, where OpenGPU converts, scales and composites them, and
the CPU never copies a frame.

We, 4 October 2026: "expose a graphics chip's H.264, H.265 etc hardware
encoding", with VLC's port driving it. VLC for AmigaOS
(https://github.com/DalsinAI/openamigavlc) is its first user, and its needs
decide what OpenMedia offers first. `DESIGN.md` is the design.

Status, 18 October 2026: **stage 0 is built**: `openmedia.library` 1.0
tells what a file is. One call (`OM_ObtainMediaInfoA`) reads a file's
headers and gives its kind, container, codecs, pixel size and depth,
length, channels and rate; our own parsers know MP4, QuickTime, Matroska
and WebM, AVI, MPEG program and transport streams, Ogg, WAV, AIFF, IFF
8SVX, ANIM and ILBM, FLAC, MP3, AAC, PNG, JPEG, GIF, BMP, WebP and AVIF, and
datatypes.library answers for the rest. OpenFiles' Information window is
its first user. The decode and encode sessions (phase 1) come next, at the
LVOs `DESIGN.md` section 6 keeps for them.

- `include/openmedia/openmedia.h`: the interface; `docs/openmedia.doc`: the autodoc
- `library/`: the library (`build.sh`: the os32-gcc16 stove, the 68040), its SFD and the parsers
- `tools/openmediainfo.c`: `C:OpenMediaInfo FILE/M`, the answer from the Shell
- `tests/host/run.sh`: the parsers' tests on samples made with FFmpeg's command-line tool, and fuzz cases

It goes in `LIBS:` (about 40 KB).

Part of the Open family, beside OpenRTG and OpenGPU
(https://github.com/DalsinAI/openamigartg), OpenSocket and OpenMulticore.
It works on AmigaChrome, PiStorm and real Amigas.

## Licence

MIT, Copyright (c) 2026 AmigaChrome Team (`LICENSE`). This covers the library,
the drivers, the headers and the test tools. The CPU fallback is a separate
optional module built on FFmpeg; it keeps FFmpeg's LGPL and ships as its own
file with its source.

If you use or build on this work, we ask (we do not require) that you credit
the AmigaChrome Team, for example "based on OpenMedia by the AmigaChrome
Team".

## Contributors

OpenMedia is created and maintained by [SacredTrees](https://github.com/SacredTrees) with the AmigaChrome agent team, copyright AmigaChrome Team. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
