# Restart: OpenMedia

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenMedia: openmedia.library gives Amiga programs a graphics chip's video engines (H.264, H.265 and more, decode and encode) through one interface, with frames landing in OpenRTG video RAM.

## Where it stands (18 October 2026)

Stage 0 is built (DESIGN.md section 6): `openmedia.library` 1.0 with `OM_ObtainMediaInfoA`, `OM_ReleaseMediaInfo` and `OM_CodecName`, the parsers' host tests (57 samples, 470 checks, fuzzing), and `C:OpenMediaInfo`; tested in a lab on the open ROM and on Kickstart 3.2.3. OpenFiles 0.4.1's Information window is its first user. The blockers of phase 1, OpenGPU and the ACRTG v3 ring (amigachrome #213), are done.

## Merged lately

- #1 (a116667, 2026-10-06): Credit who made OpenMedia: CONTRIBUTORS.md

## Open pull requests

- None.

## Next step

1. Phase 1: a host-decode driver for AmigaChrome, feeding VLC's port; its calls take the LVOs from -60 (DESIGN.md section 6).
2. OpenUp installs `LIBS:openmedia.library` with OpenFiles.

## Waiting on @SacredTrees

- Nothing.

## Who owns it

No active thread (follows OpenGPU).

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenGPU_Mesa_GPU_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
