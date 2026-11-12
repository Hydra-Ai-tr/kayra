"""YouTube thumbnail (1280x720) and README screenshot from a demo recording.

Usage: python video/make_thumb.py tr|en [recdir]
"""
import os
import sys

import imageio_ffmpeg
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

LANG = sys.argv[1] if len(sys.argv) > 1 else "tr"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SRC = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.environ["TEMP"], "kayra-video", LANG)
FONTS = os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts")
W, H = 1280, 720
CYAN, CYAN2, AMBER = (38, 200, 255), (150, 232, 255), (255, 184, 48)

TXT = {
    "tr": {"l1": "KENDİ", "l2": "JARVIS'İN", "sub": "Türkçe konuşan yapay zekâ asistanı", "badge": "Ücretsiz · açık kaynak"},
    "en": {"l1": "YOUR OWN", "l2": "JARVIS", "sub": "A voice AI assistant for your PC", "badge": "Free · open source"},
}[LANG]
THUMB_AT, SHOT_AT = {"tr": (42.0, 52.0), "en": (40.0, 50.0)}[LANG]


def font(name, size, variation=None):
    f = ImageFont.truetype(os.path.join(FONTS, name), size)
    if variation:
        f.set_variation_by_name(variation)
    return f


def grab(path, t):
    """Returns the recording frame at t seconds."""
    gen = imageio_ffmpeg.read_frames(path, input_params=["-ss", "%.3f" % t])
    meta = next(gen)
    buf = next(gen)
    gen.close()
    return Image.frombytes("RGB", tuple(meta["size"]), buf)


def background():
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    img = np.zeros((H, W, 3), np.float32) + np.array([2, 9, 16], np.float32)
    for cx, cy, r, col, k in ((300, 360, 620, CYAN, 0.22), (1050, 300, 560, (40, 90, 255), 0.18)):
        g = np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * (r / 2.2) ** 2))[..., None]
        img += g * np.array(col, np.float32) * k
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")


def main():
    rec = os.path.join(SRC, "rec_%s.mp4" % LANG)
    shot_full = grab(rec, SHOT_AT)
    shot_path = os.path.join(REPO, "assets", "screenshot-%s.png" % LANG)
    shot_full.save(shot_path)
    print(shot_path)

    c = background()
    shot = grab(rec, THUMB_AT)
    sw = 820
    shot = shot.resize((sw, int(shot.height * sw / shot.width)), Image.LANCZOS).convert("RGBA")
    mask = Image.new("L", shot.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, shot.width - 1, shot.height - 1), 18, fill=255)
    shot.putalpha(mask)
    frame = Image.new("RGBA", (shot.width + 8, shot.height + 8), (0, 0, 0, 0))
    ImageDraw.Draw(frame).rounded_rectangle((0, 0, frame.width - 1, frame.height - 1), 22, fill=CYAN + (255,))
    frame.alpha_composite(shot, (4, 4))
    frame = frame.rotate(-6, resample=Image.BICUBIC, expand=True)
    glow = Image.new("RGBA", (frame.width + 120, frame.height + 120), (0, 0, 0, 0))
    glow.paste(Image.new("RGBA", frame.size, CYAN + (170,)), (60, 60), frame.split()[3])
    glow = glow.filter(ImageFilter.GaussianBlur(34))
    fx, fy = 600, 150
    c.alpha_composite(glow, (fx - 60, fy - 60))
    c.alpha_composite(frame, (fx, fy))

    shade = Image.new("RGBA", (W, H))
    sd = ImageDraw.Draw(shade)
    for x in range(W):
        sd.line((x, 0, x, H), fill=(2, 9, 16, int(max(0, 230 - 230 * x / 760))))
    c.alpha_composite(shade)

    d = ImageDraw.Draw(c)
    logo = Image.open(os.path.join(REPO, "assets", "logo-transparent.png")).convert("RGBA").resize((96, 96), Image.LANCZOS)
    c.alpha_composite(logo, (56, 50))
    d.text((166, 64), "KAYRA", font=font("bahnschrift.ttf", 60, "Bold"), fill=(230, 248, 255))

    big = font("seguibl.ttf", 110)
    for i, line in enumerate((TXT["l1"], TXT["l2"])):
        y = 190 + i * 124
        d.text((56, y + 5), line, font=big, fill=(0, 0, 0, 180))
        d.text((52, y), line, font=big, fill=(255, 255, 255) if i == 0 else CYAN2)
    d.text((58, 462), TXT["sub"], font=font("seguisb.ttf", 38), fill=(214, 236, 248))

    bf = font("seguibl.ttf", 50)
    tw = d.textlength(TXT["badge"], font=bf)
    bx, by = 52, 560
    d.rounded_rectangle((bx, by, bx + tw + 64, by + 96), 48, fill=AMBER, outline=(255, 230, 160), width=3)
    d.text((bx + 32, by + 14), TXT["badge"], font=bf, fill=(20, 16, 8))

    out = os.path.join(HERE, "kucuk-resim-%s.png" % LANG)
    c.convert("RGB").save(out)
    print(out)


if __name__ == "__main__":
    main()
