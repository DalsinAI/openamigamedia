# Restart: OpenMedia

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenMedia: openmedia.library gives Amiga programs a graphics chip's video engines (H.264, H.265 and more, decode and encode) through one interface, with frames landing in OpenRTG video RAM.

## Where it stands

Designed, not built. It waits on OpenGPU and the ACRTG v3 ring (amigachrome #213).

## Merged lately

- #1 (a116667, 2026-10-06): Credit who made OpenMedia: CONTRIBUTORS.md

## Open pull requests

- None.

## Next step

1. Phase 1 after #213 lands: a host-decode driver for AmigaChrome, feeding VLC's port.

## Waiting on @SacredTrees

- Nothing.

## Who owns it

No active thread (follows OpenGPU).

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenGPU_Mesa_GPU_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
