#!/usr/bin/env python3
"""Vectorise the SpectrumWorx skin's panels, module strips, combos and knob rings.

Same method as run_pills.py: every number here is measured off the PNG rather
than eyeballed -- rounded-rect geometry by supersampled coordinate descent,
soft edges by a least-squares solve -- and each result is rendered back with
rsvg-convert and compared against the original, in premultiplied colour plus
alpha so that transparent pixels cannot flatter the score.

The constructions found:

  55/56  ModuleBg / ModuleBgSelected -- a tall rounded rect, flat #231f20 body
         inside a 1px #13b5ea rim; "selected" is the same shape plus a white
         outer glow.  No filters: the glow is stacked rounded rects, as in 08.
  59/60  ModuleCombo / ModuleComboOn and 61/62 SettingsCombo / SettingsComboOn
         -- one construction at two sizes: flat #1a1a1a body, a 1px rim that is
         #13b5ea when off and white when on, and a faint #f0fcff glow that is
         pixel-identical in both states.
  58     ModuleKnobSelected -- a soft white ring, emitted as one disc carrying
         a radial gradient whose stops are all the same white and differ only
         in stop-opacity.  Constant colour matters: JUCE's ColourGradient
         tweens premultiplied where SVG says straight, and the two agree
         exactly only while the colour does not move.
  07/17  PresetBackground / SettingsEngineBg -- a flat #1a171b panel carrying
         1px white rounded-rect frames.  07 is rounded on all four corners and
         its top field is two rects sharing three sides; 17 is square along the
         top, where the settings tabs meet it, and carries their feet in its
         first row.

20 (SettingsAboutBg) is deliberately not here: it is fifteen lines of baked
text in a humanist sans that is not Vera, plus a drawn logo, and the rule is
that any text is Vera outlines.  Re-setting it would change every glyph on the
page and still come out larger than the PNG.

Run:  python3 tools/skin-vectorise/run_panels.py
It is idempotent -- re-running reproduces the committed files byte for byte.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from svgify import SKIN, TMP, SS, fmt, join, load, down, descend, hexc  # noqa: E402


def _i(rgb):
    return tuple(int(round(float(v) * 255)) for v in rgb)


# --------------------------------------------------------------- sampling --
class Win:
    """A supersampled sample grid over part of the canvas.

    Rows may be given as several disjoint ranges, so a tall frame can be fitted
    from its two ends without carrying the empty middle."""

    def __init__(self, shape, xlo=0, xhi=None, rows=None):
        H, W = shape
        self.H, self.W = H, W
        self.xlo, self.xhi = xlo, W if xhi is None else xhi
        self.rows = rows or [(0, H)]
        ys = np.concatenate([np.arange(lo * SS, hi * SS) for lo, hi in self.rows])
        xs = np.arange(self.xlo * SS, self.xhi * SS)
        self.px = (xs[None, :] + .5) / SS
        self.py = (ys[:, None] + .5) / SS
        self.ss = (len(ys), len(xs))

    def crop(self, img):
        return np.concatenate([img[lo:hi, self.xlo:self.xhi] for lo, hi in self.rows])


def rr_sdf(win, g):
    """Signed distance to a rounded rect, (x0, x1, y0, y1, r) or with the four
    corner radii spelled out (top-left, top-right, bottom-right, bottom-left)."""
    x0, x1, y0, y1 = g[:4]
    rr = list(g[4:])
    rtl, rtr, rbr, rbl = rr * 4 if len(rr) == 1 else rr
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    r = np.where(win.py >= cy,
                 np.where(win.px >= cx, rbr, rbl),
                 np.where(win.px >= cx, rtr, rtl))
    dx = np.abs(win.px - cx) - ((x1 - x0) / 2 - r)
    dy = np.abs(win.py - cy) - ((y1 - y0) / 2 - r)
    return (np.hypot(np.maximum(dx, 0), np.maximum(dy, 0))
            + np.minimum(np.maximum(dx, dy), 0) - r)


def radius(win, cx, cy):
    return np.hypot(win.px - cx, win.py - cy) + np.zeros(win.ss)


def blank(win):
    return np.zeros(win.ss + (3,)), np.zeros(win.ss)


def over(st, cov, rgb):
    """Source-over of a flat colour at `cov` coverage, at supersample scale."""
    pm, al = st
    c = np.asarray(cov, dtype=float)
    return (np.asarray(rgb, dtype=float)[None, None, :] * c[:, :, None] + pm * (1 - c)[:, :, None],
            c + al * (1 - c))


def flatten(win, layers):
    st = blank(win)
    for cov, rgb in layers:
        st = over(st, cov, rgb)
    return down(st[0]), down(st[1])


def err_of(win, png, pm, al):
    t = win.crop(png)
    return float((np.abs(pm - t[:, :, :3] * t[:, :, 3:4]).sum(axis=2)
                  + np.abs(al - t[:, :, 3])).mean())


# ------------------------------------------------------------------ paths --
def rr_path(g, inset=0.0):
    """A rounded rect, optionally eroded by `inset`; a negative inset grows it."""
    x0, x1, y0, y1 = g[0] + inset, g[1] - inset, g[2] + inset, g[3] - inset
    rr = list(g[4:])
    rtl, rtr, rbr, rbl = [max(float(v) - inset, 0.0) for v in (rr * 4 if len(rr) == 1 else rr)]
    t = ["M", fmt(x0 + rtl), fmt(y0)]

    def arc(r, x, y):
        # a square corner needs nothing: the H/V before it already ends there
        if r > 0:
            t.extend(["A", fmt(r), fmt(r), "0", "0", "1", fmt(x), fmt(y)])

    t.extend(["H", fmt(x1 - rtr)]); arc(rtr, x1, y0 + rtr)
    t.extend(["V", fmt(y1 - rbr)]); arc(rbr, x1 - rbr, y1)
    t.extend(["H", fmt(x0 + rbl)]); arc(rbl, x0, y1 - rbl)
    t.extend(["V", fmt(y0 + rtl)]); arc(rtl, x0 + rtl, y0)
    t.append("Z")
    return join(t)


def cap_path(g, w):
    """Just the right-hand end of a rounded-rect ring.

    07's preset field is two rounded rects sharing three sides, and painting
    both rings would paint those three sides twice -- two 50%-covered edges
    composite to 75%, not 50%, which is a visibly heavier line.  So the shorter
    one contributes only the piece that is its own."""
    x1, y0, y1, r = g[1], g[2], g[3], g[4]
    f, xa = fmt, g[1] - g[4]
    return join(["M", f(xa), f(y0),
                 "A", f(r), f(r), "0", "0", "1", f(x1), f(y0 + r),
                 "V", f(y1 - r),
                 "A", f(r), f(r), "0", "0", "1", f(xa), f(y1),
                 "V", f(y1 - w),
                 "A", f(r - w), f(r - w), "0", "0", "0", f(x1 - w), f(y1 - r),
                 "V", f(y0 + r),
                 "A", f(r - w), f(r - w), "0", "0", "0", f(xa), f(y0 + w), "Z"])


def circ_path(cx, cy, r):
    f = fmt
    return join(["M", f(cx - r), f(cy), "A", f(r), f(r), "0", "0", "0", f(cx + r), f(cy),
                 "A", f(r), f(r), "0", "0", "0", f(cx - r), f(cy), "Z"])


def svg(W, H, body):
    return "\n".join(
        ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
         % (W, H, W, H)] + body + ["</svg>"]) + "\n"


def write(n, text):
    with open(os.path.join(SKIN, "%02d.svg" % n), "w") as fh:
        fh.write(text)


def compare(n):
    """Render the SVG at the PNG's size; mean and worst per-channel delta."""
    png = load(n)
    H, W = png.shape[:2]
    out = os.path.join(TMP, "cmp_%02d.png" % n)
    subprocess.run(["rsvg-convert", "-w", str(W), "-h", str(H),
                    os.path.join(SKIN, "%02d.svg" % n), "-o", out], check=True)
    got = np.asarray(Image.open(out).convert("RGBA"), dtype=np.float64) / 255.0
    e = np.concatenate([np.abs(got[:, :, :3] * got[:, :, 3:4] - png[:, :, :3] * png[:, :, 3:4]),
                        np.abs(got[:, :, 3:4] - png[:, :, 3:4])], axis=2)
    return e.mean() * 255, e.max() * 255


