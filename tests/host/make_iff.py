"""Writes small Amiga IFF files for the host tests, from EA IFF 85 and its
ILBM, 8SVX and ANIM forms: two pictures, an animation and three sounds.
   python3 make_iff.py OUT_DIR
MIT, Copyright (c) 2026 AmigaChrome Team."""
import os
import struct
import sys


def chunk(cid, data):
    pad = b"\0" if len(data) & 1 else b""
    return cid + struct.pack(">I", len(data)) + data + pad


def form(ftype, *chunks):
    body = ftype + b"".join(chunks)
    return b"FORM" + struct.pack(">I", len(body)) + body


def bmhd(w, h, planes):
    # w, h, x, y, nPlanes, masking, compression, pad, transparentColor, xAspect, yAspect, pageWidth, pageHeight
    return chunk(b"BMHD", struct.pack(">HHhhBBBBHBBhh", w, h, 0, 0, planes, 0, 0, 0, 0, 10, 11, w, h))


def ilbm(w, h, planes, camg=None):
    row = ((w + 15) // 16) * 2
    parts = [bmhd(w, h, planes), chunk(b"CMAP", bytes(3 * min(1 << planes, 32)))]
    if camg is not None:
        parts.append(chunk(b"CAMG", struct.pack(">I", camg)))
    parts.append(chunk(b"BODY", bytes(row * planes * h)))
    return form(b"ILBM", *parts)


def anhd(op, reltime):
    # operation, mask, w, h, x, y, abstime, reltime, interleave, pad0, bits, pad[16]
    return chunk(b"ANHD", struct.pack(">BBHHhhIIBBI16s", op, 0, 0, 0, 0, 0, 0, reltime, 0, 0, 0, bytes(16)))


def anim(frames, reltime):
    first = ilbm(320, 200, 4)
    deltas = [form(b"ILBM", anhd(5, reltime), chunk(b"DLTA", bytes(64))) for _ in range(frames - 1)]
    return form(b"ANIM", first, *deltas)


def svx(rate, samples, stereo=False, sixteen=False):
    # oneShotHiSamples, repeatHiSamples, samplesPerHiCycle, samplesPerSec, ctOctave, sCompression, volume
    vhdr = chunk(b"VHDR", struct.pack(">IIIHBBI", samples, 0, 32, rate, 1, 0, 0x10000))
    parts = [vhdr, chunk(b"NAME", b"tone")]
    if stereo:
        parts.append(chunk(b"CHAN", struct.pack(">I", 6)))
    width = 2 if sixteen else 1
    parts.append(chunk(b"BODY", bytes(samples * width * (2 if stereo else 1))))
    return form(b"16SV" if sixteen else b"8SVX", *parts)


def main():
    out = sys.argv[1]
    files = {
        "picture_ehb.iff": ilbm(320, 200, 6, 0x80),
        "picture_ham.iff": ilbm(320, 256, 6, 0x800),
        "picture_24.iff": ilbm(64, 48, 24),
        "anim5.anim": anim(5, 2),
        "sound.8svx": svx(8363, 16726),
        "sound_stereo.8svx": svx(8000, 8000, stereo=True),
        "sound.16sv": svx(22050, 44100, sixteen=True),
    }
    for name, data in files.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
