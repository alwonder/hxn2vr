#!/usr/bin/env python3
"""Sanity-check a uHexen2 screenshot (uncompressed TGA type 2, 24/32 bpp).

Usage: py -I tests/smoke/check_shot.py <tga> [--stereo] [--indicators-baseline=<tga>]

Exit 0 if the image is not a single flat colour. With --stereo the image is treated
as side-by-side stereo: the left and right halves must differ, but by no more than
25% of pixels with a per-channel difference > 64, and they must show the same view up
to a small horizontal disparity: the best match of the right half against the left
half over horizontal shifts of up to 1/16 of the half width (mean absolute grey
difference, 4x subsampled) must be at most STEREO_MATCH_MAX times the left half's
mean absolute deviation. A mono screenshot (two unrelated halves of one view) fails
that test: measured ratio 1.53 for a flat demo1 shot vs 0.35 for the mock-VR mirror.

--indicators-baseline compares scenario w's final stereo shot to a same-pose shot
with both indicators off. Requires new connected bluish-white marker pixels in
the left half of the left eye and red aim-dot pixels near that eye's centre.
"""
import sys

STEREO_MATCH_MAX = 0.75


def load_tga(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 18:
        raise ValueError("file too short for a TGA header")
    idlen, cmap_type, img_type = data[0], data[1], data[2]
    w = data[12] | (data[13] << 8)
    h = data[14] | (data[15] << 8)
    bpp = data[16]
    desc = data[17]
    if img_type != 2 or cmap_type != 0:
        raise ValueError("unsupported TGA (type %d, cmap %d); need uncompressed type 2" % (img_type, cmap_type))
    if bpp not in (24, 32):
        raise ValueError("unsupported bpp %d" % bpp)
    if w == 0 or h == 0:
        raise ValueError("empty image")
    bytespp = bpp // 8
    off = 18 + idlen
    need = w * h * bytespp
    if len(data) < off + need:
        raise ValueError("truncated pixel data (%d < %d)" % (len(data) - off, need))
    pix = data[off:off + need]
    # Rows are bottom-up unless bit 5 of the descriptor is set (uHexen2 writes bottom-up);
    # normalise to top-down so the checks do not depend on it.
    stride = w * bytespp
    rows = [pix[y * stride:(y + 1) * stride] for y in range(h)]
    if not (desc & 0x20):
        rows.reverse()
    return w, h, bytespp, rows


def main(argv):
    flags = [a for a in argv[1:] if a.startswith("--")]
    args = [a for a in argv[1:] if not a.startswith("--")]
    baseline_flags = [f for f in flags if f.startswith("--indicators-baseline=")]
    if (len(args) != 1 or len(baseline_flags) > 1 or
            any(f != "--stereo" and f not in baseline_flags for f in flags)):
        sys.stderr.write(__doc__)
        return 2
    stereo = "--stereo" in flags
    try:
        w, h, bpp, rows = load_tga(args[0])
    except (OSError, ValueError) as e:
        sys.stderr.write("check_shot: %s\n" % e)
        return 1

    if baseline_flags:
        if not stereo:
            sys.stderr.write("check_shot: indicator check requires --stereo\n")
            return 2
        try:
            baseline = load_tga(baseline_flags[0].split("=", 1)[1])
            if not check_indicators(w, h, bpp, rows, baseline):
                return 1
        except (OSError, ValueError) as e:
            sys.stderr.write("check_shot: indicator baseline: %s\n" % e)
            return 1

    # flat-colour check on the whole image (pixel bytes incl. alpha if present)
    first = rows[0][:bpp]
    flat = all(r == first * w for r in rows)
    ncol = len({r[x:x + 3] for r in rows[::max(1, h // 64)] for x in range(0, w * bpp, bpp)})
    print("check_shot: %dx%d %dbpp, %s" % (w, h, bpp * 8, "FLAT" if flat else "~%d distinct colours (sampled)" % ncol))
    if flat:
        sys.stderr.write("check_shot: image is a single flat colour\n")
        return 1

    if stereo:
        half = w // 2
        total = half * h
        differing = 0
        big = 0
        for r in rows:
            for x in range(half):
                a = r[x * bpp:x * bpp + 3]
                b = r[(x + half) * bpp:(x + half) * bpp + 3]
                if a != b:
                    differing += 1
                    if max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) > 64:
                        big += 1
        frac = big / total
        print("check_shot: stereo halves differ in %d px, >64 in %d px (%.1f%%)" % (differing, big, frac * 100))
        if differing == 0:
            sys.stderr.write("check_shot: left and right halves are identical\n")
            return 1
        if frac > 0.25:
            sys.stderr.write("check_shot: halves differ too much (>25% of pixels by >64)\n")
            return 1
        ratio = stereo_match(w, h, bpp, rows)
        print("check_shot: stereo match ratio %.2f (max %.2f)" % (ratio, STEREO_MATCH_MAX))
        if ratio > STEREO_MATCH_MAX:
            sys.stderr.write("check_shot: halves are not the same view (not a stereo pair)\n")
            return 1
    return 0


def check_indicators(w, h, bpp, rows, baseline):
    bw, bh, bbpp, brows = baseline
    if (bw, bh) != (w, h):
        raise ValueError("dimensions do not match the final shot")

    def largest_patch(x0, x1, y0, y1, colour):
        pixels = set()
        for y in range(int(y0 * h), int(y1 * h)):
            for x in range(int(x0 * w / 2), int(x1 * w / 2)):
                i, bi = x * bpp, x * bbpp
                b, g, r = rows[y][i:i + 3]  # TGA byte order is BGR
                before = brows[y][bi:bi + 3]
                if (colour(r, g, b) and
                        max(abs(a - old) for a, old in zip((b, g, r), before)) >= 24):
                    pixels.add((x, y))
        # Connected area rejects isolated scene/animation changes and compression-free edge noise.
        largest = 0
        while pixels:
            pending = [pixels.pop()]
            area = 0
            while pending:
                x, y = pending.pop()
                area += 1
                for p in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if p in pixels:
                        pixels.remove(p)
                        pending.append(p)
            largest = max(largest, area)
        return largest

    # Alpha 0.8 over the scene: marker RGB starts at (163, 163, 204).
    # Repeated octahedron faces move it towards (204, 204, 255).
    marker = largest_patch(0.0, 0.5, 0.1, 0.9,
                           lambda r, g, b: 155 <= r <= 220 and 155 <= g <= 220 and
                           b >= 200 and b - max(r, g) >= 15 and abs(r - g) <= 12)
    dot = largest_patch(0.3, 0.7, 0.3, 0.7,
                        lambda r, g, b: r >= 200 and g <= 115 and b <= 100 and r - g >= 100)
    print("check_shot: new indicator patches in left eye: marker %d px (min 12), dot %d px (min 4)" %
          (marker, dot))
    if marker < 12 or dot < 4:
        sys.stderr.write("check_shot: marker or aim dot missing from final screenshot\n")
        return False
    return True


def stereo_match(w, h, bpp, rows, step=4):
    """Best mean |left - shifted right| (grey, subsampled) over small horizontal shifts,
    relative to the left half's mean absolute deviation. inf if the left half is featureless."""
    half = w // 2
    hw, hh = half // step, h // step
    if hw < 2 or hh < 1:
        return float("inf")

    def grey(row, x):
        i = x * bpp
        return (row[i] + row[i + 1] + row[i + 2]) / 3.0

    left = [[grey(rows[y * step], x * step) for x in range(hw)] for y in range(hh)]
    right = [[grey(rows[y * step], half + x * step) for x in range(hw)] for y in range(hh)]
    n = hw * hh
    mean = sum(map(sum, left)) / n
    dev = sum(abs(v - mean) for r in left for v in r) / n
    if dev < 1.0:
        return float("inf")
    best = None
    smax = max(1, hw // 16)
    for s in range(-smax, smax + 1):
        lo, hi = max(0, -s), min(hw, hw - s)
        tot = 0.0
        for y in range(hh):
            lr, rr = left[y], right[y]
            tot += sum(abs(lr[x] - rr[x + s]) for x in range(lo, hi))
        d = tot / ((hi - lo) * hh)
        if best is None or d < best:
            best = d
    return best / dev


if __name__ == "__main__":
    sys.exit(main(sys.argv))