def pick(n, cands):
    """Write each candidate, render it, keep the one closest to the PNG.

    The soft profiles below are deconvolutions -- their basis is already
    area-averaged over the pixel -- so how hard to smooth them is not something
    the residual can answer on its own: it sits well above the PNG's
    quantisation step whatever we do, because the artwork is only circular to
    about a sixth of a pixel.  So the smoothing is chosen the same way
    everything else here is: by rendering it back and measuring."""
    best = None
    for label, text in cands:
        write(n, text)
        d = compare(n)[0]
        if best is None or d < best[0] - 1e-9:
            best = (d, label, text)
    write(n, best[2])
    return best[1], best[0]


# ------------------------------------------------------------ soft edges ---
def reg_solve(A, b, lam):
    """Least squares with a second-difference penalty `lam` on the profile.

    Without it the solve sharpens the profile into a spike and lets
    neighbouring bands cancel it out, which renders as a hard bright ring where
    the artwork has a soft one."""
    n = A.shape[1]
    D = np.zeros((max(n - 2, 0), n))
    for i in range(n - 2):
        D[i, i:i + 3] = (1, -2, 1)
    AtA = A.T @ A
    pen = np.trace(AtA) / n * (D.T @ D) + 1e-9 * np.eye(n)
    return np.linalg.solve(AtA + lam * pen, A.T @ b)


