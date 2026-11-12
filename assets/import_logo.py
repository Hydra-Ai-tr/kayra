"""Builds the app's logo files from assets/logo-source.png (the K emblem, transparent).

    python assets/import_logo.py

logo-mark.png  full emblem, square
logo-k.png     only the glowing K (no dark disc), for the HUD core and the sidebar
kayra.ico      window and taskbar icon
"""
from pathlib import Path

import numpy as np
from PIL import Image

here = Path(__file__).parent
rgba = np.asarray(Image.open(here / "logo-source.png").convert("RGBA")).astype(np.float32) / 255.0
src, alpha = rgba[..., :3], rgba[..., 3]
mark = rgba

ys, xs = np.nonzero(alpha > 0.04)
y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
side = max(y1 - y0, x1 - x0) + 16
cy, cx = (y0 + y1) // 2, (x0 + x1) // 2
img = Image.fromarray((mark * 255).astype(np.uint8), "RGBA")
img = img.crop((cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2))
img.resize((512, 512), Image.LANCZOS).save(here / "logo-mark.png")

# the K alone: bright cyan pixels inside the letter's box, the dark disc made transparent
lum = src.max(axis=2)
k_alpha = np.clip((lum - 0.30) / 0.45, 0.0, 1.0) * (src[..., 2] > src[..., 0] + 0.15) * alpha
h, w = k_alpha.shape
box = (int(w * 0.318), int(h * 0.272), int(w * 0.722), int(h * 0.69))
krgb = np.clip(src / np.maximum(lum, 1e-4)[..., None], 0, 1)
k = np.dstack([krgb, k_alpha])
kimg = Image.fromarray((k * 255).astype(np.uint8), "RGBA").crop(box)
kw, kh = kimg.size
s = max(kw, kh) + 24
canvas = Image.new("RGBA", (s, s), (0, 0, 0, 0))
canvas.paste(kimg, ((s - kw) // 2, (s - kh) // 2))
canvas.resize((256, 256), Image.LANCZOS).save(here / "logo-k.png")

# icon: the emblem on a dark rounded square so it reads on light and dark taskbars
icon = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
emblem = Image.open(here / "logo-mark.png").resize((256, 256), Image.LANCZOS)
icon.alpha_composite(emblem)
icon.save(here / "kayra.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("logo-mark.png, logo-k.png, kayra.ico")
