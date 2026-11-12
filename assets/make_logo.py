"""Draws the KAYRA logo (an arc-reactor emblem with a K), the Windows icon and
the repository banner. Run: python assets/make_logo.py"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts")

CYAN = (0, 209, 255)
DEEP = (3, 14, 22)


def font(name, size, variation=None):
    f = ImageFont.truetype(os.path.join(FONTS, name), size)
    if variation:
        try:
            f.set_variation_by_name(variation)
        except Exception:
            pass
    return f


def ring(d, c, r, w, fill):
    d.ellipse([c - r, c - r, c + r, c + r], outline=fill, width=int(w))


def emblem(size, background=True):
    """Square RGBA emblem, drawn 4x larger and scaled down for smooth edges."""
    S = size * 4
    c = S / 2
    R = S / 2
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    if background:
        bg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        bd = ImageDraw.Draw(bg)
        bd.ellipse([c - R * 0.98, c - R * 0.98, c + R * 0.98, c + R * 0.98], fill=DEEP + (255,))
        img.alpha_composite(bg)

    # soft outer glow
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([c - R * 0.62, c - R * 0.62, c + R * 0.62, c + R * 0.62], fill=CYAN + (110,))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.06))
    img.alpha_composite(glow)

    d = ImageDraw.Draw(img)
    # outer ring with ticks
    ring(d, c, R * 0.86, R * 0.045, CYAN + (255,))
    for i in range(60):
        a = i / 60 * 2 * math.pi
        r0, r1 = R * 0.74, R * (0.79 if i % 5 else 0.81)
        d.line([c + math.cos(a) * r0, c + math.sin(a) * r0, c + math.cos(a) * r1, c + math.sin(a) * r1],
               fill=CYAN + (200 if i % 5 == 0 else 110,), width=max(1, int(R * (0.016 if i % 5 == 0 else 0.009))))

    # ten reactor segments
    n = 10
    for i in range(n):
        a0 = (i / n) * 2 * math.pi - math.pi / 2 + 0.07
        a1 = ((i + 1) / n) * 2 * math.pi - math.pi / 2 - 0.07
        ri, ro = R * 0.47, R * 0.66
        pts = []
        steps = 12
        for k in range(steps + 1):
            a = a0 + (a1 - a0) * k / steps
            pts.append((c + math.cos(a) * ro, c + math.sin(a) * ro))
        for k in range(steps, -1, -1):
            a = a0 + (a1 - a0) * k / steps
            pts.append((c + math.cos(a) * ri, c + math.sin(a) * ri))
        d.polygon(pts, fill=(70, 214, 255, 235), outline=(190, 245, 255, 255), width=max(1, int(R * 0.012)))

    ring(d, c, R * 0.42, R * 0.025, CYAN + (255,))

    # glowing core
    core = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    cd = ImageDraw.Draw(core)
    steps = 40
    for k in range(steps):
        t = k / (steps - 1)
        r = R * 0.36 * (1 - t)
        col = (int(0 + 235 * t), int(200 + 55 * t), 255, 255)
        cd.ellipse([c - r, c - r, c + r, c + r], fill=col)
    img.alpha_composite(core)

    # the K, cut in deep navy
    d = ImageDraw.Draw(img)
    kf = font("bahnschrift.ttf", int(R * 0.50), "Bold")
    l, t, r_, b = d.textbbox((0, 0), "K", font=kf)
    d.text((c - (l + r_) / 2, c - (t + b) / 2), "K", font=kf, fill=DEEP + (255,))

    return img.resize((size, size), Image.LANCZOS)


def banner(path, logo):
    W, H = 1280, 640
    img = Image.new("RGBA", (W, H), (2, 8, 14, 255))
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(2 + 4 * t), int(10 + 10 * (1 - t)), int(18 + 14 * (1 - t)), 255))
    # hex grid
    grid = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    s = 26
    for row in range(-1, H // int(s * 1.5) + 2):
        for col in range(-1, W // int(s * 1.732) + 2):
            cx = col * s * 1.732 + (row % 2) * s * 0.866
            cy = row * s * 1.5
            dist = math.hypot(cx - 330, cy - 320) / 900
            a = int(max(0, 60 * (1 - dist)))
            if a <= 0:
                continue
            pts = [(cx + s * math.cos(math.pi / 3 * k + math.pi / 6), cy + s * math.sin(math.pi / 3 * k + math.pi / 6))
                   for k in range(6)]
            gd.polygon(pts, outline=CYAN + (a,))
    img.alpha_composite(grid)

    lg = logo.resize((400, 400), Image.LANCZOS)
    img.alpha_composite(lg, (130, 120))

    d = ImageDraw.Draw(img)
    title = font("bahnschrift.ttf", 150, "Bold")
    x, y = 590, 150
    for ch in "KAYRA":
        d.text((x, y), ch, font=title, fill=(225, 246, 255, 255))
        x += d.textlength(ch, font=title) + 22
    d.rectangle([594, 330, 1190, 333], fill=CYAN + (180,))
    sub = font("bahnschrift.ttf", 34, "SemiBold")
    d.text((594, 352), "Kişisel yapay zekâ asistanı", font=sub, fill=CYAN + (255,))
    small = font("segoeui.ttf", 26)
    d.text((594, 404), "Personal AI assistant for Windows", font=small, fill=(160, 200, 220, 255))
    tags = font("bahnschrift.ttf", 22, "SemiBold")
    x = 594
    for t in ["C++17", "SES TANIMA", "TÜRKÇE", "OFFLINE", "AÇIK KAYNAK"]:
        w = d.textlength(t, font=tags)
        d.rounded_rectangle([x, 470, x + w + 28, 508], radius=5, outline=CYAN + (150,), width=2)
        d.text((x + 14, 476), t, font=tags, fill=(200, 236, 250, 255))
        x += w + 42
    img.convert("RGB").save(path)


def main():
    logo = emblem(1024)
    logo.save(os.path.join(HERE, "logo.png"))
    emblem(1024, background=False).save(os.path.join(HERE, "logo-transparent.png"))
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    icons = [emblem(s) if s >= 64 else emblem(s * 4).resize((s, s), Image.LANCZOS) for s in sizes]
    icons[-1].save(os.path.join(HERE, "kayra.ico"), sizes=[(s, s) for s in sizes], append_images=icons[:-1])
    banner(os.path.join(HERE, "banner.png"), logo)
    print("logo.png, logo-transparent.png, kayra.ico, banner.png")


if __name__ == "__main__":
    main()