def band_solve(win, png, d, edges, under, lam):
    """Opacity and colour of the bands lo <= d < hi, by least squares.

    The bands do not overlap and lie clear of everything already painted, so
    both alpha and premultiplied colour are linear in them."""
    t = win.crop(png)
    cols = [down(((d >= lo) & (d < hi)).astype(float)).ravel() for lo, hi in edges]
    A = np.stack(cols, axis=1)
    keep = A.sum(axis=1) > 1e-9
    o = reg_solve(A[keep], (t[:, :, 3] - under[1]).ravel()[keep], lam)
    c = reg_solve(A[keep], (t[:, :, :3] * t[:, :, 3:4] - under[0]).reshape(-1, 3)[keep], lam)
    out = []
    for i, e in enumerate(edges):
        op = float(np.clip(o[i], 0, 1))
        out.append((e, op, tuple(np.clip(c[i] / op, 0, 1)) if op > 1e-4 else (1., 1., 1.)))
    return out


def stack_bands(bands):
    """Band alphas -> the opacities of stacked filled shapes painted largest
    first, which is one path each instead of an even-odd pair."""
    out, below = [], 0.0
    for (lo, hi), op, _ in reversed(bands):
        out.append((hi, 0.0 if op <= below + 1e-4 else (op - below) / max(1 - below, 1e-6)))
        below = op
    return out


GLOW_TRIES = [(w, lam) for w in (1.0, .5) for lam in (0.0, .01, .1, 1.0)]


def glow_of(win, png, d, under, step=1.0, lam=0.0):
    """The glow's falloff, and the colour it is actually drawn in.

    The colour is read off the pixels that are clear of the shape's own edge
    rather than taken from the solve: dividing a fitted premultiplied colour by
    an opacity of 0.02 amplifies the noise in it, and it renders no better."""
    edges = [(t, t + step) for t in np.arange(0, 5.0, step)]
    bands = band_solve(win, png, d, edges, under, lam)
    t = win.crop(png)
    clear = (down(d) > .5) & (t[:, :, 3] > .01)
    rgb = tuple(np.median(t[:, :, :3][clear], axis=0)) if clear.sum() else (1., 1., 1.)
    return [(e, o) for e, o in stack_bands(bands) if o > .002], rgb


