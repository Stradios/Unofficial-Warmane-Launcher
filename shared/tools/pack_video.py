#!/usr/bin/env python3
"""Turn woods.mp4 into assets/woods.bin: a pre-cropped (1216x452 - the launcher shows 451 rows -, same crop the
website uses: object-fit:cover) JPEG frame sequence the launcher plays in a loop.
Format (little endian): u32 fps, u32 count, u32 offsets[count+1], then JPEG data."""
import struct, subprocess, sys, tempfile, glob, os
src, out = sys.argv[1], sys.argv[2]
fps = int(sys.argv[3]) if len(sys.argv) > 3 else 20
q = sys.argv[4] if len(sys.argv) > 4 else "6"
with tempfile.TemporaryDirectory() as d:
    subprocess.check_call(["ffmpeg", "-v", "error", "-i", src, "-an",
        "-vf", f"fps={fps},scale=1216:-2:flags=lanczos,crop=1216:452:0:(ih-452)/2",
        "-q:v", q, os.path.join(d, "f%04d.jpg")])
    files = sorted(glob.glob(os.path.join(d, "f*.jpg")))
    blobs = [open(f, "rb").read() for f in files]
offs, pos = [], 0
for b in blobs:
    offs.append(pos); pos += len(b)
offs.append(pos)
with open(out, "wb") as f:
    f.write(struct.pack("<II", fps, len(blobs)))
    f.write(struct.pack(f"<{len(offs)}I", *offs))
    for b in blobs: f.write(b)
print(len(blobs), "frames,", os.path.getsize(out) // 1024, "KB")
