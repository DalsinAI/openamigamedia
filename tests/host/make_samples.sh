#!/bin/sh
# Makes the host tests' sample files in OUT (default ./samples): short
# videos, sounds and pictures in every format openmedia.library's parsers
# read, made with FFmpeg's command-line tool, and the Amiga IFF files with
# make_iff.py. FFmpeg only makes the files: none of its code is in
# openmedia.library, and no sample is kept in the repository.
#   tests/host/make_samples.sh [OUT]
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/samples}
mkdir -p "$OUT"
cd "$OUT"
F="ffmpeg -hide_banner -loglevel error -y"
V="-f lavfi -i testsrc=size=320x240:rate=25:duration=2"
A="-f lavfi -i sine=frequency=440:sample_rate=48000:duration=2"
A44="-f lavfi -i sine=frequency=440:sample_rate=44100:duration=2"
X264="-c:v libx264 -preset ultrafast -pix_fmt yuv420p"

# ISO base media
$F $V $A $X264 -c:a aac -ac 2 -shortest mp4_h264_aac.mp4
$F $V $A $X264 -c:a aac -ac 2 -shortest -movflags +faststart mp4_faststart.mp4
$F $V $A -c:v mjpeg -c:a pcm_s16le -ac 2 -shortest -f mov mov_mjpeg_pcm.mov
$F $V $A -c:v libx265 -preset ultrafast -x265-params log-level=none -c:a libmp3lame -ac 2 -shortest mp4_hevc_mp3.mp4
$F $V $A -c:v libsvtav1 -preset 12 -svtav1-params verbosity=0 -c:a libopus -ac 2 -shortest mp4_av1_opus.mp4 2>/dev/null
$F $A -c:a alac m4a_alac.m4a
$F $A -c:a aac -ac 2 m4a_aac.m4a
$F -f lavfi -i testsrc=size=176x144:rate=15:duration=2 -f lavfi -i sine=sample_rate=8000:duration=2 -c:v h263 -c:a aac -ac 1 -ar 8000 -shortest 3gp_h263_aac.3gp
$F $V $A $X264 -c:a aac -ac 2 -shortest -movflags frag_keyframe+empty_moov mp4_fragmented.mp4
$F $V -frames:v 1 -c:v libaom-av1 -cpu-used 8 -still-picture 1 -f avif picture.avif 2>/dev/null || true
# Matroska and WebM
$F $V $A $X264 -c:a flac -shortest mkv_h264_flac.mkv
$F $V $A -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -c:a libopus -ac 2 -shortest webm_vp9_opus.webm
$F $V $A -c:v libvpx -deadline realtime -cpu-used 8 -c:a libvorbis -ac 2 -shortest webm_vp8_vorbis.webm
$F $V $A -c:v mpeg4 -c:a ac3 -ac 2 -shortest mkv_mpeg4_ac3.mkv
$F $V $A $X264 -c:a aac -ac 2 -shortest -f matroska - > mkv_live.mkv
$F $A -c:a libopus mka_opus.mka
# AVI
$F $V $A -c:v mpeg4 -vtag XVID -c:a libmp3lame -ac 2 -shortest avi_xvid_mp3.avi
$F $V $A -c:v mjpeg -c:a pcm_s16le -ac 2 -shortest avi_mjpeg_pcm.avi
# MPEG program and transport streams, and bare video streams
$F $V $A -c:v mpeg1video -b:v 1M -c:a mp2 -ac 2 -shortest -f mpeg mpg_mpeg1_mp2.mpg
$F $V $A -c:v mpeg2video -b:v 1M -c:a ac3 -ac 2 -shortest -f vob vob_mpeg2_ac3.vob
$F $V $A $X264 -c:a aac -ac 2 -shortest -f mpegts ts_h264_aac.ts
$F $V $A -c:v mpeg2video -b:v 1M -c:a mp2 -ac 2 -shortest -f mpegts ts_mpeg2_mp2.ts
$F $V $A $X264 -c:a ac3 -ac 2 -shortest -f mpegts -mpegts_m2ts_mode 1 m2ts_h264_ac3.m2ts
$F $V $A -c:v libx265 -preset ultrafast -x265-params log-level=none -c:a eac3 -ac 2 -shortest -f mpegts ts_hevc_eac3.ts
$F $V $A $X264 -c:a ac3 -ac 6 -shortest -f mpegts ts_h264_ac3_51.ts       # 5.1: the LFE channel counts
$F $V $A $X264 -c:a eac3 -ac 6 -shortest -f mpegts ts_h264_eac3_51.ts
$F $V -c:v mpeg2video -f mpeg2video video.m2v
$F $V $X264 -f h264 video.h264
# Ogg
$F $V $A -c:v libtheora -q:v 5 -c:a libvorbis -ac 2 -shortest ogv_theora_vorbis.ogv
$F $V -f lavfi -i sine=frequency=440:sample_rate=48000:duration=1 -c:v libtheora -q:v 5 -c:a libvorbis -ac 2 ogv_short_sound.ogv   # the longer stream's length
$F $A -c:a libvorbis -ac 2 ogg_vorbis.ogg
$F $A -c:a libopus -ac 2 opus.opus
$F $A44 -c:a flac -f ogg oga_flac.oga
$F -f lavfi -i sine=sample_rate=16000:duration=2 -c:a libspeex -f ogg speex.spx
# Sounds
$F $A -ac 2 -c:a pcm_s16le wav_pcm16.wav
$F $A -c:a pcm_f32le wav_float.wav
$F $A -ac 6 -c:a pcm_s16le wav_ext6.wav
$F $A44 -ac 2 -c:a libmp3lame -f wav wav_mp3.wav
$F $A44 -ac 2 -c:a pcm_s16be aiff_pcm16.aiff
$F $A44 -ac 2 -c:a pcm_s16le -f aiff aifc_sowt.aifc
$F $A44 -ac 2 -c:a flac flac.flac
$F $A44 -ac 2 -c:a libmp3lame -b:a 128k mp3_cbr.mp3
$F $A44 -ac 2 -c:a libmp3lame -q:a 4 mp3_vbr.mp3
$F $A44 -ac 1 -c:a libmp3lame -b:a 64k -write_xing 0 -id3v2_version 0 mp3_bare.mp3
$F $A -ac 2 -c:a mp2 -b:a 192k -f mp2 audio.mp2
$F $A -ac 2 -c:a aac -f adts audio.aac
# Pictures
$F $V -frames:v 1 -pix_fmt rgb24 picture.png
$F $V -frames:v 1 -pix_fmt rgba picture_rgba.png
$F $V -frames:v 1 picture.jpg
$F $V -frames:v 1 picture.gif
$F $V -frames:v 1 -pix_fmt bgr24 picture.bmp
$F $V -frames:v 1 -c:v libwebp -lossless 0 picture.webp
$F $V -frames:v 1 -c:v libwebp -lossless 1 picture_lossless.webp
# The Amiga's own IFF files
python3 -I "$HERE/make_iff.py" "$OUT"
ls | wc -l