def solve_rim(win, png, layers, shape, body_cov, body):
    """The rim's colour, given everything around it: the body is painted over
    the rim, so what is left of it is a known coverage map and the residual
    premultiplied colour is linear in the one unknown."""
    base = flatten(win, layers + [(shape, (0., 0., 0.)), (body_cov, body)])
    cov = down(shape * (1 - body_cov))
    t = win.crop(png)
    res = t[:, :, :3] * t[:, :, 3:4] - base[0]
    s = (cov ** 2).sum()
    return tuple(np.clip((cov[:, :, None] * res).sum(axis=(0, 1)) / s, 0, 1))


# =========================================================== 55 / 56 ========
def do_strip():
    plain, lit = load(55), load(56)
    H, W = plain.shape[:2]
    full = Win((H, W))
    a = plain[:, :, 3]

    ys, xs = np.nonzero(a > .5)
    g0 = [xs.min() + .5, xs.max() + .5, ys.min() + .5, ys.max() + .5, 6.0]

    def geo(p):
        if p[4] < .5 or p[4] > min(p[1] - p[0], p[3] - p[2]) / 2:
            return 1e9
        return float(np.abs(down((rr_sdf(full, p) <= 0).astype(float)) - a).mean())

    g, gerr = descend(g0, geo, [.4] * 5)
    d = rr_sdf(full, g)
    shape = (d <= 0).astype(float)

    body = tuple(plain[H // 2, W // 2, :3])
    rim = tuple(plain[int(g[2]), W // 2, :3])   # the top row is the rim, undiluted

    def rimw(p):
        return err_of(full, plain, *flatten(full, [(shape, rim),
                                                   ((d <= -p[0]).astype(float), body)]))

    (rw,), rerr = descend([1.0], rimw, [.25])
    rim = solve_rim(full, plain, [], shape, (d <= -rw).astype(float), body)

    core = ['<path d="%s" fill="%s"/>' % (rr_path(g), hexc(_i(rim))),
            '<path d="%s" fill="%s"/>' % (rr_path(g, rw), hexc(_i(body)))]
    write(55, svg(W, H, core))

    gw = Win((H, W), rows=[(0, 30), (H - 30, H)])
    dg = rr_sdf(gw, g)
    under = flatten(gw, [((dg <= 0).astype(float), rim), ((dg <= -rw).astype(float), body)])
    cands = []
    for step, lam in GLOW_TRIES:
        glow, gcol = glow_of(gw, lit, dg, under, step, lam)
        cands.append(("%.1f/%g" % (step, lam), svg(W, H, [
            '<path d="%s" fill="%s" fill-opacity="%s"/>'
            % (rr_path(g, -e), hexc(_i(gcol)), fmt(o, 3)) for e, o in glow] + core)))
    label, delta = pick(56, cands)
    return "geom %s r=%.2f rim %.3f (%.5f/%.5f) glow %s -> %.2f" % (
        ["%.3f" % v for v in g[:4]], g[4], rw, gerr, rerr, label, delta)


# ================================================== 59/60 and 61/62 ========
def do_combo(off, on):
    a_off, a_on = load(off), load(on)
    H, W = a_off.shape[:2]
    full = Win((H, W))
    body = tuple(a_off[H // 2, W // 2, :3])

    ys, xs = np.nonzero(a_off[:, :, 3] > .5)
    p = [xs.min() + .5, xs.max() + .5, ys.min() + .5, ys.max() + .5, 5.0, 1.0]
    glow, gcol = [], (1., 1., 1.)
    rim_off = tuple(a_off[int(p[2]), W // 2, :3])

    # geometry and rim against the glow, alternately, until neither moves: the
    # glow biases the edge fit, and the edge fit is what sites the glow
    for _ in range(3):
        def cost(q, _g=tuple(glow), _c=gcol, _r=rim_off):
            gg, rw = list(q[:5]), q[5]
            if gg[4] < .5 or rw < .2 or rw > 3:
                return 1e9
            if gg[4] > min(gg[1] - gg[0], gg[3] - gg[2]) / 2:
                return 1e9
            dd = rr_sdf(full, gg)
            L = [((dd <= e).astype(float) * o, _c) for e, o in _g]
            L.append(((dd <= 0).astype(float), _r))
            L.append(((dd <= -rw).astype(float), body))
            return err_of(full, a_off, *flatten(full, L))

        p, err = descend(list(p), cost, [.3] * 5 + [.2])
        g, rw = list(p[:5]), p[5]
        d = rr_sdf(full, g)
        shape, inner = (d <= 0).astype(float), (d <= -rw).astype(float)
        halo = [((d <= e).astype(float) * o, gcol) for e, o in glow]
        rim_off = solve_rim(full, a_off, halo, shape, inner, body)
        glow, gcol = glow_of(full, a_off, d,
                             flatten(full, [(shape, rim_off), (inner, body)]))

    halo = [((d <= e).astype(float) * o, gcol) for e, o in glow]
    rim = {off: rim_off, on: solve_rim(full, a_on, halo, shape, inner, body)}
    under = flatten(full, [(shape, rim_off), (inner, body)])

    def dress(rgb, halo_paths):
        return svg(W, H, halo_paths
                   + ['<path d="%s" fill="%s"/>' % (rr_path(g), hexc(_i(rgb))),
                      '<path d="%s" fill="%s"/>' % (rr_path(g, rw), hexc(_i(body)))])

    cands, made = [], {}
    for step, lam in GLOW_TRIES:
        gl, gc = glow_of(full, a_off, d, under, step, lam)
        paths = ['<path d="%s" fill="%s" fill-opacity="%s"/>'
                 % (rr_path(g, -e), hexc(_i(gc)), fmt(o, 3)) for e, o in gl]
        key = "%.1f/%g" % (step, lam)
        made[key] = paths
        cands.append((key, dress(rim[off], paths)))
    label, delta = pick(off, cands)
    write(on, dress(rim[on], made[label]))
    return "geom %s r=%.2f rim %.3f %s/%s glow %s -> %.2f (%.5f)" % (
        ["%.3f" % v for v in g[:4]], g[4], rw, hexc(_i(rim[off])), hexc(_i(rim[on])),
        label, delta, err)


# =============================================================== 58 / 65 ====
def hats(r, knots):
    """Piecewise-linear interpolation basis over `knots`, clamped at both ends
    -- exactly how a gradient reads its stops."""
    out = []
    for i, k in enumerate(knots):
        f = np.zeros(r.shape)
        if i:
            lo = knots[i - 1]
            m = (r >= lo) & (r < k)
            f[m] = (r[m] - lo) / (k - lo)
        else:
            f[r < k] = 1.0
        if i + 1 < len(knots):
            hi = knots[i + 1]
            m = (r >= k) & (r < hi)
            f[m] = (hi - r[m]) / (hi - k)
        else:
            f[r >= k] = 1.0
        f[r == k] = 1.0
        out.append(f)
    return out


RING_TRIES = [(s, lam) for s in (.5, .75, 1.0) for lam in (0.0, .003, .01, .03, .1, .3, 1.0)]


def ring_centre(a, cx, cy):
    """The centre a ring is most nearly concentric about: the one that leaves
    the least alpha left over once each thin radial bin is given its mean.
    Cheap enough to search, unlike refitting the whole profile per candidate."""
    H, W = a.shape
    ys, xs = np.mgrid[0:H, 0:W]

    def cost(p):
        r = np.hypot(xs + .5 - p[0], ys + .5 - p[1])
        b = np.minimum((r / .25).astype(int), int(max(H, W) / .25))
        s = np.bincount(b.ravel(), a.ravel())
        c = np.maximum(np.bincount(b.ravel()), 1)
        return float(((a - (s / c)[b]) ** 2).sum())

    return descend([cx, cy], cost, [.25, .25])[0]


def do_ring(n):
    png = load(n)
    H, W = png.shape[:2]
    full = Win((H, W))
    a = png[:, :, 3]
    ys, xs = np.mgrid[0:H, 0:W]
    mid = (float((a * (xs + .5)).sum() / a.sum()), float((a * (ys + .5)).sum() / a.sum()))
    centres = {"mass": mid, "conc": tuple(ring_centre(a, *mid))}

    def build(centre, step, lam):
        cx, cy = centres[centre]
        rp = np.hypot(xs + .5 - cx, ys + .5 - cy)
        r = radius(full, cx, cy)
        lo = max(0.0, np.floor((rp[a > .004].min() - 1) / step) * step)
        hi = np.ceil((rp[a > .004].max() + 1) / step) * step
        knots = [0.0] + list(np.arange(lo, hi + step / 2, step))
        B = np.stack([down(f).ravel() for f in hats(r, knots)], axis=1)
        v = np.clip(reg_solve(B, a.ravel(), lam), 0, 1)
        while len(v) > 2 and v[-1] < 1e-3 and v[-2] < 1e-3:      # trim the dead tail
            knots, v = knots[:-1], v[:-1]
        al = (B[:, :len(v)] @ v).reshape(H, W)
        m = al > .05
        grey = float(np.clip((png[:, :, :3][m].mean(axis=1) * al[m]).sum()
                             / (al[m] ** 2).sum(), 0, 1))
        col = hexc(_i((grey,) * 3))
        # a stop sitting between two others that are also zero says nothing
        live = [i for i in range(len(v))
                if max(v[max(i - 1, 0):i + 2]) > 5e-4 or i in (0, len(v) - 1)]
        stops = "".join('<stop offset="%s" stop-color="%s" stop-opacity="%s"/>'
                        % (fmt(knots[i] / knots[-1], 4), col, fmt(v[i], 3)) for i in live)
        return len(live), v.max(), col, svg(W, H, [
            '<defs><radialGradient id="g" gradientUnits="userSpaceOnUse" cx="%s" cy="%s" r="%s">'
            % (fmt(cx), fmt(cy), fmt(knots[-1])) + stops + "</radialGradient></defs>",
            '<path d="%s" fill="url(#g)"/>' % circ_path(cx, cy, knots[-1])])

    made, cands = {}, []
    for centre in centres:
        for step, lam in RING_TRIES:
            key = "%s %.2f/%g" % (centre, step, lam)
            made[key] = build(centre, step, lam)
            cands.append((key, made[key][3]))
    label, delta = pick(n, cands)
    stops, peak, col, _ = made[label]
    return "centre %.2f,%.2f  %d stops  peak %.3f  grey %s  %s -> %.2f" % (
        centres[label.split()[0]] + (stops, peak, col, label, delta))


# =============================================================== 07 / 17 ====
# frames: the rows to fit over, and how many rounded rects share them.  07's top
# field is two: the field itself and a shorter cap inside its right-hand end.
PANELS = {
    7: dict(square_top=False, top_strip=False,
            frames=[([(4, 26)], 2), ([(24, 58)], 1),
                    ([(72, 92), (300, 320)], 1), ([(319, 357)], 1)]),
    17: dict(square_top=True, top_strip=True,
             frames=[([(7, 27), (320, 342)], 1)]),
}


def top_strip(png, panel_rgb):
    """17's first row is the foot of the four settings tabs that sit on it --
    three dim and one lit.  Returned as [(x0, x1, rgb)]."""
    row = png[0, :, :3]
    off = np.abs(row - np.asarray(panel_rgb)).max(axis=1)
    on = off > .02
    runs, i = [], 0
    while i < len(on):
        if not on[i]:
            i += 1
            continue
        j = i
        while j < len(on) and on[j]:
            j += 1
        seg = row[i:j]
        rgb = tuple(np.median(seg[off[i:j] > off[i:j].max() * .9], axis=0))
        cov = np.clip(off[i:j] / np.abs(np.asarray(rgb) - np.asarray(panel_rgb)).max(), 0, 1)
        c, w = band_edge(cov, 0.0, 1.0)
        runs.append((i + c - w / 2, i + c + w / 2, rgb))
        i = j
    return runs


#   Supersampling at 8 cannot see an edge move by less than a sixteenth of a
# pixel, and rsvg and JUCE can: the descent below settles anywhere on that
# plateau, which on a 1px line is a visible 11/255.  So the straight runs of
# every edge are read straight off the PNG instead -- a band's width is the
# total coverage across it and its centre is that coverage's first moment,
# both exact for a straight edge -- and only the corner radius is left to fit.
def band_edge(vals, bg, ink):
    """Centre and width of a stroke lying across `vals`."""
    c = np.clip((np.asarray(vals) - bg) / (ink - bg), 0, 1)
    w = float(c.sum())
    return float((c * (np.arange(len(c)) + .5)).sum() / w), w


def step_edge(vals, rising):
    """Where an opaque edge crosses `vals` (coverage running 0 -> 1 or 1 -> 0)."""
    s = float(np.asarray(vals).sum())
    return len(vals) - s if rising else s


def panel_geom(png, square_top):
    H, W = png.shape[:2]
    a = png[:, :, 3]
    win = Win((H, W), rows=[(0, 24), (H - 24, H)])
    ac = win.crop(a)
    # the four sides off their straight runs; only the radius is left to fit
    x0 = step_edge(a[H // 2, :6], True)
    x1 = W - 6 + step_edge(a[H // 2, W - 6:], False)
    y0 = 0.0 if square_top else step_edge(a[:6, W // 2], True)
    y1 = H - 6 + step_edge(a[H - 6:, W // 2], False)

    def shape(p):
        return [x0, x1, y0, y1] + ([0.0, 0.0, p[0], p[0]] if square_top else [p[0]])

    def cost(p):
        if p[0] < .5 or p[0] > min(x1 - x0, y1 - y0) / 2:
            return 1e9
        return float(np.abs(down((rr_sdf(win, shape(p)) <= 0).astype(float)) - ac).mean())

    p, err = descend([7.0], cost, [.4])
    return shape(p), err


def frame_geom(png, panel, panel_rgb, rows, count):
    H, W = png.shape[:2]
    win = Win((H, W), rows=rows)
    t = win.crop(png)
    ink = t[:, :, 0] * t[:, :, 3]
    ys, xs = np.nonzero(ink > panel_rgb[0] + .05)
    yy = np.concatenate([np.arange(lo, hi) for lo, hi in rows])
    lit = png[:, :, 0] * png[:, :, 3]
    bg = panel_rgb[0]

    # the four sides off their straight runs, and the cap off its own
    ex0, ex1 = xs.min(), xs.max()
    ey0, ey1 = yy[ys.min()], yy[ys.max()]
    mid_y, mid_x = (ey0 + ey1) // 2, (ex0 + ex1) // 2
    cl, wl = band_edge(lit[mid_y, ex0 - 1:ex0 + 4], bg, 1.0)
    cr, wr = band_edge(lit[mid_y, ex1 - 3:ex1 + 2], bg, 1.0)
    ct, wt = band_edge(lit[ey0 - 1:ey0 + 4, mid_x], bg, 1.0)
    cb, wb = band_edge(lit[ey1 - 3:ey1 + 2, mid_x], bg, 1.0)
    w = (wl + wr + wt + wb) / 4
    x0, x1 = ex0 - 1 + cl - w / 2, ex1 - 3 + cr + w / 2
    y0, y1 = ey0 - 1 + ct - w / 2, ey1 - 3 + cb + w / 2
    caps = []
    if count == 2:
        # the shorter rect's own end: the tallest column of ink short of x1
        col = (lit[ey0:ey1, :int(x1 - 3 * w)] > bg + .05).sum(axis=0)
        k = int(np.argmax(col))
        cc, _ = band_edge(lit[mid_y, k - 2:k + 3], bg, 1.0)
        caps = [k - 2 + cc + w / 2]

    dp = rr_sdf(win, panel)

    def cost(p):
        r = p[0]
        if r < .5 or r > min(x1 - x0, y1 - y0) / 2:
            return 1e9
        d = rr_sdf(win, [x0, x1, y0, y1, r])
        c = ((d <= 0) & (d >= -w)).astype(float)
        for xc in caps:
            d2 = rr_sdf(win, [x0, xc, y0, y1, r])
            c = np.maximum(c, ((d2 <= 0) & (d2 >= -w)).astype(float))
        return err_of(win, png, *flatten(win, [((dp <= 0).astype(float), panel_rgb),
                                               (c, (1., 1., 1.))]))

    p, err = descend([7.0], cost, [.4])
    return [x0, x1, y0, y1, p[0], w] + caps, err


def do_panel(n):
    png = load(n)
    H, W = png.shape[:2]
    spec = PANELS[n]
    panel_rgb = tuple(png[H // 2, 4, :3])
    g, gerr = panel_geom(png, spec["square_top"])

    L = ['<path d="%s" fill="%s"/>' % (rr_path(g), hexc(_i(panel_rgb)))]
    notes = []
    if spec["top_strip"]:
        for x0, x1, rgb in top_strip(png, panel_rgb):
            L.append('<path d="M%s 0H%sV1H%sZ" fill="%s"/>'
                     % (fmt(x0), fmt(x1), fmt(x0), hexc(_i(rgb))))
    for rows, count in spec["frames"]:
        p, err = frame_geom(png, g, panel_rgb, rows, count)
        rc = [p[0], p[1], p[2], p[3], p[4]]
        L.append('<path d="%s%s" fill="#fff" fill-rule="evenodd"/>'
                 % (rr_path(rc), rr_path(rc, p[5])))
        if count == 2:
            L.append('<path d="%s" fill="#fff"/>'
                     % cap_path([p[0], p[6], p[2], p[3], p[4]], p[5]))
        notes.append("y%.1f..%.1f w%.2f r%.2f (%.4f)" % (p[2], p[3], p[5], p[4], err))
    write(n, svg(W, H, L))
    return "panel %s r=%s (%.5f); frames " % (
        ["%.2f" % v for v in g[:4]], ["%.2f" % v for v in g[4:]], gerr) + "; ".join(notes)


# ==================================================================== main ==
def main():
    todo = [("55/56 module strip", do_strip),
            ("59/60 module combo", lambda: do_combo(59, 60)),
            ("61/62 settings combo", lambda: do_combo(61, 62)),
            ("58 knob ring", lambda: do_ring(58)),
            ("07 preset panel", lambda: do_panel(7)),
            ("17 settings panel", lambda: do_panel(17))]
    for name, fn in todo:
        print("%-22s %s" % (name, fn()))
        sys.stdout.flush()

    print()
    for n in (7, 17, 55, 56, 58, 59, 60, 61, 62):
        mean, mx = compare(n)
        png = os.path.join(SKIN, "%02d.png" % n)
        im = Image.open(png)
        print("%02d  %3dx%-3d  delta mean %5.2f/255 max %6.2f   svg %5d B   png %5d B"
              % (n, im.width, im.height, mean, mx,
                 os.path.getsize(os.path.join(SKIN, "%02d.svg" % n)), os.path.getsize(png)))


if __name__ == "__main__":
    main()
