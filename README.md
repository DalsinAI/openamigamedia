# OpenMedia

Video hardware for Amiga programs: `openmedia.library` exposes a graphics
chip's video engines (H.264, H.265/HEVC and more, decode and encode) through
one interface with a driver per chip. Decoded frames go straight into
OpenRTG's video RAM, where OpenGPU converts, scales and composites them, and
the CPU never copies a frame.

Dale, 4 October 2026: "expose a graphics chip's H.264, H.265 etc hardware
encoding", with VLC's port driving it. VLC for AmigaOS
(https://github.com/DalsinAI/openamigavlc) is its first user, and its needs
decide what OpenMedia offers first. `DESIGN.md` is the design.

Status, 4 October 2026: designed, not built.

Part of the Open family, beside OpenRTG and OpenGPU
(https://github.com/DalsinAI/openamigartg), OpenSocket and OpenMulticore.
It works on AmigaChrome, PiStorm and real Amigas.

## Licence

MIT, Copyright (c) 2026 Dalsin Limited (`LICENSE`). This covers the library,
the drivers, the headers and the test tools. The CPU fallback is a separate
optional module built on FFmpeg; it keeps FFmpeg's LGPL and ships as its own
file with its source.

If you use or build on this work, we ask (we do not require) that you credit
Dalsin Limited and AmigaChrome, for example "based on OpenMedia by Dalsin
Limited".
