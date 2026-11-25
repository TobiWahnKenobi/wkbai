"""wkbai's own top bar buttons: Statistics and Timeline, in the style of the original's five.

    python -I tools/ui/top_bar_buttons.py [--preview PANEL.tga OUT.png]

Writes redata/Interface/InGame/Top Panel/{Statistics,Timeline} Button{, Highlighted}.tga
(56 x 14, like the original's buttons). Their rail on the Celtic band left of Settings is the
original panel's own strip, drawn again by ClientUIScript.lua. Everything here is drawn from scratch: a noisy navy
tile lit from the top right, a gold icon with a dark outline (a bar chart, an hourglass), and
the lit tile for the mouse over it and for its window being open (the original's buttons have no
pressed look). No pixel of the original's art is used.

--preview draws that rail and the buttons onto a copy of the original's Standard Panel.tga (read
from the game, never written) to check the look; the result is for the developer's eyes only.

Needs Pillow.
"""
import argparse, os, random
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "redata", "Interface", "InGame", "Top Panel")
W, H = 56, 14
SS = 8  # supersampling for the icons

# where the left button sits on the panel (the Statistics button; Timeline is 60 further)
SLOT_X, SLOT_Y = 98, 20


def tile(seed, bright):
    """The navy background: dark at the bottom left, lighter to the top right, with grain."""
    rnd = random.Random(seed)
    im = Image.new("RGBA", (W, H))
    px = im.load()
    for y in range(H):
        for x in range(W):
            t = 0.55 * (x / (W - 1)) + 0.45 * (1 - y / (H - 1))  # 0 bottom left .. 1 top right
            base = 0.12 + 0.55 * t * t + 0.12 * bright
            n = rnd.uniform(-0.07, 0.07)
            v = max(0.0, min(1.0, base + n))
            r = g = int(10 + 95 * v * v + 35 * bright * v)
            b = int(40 + 140 * v + 30 * bright)
            px[x, y] = (min(r, 255), min(g, 255), min(b, 255), 255)
    # lit edge at the right, dark at the left, as the original's
    for y in range(H):
        r, g, b, a = px[W - 1, y]
        px[W - 1, y] = (min(r + 40, 255), min(g + 40, 255), min(b + 45, 255), 255)
        r, g, b, a = px[0, y]
        px[0, y] = (r // 2, g // 2, b // 2, 255)
    return im


def gold(t):
    """Gold from shadow (0) to highlight (1), as the horn and the coins."""
    t = max(0.0, min(1.0, t))
    return (int(110 + 145 * t), int(70 + 150 * t), int(10 + 70 * t * t), 255)


def icon_stats():
    """Four bars rising to the right on a baseline."""
    big = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    bars = [(12, 0.35), (19, 0.55), (26, 0.45), (33, 0.72), (40, 0.98)]
    base = 12.6
    for x, h in bars:
        top = base - h * 11.4
        x0, x1 = x * SS, (x + 5.0) * SS
        y0, y1 = top * SS, base * SS
        d.rectangle([x0 - SS * 0.8, y0 - SS * 0.8, x1 + SS * 0.8, y1 + SS * 0.4], fill=(20, 10, 0, 255))
        # left lit, right in shadow
        steps = 12
        for i in range(steps):
            xa = x0 + (x1 - x0) * i / steps
            xb = x0 + (x1 - x0) * (i + 1) / steps
            d.rectangle([xa, y0, xb, y1], fill=gold(1.0 - 0.75 * i / steps))
        d.rectangle([x0, y0, x1, y0 + SS * 0.9], fill=gold(1.0))
    # baseline, a thin dark bar under them
    d.rectangle([10 * SS, base * SS, 46.5 * SS, (base + 1.0) * SS], fill=(20, 10, 0, 255))
    return big


def icon_timeline():
    """An hourglass (gold caps and posts, pale glass, sand running to the lower bulb) at the
    left of a gold line with three marks: time passing, and what happened in it."""
    big = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    cx = 17.0
    top, bot = 0.6, 13.4
    mid = (top + bot) / 2
    hw = 5.0  # bulb half width
    # the line with its marks, behind the hourglass's right cap
    d.rectangle([(cx + 6) * SS, (mid - 1.1) * SS, 47.5 * SS, (mid + 1.1) * SS], fill=(20, 10, 0, 255))
    d.rectangle([(cx + 6) * SS, (mid - 0.45) * SS, 47 * SS, (mid + 0.45) * SS], fill=gold(0.8))
    for i, mx in enumerate((29.0, 36.5, 44.0)):
        r = 2.0 if i == 2 else 1.6
        d.ellipse([(mx - r - 0.7) * SS, (mid - r - 0.7) * SS, (mx + r + 0.7) * SS, (mid + r + 0.7) * SS], fill=(20, 10, 0, 255))
        d.ellipse([(mx - r) * SS, (mid - r) * SS, (mx + r) * SS, (mid + r) * SS], fill=gold(1.0) if i == 2 else gold(0.65))

    def P(x, y):
        return (x * SS, y * SS)

    # glass: two triangles meeting at the neck, outlined dark
    upper = [P(cx - hw, top + 1.2), P(cx + hw, top + 1.2), P(cx + 0.7, mid), P(cx - 0.7, mid)]
    lower = [P(cx - 0.7, mid), P(cx + 0.7, mid), P(cx + hw, bot - 1.2), P(cx - hw, bot - 1.2)]
    for poly in (upper, lower):
        d.polygon(poly, fill=(200, 215, 235, 255), outline=(20, 10, 0, 255), width=SS)
    # sand: what is left on top, the heap below and the stream between
    d.polygon([P(cx - 2.6, mid - 2.4), P(cx + 2.6, mid - 2.4), P(cx + 0.5, mid), P(cx - 0.5, mid)], fill=(225, 175, 80, 255))
    d.polygon([P(cx - 4.2, bot - 1.6), P(cx + 4.2, bot - 1.6), P(cx + 1.6, mid + 2.6), P(cx - 1.6, mid + 2.6)], fill=(225, 175, 80, 255))
    d.line([P(cx, mid), P(cx, bot - 2.0)], fill=(225, 175, 80, 255), width=int(SS * 0.6))
    # posts at both sides
    for sx in (-1, 1):
        x = cx + sx * (hw + 0.9)
        d.rectangle([P(x - 0.9, top + 0.6), P(x + 0.9, bot - 0.6)], fill=(20, 10, 0, 255))
        d.rectangle([P(x - 0.45, top + 0.9), P(x + 0.45, bot - 0.9)], fill=gold(0.75 if sx < 0 else 0.4))
    # caps
    for y0, y1 in ((top - 0.2, top + 1.6), (bot - 1.6, bot + 0.2)):
        d.rectangle([P(cx - hw - 2.4, y0), P(cx + hw + 2.4, y1)], fill=(20, 10, 0, 255))
        steps = 10
        for i in range(steps):
            xa = cx - hw - 1.8 + (2 * hw + 3.6) * i / steps
            xb = cx - hw - 1.8 + (2 * hw + 3.6) * (i + 1) / steps
            d.rectangle([P(xa, y0 + 0.45), P(xb, y1 - 0.45)], fill=gold(0.95 - 0.6 * i / steps))
    return big


def compose(bg, big, dx=0, dy=0, dim=1.0):
    icon = big.resize((W, H), Image.LANCZOS)
    if dim != 1.0:
        r, g, b, a = icon.split()
        r, g, b = (c.point(lambda v: int(v * dim)) for c in (r, g, b))
        icon = Image.merge("RGBA", (r, g, b, a))
    # a soft dark shadow down-left, as the original's icons have
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sh.putalpha(icon.getchannel("A").point(lambda v: int(v * 0.55)))
    sh = sh.filter(ImageFilter.GaussianBlur(0.6))
    out = bg.copy()
    out.alpha_composite(sh, (dx - 1, dy + 1) if dx >= 1 else (0, min(dy + 1, 1)))
    out.alpha_composite(icon, (dx, dy))
    return out


def build():
    os.makedirs(OUT, exist_ok=True)
    files = {}
    for name, icon, seed in (("Statistics", icon_stats(), 3), ("Timeline", icon_timeline(), 5)):
        files[name + " Button"] = compose(tile(seed, 0.0), icon)
        files[name + " Button Highlighted"] = compose(tile(seed, 1.0), icon)
    for n, im in files.items():
        im.save(os.path.join(OUT, n + ".tga"), compression=None)
    return files


def preview(panel_path, out_path, files):
    p = Image.open(panel_path).convert("RGBA")
    p.alpha_composite(p.crop((196, 16, 338, 42)), (196 - 120, 16))  # as ClientUIScript.lua
    p.alpha_composite(files["Statistics Button"], (SLOT_X, SLOT_Y))
    p.alpha_composite(files["Timeline Button Highlighted"], (SLOT_X + 60, SLOT_Y))
    p = p.crop((0, 0, 560, 60))
    p.resize((p.width * 4, p.height * 4), Image.NEAREST).save(out_path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", nargs=2, metavar=("PANEL", "OUT"))
    a = ap.parse_args()
    f = build()
    if a.preview:
        preview(a.preview[0], a.preview[1], f)
