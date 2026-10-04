# OpenMedia: design

Dale, 4 October 2026: "can we port VLC to the Amiga based on our OpenRTG,
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
| `VideoCore.media` | PiStorm | The Raspberry Pi's VideoCore decoders (H.264, and HEVC on a Pi 4 and Pi 5) through Emu68, as a provider, the way OpenMulticore's providers work. |
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

## 5. Licences

The library, drivers, headers and test tools are MIT, Copyright (c) 2026
Dalsin Limited. The FFmpeg fallback module is LGPL, built and shipped as its
own file with its source. VLC's port is in its own repository; our files
there are MIT, and VLC's keep VLC's licences.
