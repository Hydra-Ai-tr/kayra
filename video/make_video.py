"""KAYRA tanıtım videosu / promo video (YouTube, 1920x1080, 30 fps).

The demo in the middle is the app's own recording mode, unedited:

    kayra.exe --record <dir>/rec_tr.mp4 --script video/demo_tr.json --ffmpeg <ffmpeg.exe>

Every reply in it is Kayra's real answer (built-in engine, live weather and
Wikipedia) spoken by the Windows voice (Tolga / Zira). The music is
synthesized here, so there is nothing to license.

    python video/make_video.py tr|en [recording dir]
"""
import json
import math
import os
import subprocess
import sys
import wave

import imageio_ffmpeg
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H, FPS = 1920, 1080, 30
LANG = sys.argv[1] if len(sys.argv) > 1 else "tr"
SRC = (sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("--")
       else os.path.join(os.environ["TEMP"], "kayra-video", LANG))
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..")
FONTS = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts")

CYAN = (0, 209, 255)
CYAN2 = (150, 236, 255)
AMBER = (255, 179, 46)
TEXT = (226, 244, 255)
MUTED = (140, 178, 198)
BG = (2, 9, 16)


def font(name, size, variation=None):
    f = ImageFont.truetype(os.path.join(FONTS, name), size)
    if variation:
        try:
            f.set_variation_by_name(variation)
        except Exception:
            pass
    return f


F_TITLE = font("bahnschrift.ttf", 150, "Bold")
F_H1 = font("bahnschrift.ttf", 62, "SemiBold")
F_H2 = font("bahnschrift.ttf", 44, "SemiBold")
F_BIG = font("bahnschrift.ttf", 84, "SemiBold")
F_SUB = font("segoeui.ttf", 32)
F_SMALL = font("segoeui.ttf", 26)
F_TINY = font("segoeui.ttf", 22)
F_CAP = font("seguisb.ttf", 34)
F_TAG = font("bahnschrift.ttf", 24, "Bold")
F_FEAT = font("bahnschrift.ttf", 32, "SemiBold")
F_MONO = font("consola.ttf", 30)
F_MONOB = font("consolab.ttf", 30)

T = {
    "tr": {
        "tagline": "Bilgisayarınız için kişisel yapay zekâ asistanı",
        "no": ["Türkçe konuşur, Türkçe anlar.", "Ses tanıma bilgisayarınızda.", "C++ ile hafif ve hızlı."],
        "free": "Açık kaynak · ticari olmayan kullanım için ücretsiz",
        "marks": {
            "voice": ("“Kayra” deyin, gerisini o halletsin", "Ses tanıma tamamen bilgisayarınızda çalışır: whisper.cpp, bulut yok."),
            "info": ("Hava durumu ve anlık bilgi", "Konumunuzu kendisi bulur, Wikipedia'dan özet okur."),
            "timer": ("Zamanlayıcı ve hatırlatmalar", "Süre dolunca sesli haber verir."),
            "apps": ("Uygulamalar, siteler ve medya", "Başlat menüsündeki her uygulamayı tanır, YouTube'da arar."),
            "calc": ("Hesap yapar, sizi hatırlar", "İsterseniz yazarak da konuşabilirsiniz."),
        },
        "rec_badge": "Gerçek kayıt · uygulamanın kendisi · Kayra'nın sesi",
        "you": "SİZ",
        "brain_h": "İstediğiniz yapay zekâ beynini bağlayın",
        "brain_s": "OpenAI uyumlu her API; yerel ya da bulut. Beyin olmadan da çalışır.",
        "brains": [("Ollama", "yerel"), ("LM Studio", "yerel"), ("Kor Studio", "yerel"), ("Yerleşik komutlar", "internetsiz"),
                   ("OpenAI", "bulut"), ("Google Gemini", "bulut"), ("Groq", "bulut"), ("OpenRouter", "bulut")],
        "brain_note": "Beyin bağlıyken serbest sohbet eder ve aynı araçları kendisi kullanır: uygulama açar, arar, hatırlar.",
        "cli_h": "Komut satırı da var",
        "cli_s": "Kayra'yı betiklerden çağırın, ses dosyası çözün, kendini test etsin.",
        "cli": [("$ ", "kayra-cli ask \"240'ın yüzde 15'i kaç\""), ("", "Sonuç 36."), ("", ""),
                ("$ ", "kayra-cli transcribe komut.wav"), ("", "Kayra, 5 dakika sonra çayı demlememi hatırlat."),
                ("", "(0.66 s for 3.5 s of audio)"), ("", ""), ("$ ", "kayra-cli selftest"), ("", "120 passed, 0 failed")],
        "feat_h": "Ve dahası",
        "feats": ["Ctrl+Shift+K her yerden", "Uyandırma kelimesi: “Kayra”", "Türkçe ve İngilizce",
                  "Hafıza: sizi tanır", "Ses ve medya kontrolü", "Ekran görüntüsü, kilit, güç"],
        "out_dl": "Ücretsiz ve açık kaynak",
        "out_note": "Windows 10/11 · PolyForm Noncommercial 1.0.0 · ticari kullanım yasaktır",
        "chapters": ["Giriş", "Sesle kontrol", "Hava ve bilgi", "Hatırlatma, uygulamalar ve medya",
                     "Hesap ve hafıza", "Yapay zekâ beyni ve komut satırı"],
    },
    "en": {
        "tagline": "A personal AI assistant for your computer",
        "no": ["Talks and listens.", "Speech recognition stays on your PC.", "Light and fast, written in C++."],
        "free": "Open source · free for noncommercial use",
        "marks": {
            "voice": ("Say “Kayra” and it takes it from there", "Speech recognition runs entirely on your PC: whisper.cpp, no cloud."),
            "info": ("Weather and quick facts", "Finds your location by itself and reads summaries from Wikipedia."),
            "timer": ("Timers and reminders", "Tells you out loud when time is up."),
            "apps": ("Apps, websites and media", "Knows every app in your Start menu and searches YouTube."),
            "calc": ("Does the math, remembers you", "You can also just type to it."),
        },
        "rec_badge": "Real recording · the app itself · Kayra's own voice",
        "you": "YOU",
        "brain_h": "Plug in the AI brain you like",
        "brain_s": "Any OpenAI-compatible API, local or cloud. It also works with no brain at all.",
        "brains": [("Ollama", "local"), ("LM Studio", "local"), ("Kor Studio", "local"), ("Built-in commands", "offline"),
                   ("OpenAI", "cloud"), ("Google Gemini", "cloud"), ("Groq", "cloud"), ("OpenRouter", "cloud")],
        "brain_note": "With a brain it chats freely and uses the same tools by itself: opens apps, searches, remembers.",
        "cli_h": "There is a command line too",
        "cli_s": "Call Kayra from scripts, transcribe audio files, let it test itself.",
        "cli": [("$ ", "kayra-cli ask --lang en \"what is 15 percent of 240\""), ("", "That's 36."), ("", ""),
                ("$ ", "kayra-cli transcribe command.wav"), ("", "Kayra, remind me to call mom in ten minutes."),
                ("", "(0.6 s for 3.2 s of audio)"), ("", ""), ("$ ", "kayra-cli selftest"), ("", "120 passed, 0 failed")],
        "feat_h": "And more",
        "feats": ["Ctrl+Shift+K from anywhere", "Wake word: “Kayra”", "English and Turkish",
                  "Memory: it knows you", "Volume and media keys", "Screenshots, lock, power"],
        "out_dl": "Free and open source",
        "out_note": "Windows 10/11 · PolyForm Noncommercial 1.0.0 · no commercial use",
        "chapters": ["Intro", "Voice control", "Weather and facts", "Reminders, apps and media",
                     "Math and memory", "AI brain and command line"],
    },
}[LANG]


# ------------------------------------------------------------------ helpers

def clamp(x, a=0.0, b=1.0):
    return max(a, min(b, x))


def ease_out(x):
    x = clamp(x)
    return 1 - (1 - x) ** 3


def ease_in_out(x):
    x = clamp(x)
    return 3 * x * x - 2 * x * x * x


def ease_back(x):
    x = clamp(x)
    c = 1.70158
    return 1 + (c + 1) * (x - 1) ** 3 + c * (x - 1) ** 2


def text_layer(txt, fnt, fill=TEXT, spacing=0):
    if spacing:
        w = sum(fnt.getlength(ch) for ch in txt) + spacing * (len(txt) - 1)
        box = fnt.getbbox(txt)
        im = Image.new("RGBA", (int(w) + 8, box[3] - box[1] + 16), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        x = 4
        for ch in txt:
            d.text((x, 8 - box[1]), ch, font=fnt, fill=fill)
            x += fnt.getlength(ch) + spacing
        return im
    box = fnt.getbbox(txt)
    w, h = box[2] - box[0] + 8, box[3] - box[1] + 16
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((4 - box[0], 8 - box[1]), txt, font=fnt, fill=fill)
    return im


_cache = {}


def cached(key, fn):
    if key not in _cache:
        _cache[key] = fn()
    return _cache[key]


def with_alpha(im, a):
    if a >= 0.999:
        return im
    im = im.copy()
    im.putalpha(im.getchannel("A").point(lambda v: int(v * a)))
    return im


def put(canvas, layer, x, y, a=1.0):
    if a <= 0.001:
        return
    canvas.alpha_composite(with_alpha(layer, a), (int(round(x)), int(round(y))))


def text(canvas, txt, fnt, x, y, a=1.0, fill=TEXT, anchor="l", spacing=0):
    layer = cached(("t", txt, id(fnt), fill, spacing), lambda: text_layer(txt, fnt, fill, spacing))
    if anchor == "c":
        x -= layer.width / 2
    elif anchor == "r":
        x -= layer.width
    put(canvas, layer, x, y, a)
    return layer.width


def rounded_mask(size, r):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), r, fill=255)
    return m


# ------------------------------------------------------------------ background

def make_glow(r, color, strength):
    size = r * 2
    yy, xx = np.mgrid[0:size, 0:size]
    d = np.sqrt((xx - r) ** 2 + (yy - r) ** 2) / r
    a = np.clip(1 - d, 0, 1) ** 2.2 * strength
    im = np.zeros((size, size, 4), np.uint8)
    im[..., 0], im[..., 1], im[..., 2] = color
    im[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(im, "RGBA")


def make_base():
    g = np.linspace(0, 1, H)[:, None] * np.ones((1, W))
    rgb = [np.clip(BG[i] + g * k, 0, 255) for i, k in enumerate((2, 8, 14))]
    return Image.fromarray(np.dstack([c.astype(np.uint8) for c in rgb] + [np.full((H, W), 255, np.uint8)]), "RGBA")


def make_hex():
    im = Image.new("RGBA", (W + 120, H + 120), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = 34
    for row in range(-1, int(H / (s * 1.5)) + 4):
        for col in range(-1, int(W / (s * 1.732)) + 4):
            cx = col * s * 1.732 + (row % 2) * s * 0.866
            cy = row * s * 1.5
            dist = math.hypot(cx - W / 2, cy - H / 2) / (W * 0.62)
            a = int(max(0, 34 * (1 - dist)))
            if a <= 2:
                continue
            pts = [(cx + s * math.cos(math.pi / 3 * k + math.pi / 6), cy + s * math.sin(math.pi / 3 * k + math.pi / 6)) for k in range(6)]
            d.polygon(pts, outline=CYAN + (a,))
    return im


BASE = make_base()
HEX = make_hex()
GLOW1 = make_glow(720, CYAN, 0.20)
GLOW2 = make_glow(560, (0, 90, 200), 0.30)
GLOW3 = make_glow(420, (0, 255, 220), 0.10)
rng = np.random.default_rng(3)
DUST = [(rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0.6, 2.0), rng.uniform(0, 6.28)) for _ in range(120)]


def background(t):
    c = BASE.copy()
    c.alpha_composite(GLOW1, (int(-320 + 120 * math.sin(t * 0.21)), int(-460 + 90 * math.cos(t * 0.17))))
    c.alpha_composite(GLOW2, (int(W - 820 + 140 * math.cos(t * 0.15)), int(H - 700 + 100 * math.sin(t * 0.19))))
    c.alpha_composite(GLOW3, (int(W * 0.45 + 300 * math.sin(t * 0.11)), int(H * 0.25 + 120 * math.cos(t * 0.13))))
    c.alpha_composite(HEX, (int(-60 + 30 * math.sin(t * 0.05)), int(-60 + (t * 6) % 52)))
    d = ImageDraw.Draw(c)
    for x, y, s, ph in DUST:
        b = 0.3 + 0.3 * math.sin(t * 1.3 + ph)
        y2 = (y - t * 7 * s) % H
        d.ellipse((x - s, y2 - s, x + s, y2 + s), fill=(150, 230, 255, int(255 * b)))
    return c


# ------------------------------------------------------------------ the recording card

CARD_W, CARD_H = 1504, 846
CARD_X, CARD_Y = (W - CARD_W) // 2, 206
CARD_MASK = rounded_mask((CARD_W, CARD_H), 18)


def card_shadow():
    pad = 60
    s = Image.new("RGBA", (CARD_W + pad * 2, CARD_H + pad * 2), (0, 0, 0, 0))
    ImageDraw.Draw(s).rounded_rectangle((pad, pad + 18, pad + CARD_W, pad + CARD_H + 18), 22, fill=(0, 0, 0, 190))
    s = s.filter(ImageFilter.GaussianBlur(28))
    rim = Image.new("RGBA", s.size, (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle((pad - 2, pad - 2, pad + CARD_W + 1, pad + CARD_H + 1), 20,
                                          outline=CYAN + (150,), width=2)
    s.alpha_composite(rim.filter(ImageFilter.GaussianBlur(1)))
    return s, pad


SHADOW, SHADOW_PAD = card_shadow()


class Recording:
    """Reads the demo recording frame by frame, in order."""

    def __init__(self, path):
        self.gen = imageio_ffmpeg.read_frames(path, pix_fmt="rgb24")
        meta = next(self.gen)
        self.size = tuple(meta["size"])
        self.idx = -1
        self.buf = None
        self.img = None
        self.img_idx = -2

    def frame(self, i):
        while self.idx < i:
            try:
                self.buf = next(self.gen)
            except StopIteration:
                break
            self.idx += 1
        if self.img_idx != self.idx and self.buf is not None:
            self.img = Image.frombytes("RGB", self.size, self.buf)
            self.img_idx = self.idx
        return self.img


def screen(canvas, img, a, zoom=1.0, focus=(0.5, 0.5), rise=0.0):
    sw, sh = img.size
    cw, ch = sw / zoom, sh / zoom
    x0 = clamp(focus[0] * sw - cw / 2, 0, sw - cw)
    y0 = clamp(focus[1] * sh - ch / 2, 0, sh - ch)
    crop = img.crop((int(x0), int(y0), int(x0 + cw), int(y0 + ch))).resize((CARD_W, CARD_H), Image.BICUBIC)
    layer = crop.convert("RGBA")
    layer.putalpha(CARD_MASK)
    y = CARD_Y + rise
    put(canvas, SHADOW, CARD_X - SHADOW_PAD, y - SHADOW_PAD, a)
    put(canvas, layer, CARD_X, y, a)


def heading(canvas, h, s, lt, a):
    e1, e2 = ease_out(lt / 0.6), ease_out((lt - 0.15) / 0.6)
    bar = cached("hbar", lambda: Image.new("RGBA", (8, 58), CYAN + (255,)))
    put(canvas, bar, CARD_X, 52 + (1 - e1) * 20, a * e1)
    text(canvas, h, F_H1, CARD_X + 26, 36 + (1 - e1) * 24, a * e1)
    text(canvas, s, F_SUB, CARD_X + 28, 120 + (1 - e2) * 24, a * e2, fill=MUTED)


def badge(canvas, txt, x, y, a):
    def mk():
        tl = text_layer(txt, F_TINY, (200, 240, 255))
        w, h = tl.width + 46, 44
        b = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(b)
        d.rounded_rectangle((0, 0, w - 1, h - 1), 22, fill=(0, 40, 60, 230), outline=CYAN + (200,), width=2)
        d.ellipse((16, h / 2 - 5, 26, h / 2 + 5), fill=(255, 70, 70, 255))
        b.alpha_composite(tl, (32, (h - tl.height) // 2))
        return b
    b = cached(("badge", txt), mk)
    put(canvas, b, x - b.width, y, a)


# ------------------------------------------------------------------ captions

def wrap(txt, fnt, maxw):
    lines, cur = [], ""
    for w in txt.split():
        t = (cur + " " + w).strip()
        if fnt.getlength(t) <= maxw or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def caption_layer(tag, txt, color):
    lines = wrap(txt, F_CAP, 1080)[:2]
    tag_l = text_layer(tag, F_TAG, color, spacing=3)
    lls = [text_layer(l, F_CAP, TEXT) for l in lines]
    w = max(l.width for l in lls) + tag_l.width + 76
    h = 30 + 46 * len(lls)
    im = Image.new("RGBA", (int(w), int(h)), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 16, fill=(2, 14, 24, 228), outline=color + (210,), width=2)
    im.alpha_composite(tag_l, (24, 18))
    for i, l in enumerate(lls):
        im.alpha_composite(l, (tag_l.width + 48, 12 + i * 46))
    return im


def voice_chunks(txt, start, dur):
    """Splits a reply into subtitle chunks timed by their length."""
    parts, cur = [], ""
    for s in txt.replace("? ", "?|").replace(". ", ".|").replace("! ", "!|").split("|"):
        if len(cur) + len(s) < 70:
            cur = (cur + " " + s).strip()
        else:
            if cur:
                parts.append(cur)
            cur = s
    if cur:
        parts.append(cur)
    out, t = [], start
    total = sum(len(p) for p in parts) or 1
    for p in parts:
        d = dur * len(p) / total
        out.append((t, t + d, p))
        t += d
    return out


# ------------------------------------------------------------------ scenes

LOGO = Image.open(os.path.join(REPO, "assets", "logo-transparent.png")).convert("RGBA")
MANIFEST = json.load(open(os.path.join(SRC, "rec_%s.json" % LANG), encoding="utf-8"))
REC_PATH = os.path.join(SRC, MANIFEST["video"])
REC_DUR = MANIFEST["duration"]
EVENTS = MANIFEST["events"]
MARKS = [(e["t"], e["name"]) for e in EVENTS if e["type"] == "mark"]
CAPTIONS = []
for e in EVENTS:
    if e["type"] == "user":
        CAPTIONS.append((e["t"] + 0.2, e["t"] + e["dur"] + 1.4, T["you"], e["text"], AMBER))
    elif e["type"] == "voice":
        for s, en, p in voice_chunks(e["text"], e["t"], e["dur"]):
            CAPTIONS.append((s, en + 0.15, "KAYRA", p, CYAN))
FOCUS = {"voice": (1.0, (0.5, 0.5)), "info": (1.0, (0.5, 0.5)), "timer": (1.22, (0.28, 0.62)),
         "apps": (1.0, (0.5, 0.5)), "calc": (1.2, (0.78, 0.6))}
REC = None


def logo_spin(c, cx, cy, size, a, t):
    lg = cached(("logo", size), lambda: LOGO.resize((size, size), Image.LANCZOS))
    glow = cached(("lglow", size), lambda: make_glow(int(size * 0.9), CYAN, 0.35))
    put(c, glow, cx - glow.width / 2, cy - glow.height / 2, a * (0.8 + 0.2 * math.sin(t * 2)))
    put(c, lg, cx - size / 2, cy - size / 2, a)
    # orbiting arcs around the emblem
    ring = Image.new("RGBA", (size + 160, size + 160), (0, 0, 0, 0))
    d = ImageDraw.Draw(ring)
    r0 = size / 2 + 40
    o = 80
    for k, (span, speed, wdt) in enumerate(((70, 40, 4), (40, -60, 3), (110, 25, 2))):
        st = (t * speed + k * 120) % 360
        rr = r0 + k * 14
        d.arc((o + size / 2 - rr, o + size / 2 - rr, o + size / 2 + rr, o + size / 2 + rr), st, st + span,
              fill=CYAN + (200,), width=wdt)
    put(c, ring, cx - ring.width / 2, cy - ring.height / 2, a)


def scene_intro(c, lt, d, a):
    s = ease_back((lt - 0.2) / 1.0)
    if s > 0:
        size = max(8, int(300 * s))
        logo_spin(c, W / 2, 330, size, a * clamp((lt - 0.2) / 0.4), lt)
    e = ease_out((lt - 1.1) / 0.8)
    text(c, "KAYRA", F_TITLE, W / 2, 545 + (1 - e) * 30, a * e, anchor="c", spacing=34)
    e2 = ease_out((lt - 1.8) / 0.8)
    text(c, T["tagline"], F_H2, W / 2, 745 + (1 - e2) * 24, a * e2, fill=CYAN2, anchor="c")
    e3 = ease_out((lt - 2.4) / 0.8)
    line = cached("intro_line", lambda: Image.new("RGBA", (560, 3), CYAN + (200,)))
    w = int(560 * e3)
    if w > 2:
        put(c, line.crop((0, 0, w, 3)), W / 2 - w / 2, 722, a)


def scene_no(c, lt, d, a):
    for i, s in enumerate(T["no"]):
        e = ease_out((lt - 0.25 - i * 0.55) / 0.6)
        text(c, s, F_BIG, W / 2, 250 + i * 140 + (1 - e) * 40, a * e, anchor="c")
    e = ease_out((lt - 2.2) / 0.7)
    text(c, T["free"], F_H2, W / 2, 750 + (1 - e) * 20, a * e, fill=CYAN2, anchor="c")


def scene_demo(c, lt, d, a):
    global REC
    if REC is None:
        REC = Recording(REC_PATH)
    rt = min(lt, REC_DUR - 1 / FPS)
    img = REC.frame(int(rt * FPS))
    # heading of the current part
    cur = 0
    for i, (mt, _) in enumerate(MARKS):
        if rt >= mt - 0.2:
            cur = i
    mt, name = MARKS[cur]
    nxt = MARKS[cur + 1][0] if cur + 1 < len(MARKS) else 1e9
    ha = clamp((nxt - 0.2 - rt) / 0.35)
    h, s = T["marks"][name]
    heading(c, h, s, rt - mt + 0.2 if cur else lt, a * ha)
    # camera: ease toward the part's focus
    prev = FOCUS[MARKS[cur - 1][1]] if cur else FOCUS[name]
    tgt = FOCUS[name]
    k = ease_in_out((rt - mt) / 1.4) if cur else 1.0
    zoom = prev[0] + (tgt[0] - prev[0]) * k
    fx = prev[1][0] + (tgt[1][0] - prev[1][0]) * k
    fy = prev[1][1] + (tgt[1][1] - prev[1][1]) * k
    e = ease_out(lt / 0.8)
    screen(c, img, a * e, zoom, (fx, fy), rise=(1 - e) * 60)
    badge(c, T["rec_badge"], CARD_X + CARD_W, 58, a * ease_out((lt - 0.8) / 0.5))
    # captions
    for s0, s1, tag, txt, col in CAPTIONS:
        if s0 <= rt < s1:
            cl = cached(("cap", tag, txt), lambda: caption_layer(tag, txt, col))
            ca = clamp((rt - s0) / 0.18) * clamp((s1 - rt) / 0.18)
            put(c, cl, W / 2 - cl.width / 2, CARD_Y + CARD_H - cl.height - 26 + (1 - ca) * 10, a * ca)
            break


def scene_brains(c, lt, d, a):
    e1, e2 = ease_out(lt / 0.6), ease_out((lt - 0.15) / 0.6)
    text(c, T["brain_h"], F_H1, W / 2, 120 + (1 - e1) * 24, a * e1, anchor="c")
    text(c, T["brain_s"], F_SUB, W / 2, 214 + (1 - e2) * 24, a * e2, fill=MUTED, anchor="c")
    cw, ch, gap = 380, 150, 30
    x0 = (W - (cw * 4 + gap * 3)) / 2
    for i, (name, kind) in enumerate(T["brains"]):
        r, col = divmod(i, 4)
        ee = ease_back((lt - 0.5 - i * 0.12) / 0.6)
        if ee <= 0:
            continue
        card = cached(("bc", i), lambda: _brain_card(name, kind, cw, ch, i == 3))
        put(c, card, x0 + col * (cw + gap), 360 + r * (ch + gap) + (1 - ee) * 50, a * clamp(ee))
    e3 = ease_out((lt - 2.2) / 0.7)
    text(c, T["brain_note"], F_SMALL, W / 2, 760 + (1 - e3) * 16, a * e3, fill=(170, 210, 228), anchor="c")


def _brain_card(name, kind, w, h, special):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    edge = AMBER if special else CYAN
    d.rounded_rectangle((0, 0, w - 1, h - 1), 18, fill=(3, 26, 40, 235), outline=edge + (170,), width=2)
    tl = text_layer(name, F_FEAT, TEXT)
    im.alpha_composite(tl, (int(w / 2 - tl.width / 2), 32))
    up = kind.replace("i", "İ").upper() if LANG == "tr" else kind.upper()
    kl = text_layer(up, F_TAG, edge, spacing=3)
    im.alpha_composite(kl, (int(w / 2 - kl.width / 2), 90))
    return im


def _term_window():
    w, h = 1400, 680
    pad = SHADOW_PAD
    im = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 18, pad + w, pad + h + 18), 20, fill=(0, 0, 0, 180))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(26)))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((pad, pad, pad + w, pad + h), 18, fill=(4, 14, 22, 250), outline=CYAN + (140,), width=2)
    d.rounded_rectangle((pad, pad, pad + w, pad + 60), 18, fill=(8, 30, 44, 255))
    d.rectangle((pad, pad + 40, pad + w, pad + 60), fill=(8, 30, 44, 255))
    for k, col in enumerate(((255, 95, 87), (254, 188, 46), (40, 200, 64))):
        d.ellipse((pad + 26 + k * 34, pad + 20, pad + 46 + k * 34, pad + 40), fill=col)
    tl = text_layer("kayra-cli", F_SMALL, (150, 200, 220))
    im.alpha_composite(tl, (pad + w // 2 - tl.width // 2, pad + 12))
    return im


def scene_cli(c, lt, d, a):
    e1, e2 = ease_out(lt / 0.6), ease_out((lt - 0.15) / 0.6)
    text(c, T["cli_h"], F_H1, W / 2, 50 + (1 - e1) * 24, a * e1, anchor="c")
    text(c, T["cli_s"], F_SUB, W / 2, 140 + (1 - e2) * 24, a * e2, fill=MUTED, anchor="c")
    e = ease_out(lt / 0.7)
    win = cached("term", _term_window)
    wx, wy = (W - 1400) / 2, 250 + (1 - e) * 50
    put(c, win, wx - SHADOW_PAD, wy - SHADOW_PAD, a * e)
    x, y = wx + 46, wy + 96
    t = lt - 0.7
    clock = 0.0
    for prompt, line in T["cli"]:
        if prompt:
            n = int((t - clock) * 30)
            if n < 0:
                break
            shown = line[:n]
            text(c, prompt, F_MONOB, x, y, a, fill=(110, 240, 170))
            pw = F_MONO.getlength(prompt)
            text(c, shown, F_MONO, x + pw, y, a)
            if n < len(line) or int(lt * 2.5) % 2 == 0:
                cur = cached("cursor", lambda: Image.new("RGBA", (15, 34), (220, 240, 250, 255)))
                if n < len(line):
                    put(c, cur, x + pw + F_MONO.getlength(shown) + 3, y + 6, a)
                    break
            clock += len(line) / 30 + 0.45
        else:
            if t < clock:
                break
            if line:
                col = CYAN2 if not line.startswith("(") else MUTED
                text(c, line, F_MONO, x, y, a * ease_out((t - clock) / 0.25), fill=col)
            clock += 0.12
        y += 52


def scene_features(c, lt, d, a):
    e = ease_out(lt / 0.6)
    text(c, T["feat_h"], F_H1, W / 2, 210 + (1 - e) * 24, a * e, anchor="c")
    cw, ch, gap = 560, 150, 36
    x0 = (W - (cw * 3 + gap * 2)) / 2
    for i, s in enumerate(T["feats"]):
        r, col = divmod(i, 3)
        ee = ease_back((lt - 0.4 - i * 0.16) / 0.6)
        if ee <= 0:
            continue
        card = cached(("fc", i), lambda: _feat_card(s, cw, ch))
        put(c, card, x0 + col * (cw + gap), 400 + r * (ch + gap) + (1 - ee) * 50, a * clamp(ee))


def _feat_card(s, w, h):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 18, fill=(3, 26, 40, 235), outline=CYAN + (170,), width=2)
    d.ellipse((34, h / 2 - 9, 52, h / 2 + 9), fill=CYAN2)
    tl = text_layer(s, F_FEAT, TEXT)
    im.alpha_composite(tl, (72, int(h / 2 - tl.height / 2)))
    return im


def _pill(tl):
    w, h = tl.width + 90, tl.height + 40
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle((0, 0, w - 1, h - 1), h // 2, fill=(0, 60, 90, 255), outline=CYAN2 + (255,), width=3)
    im.alpha_composite(tl, (45, 20))
    return im


def scene_outro(c, lt, d, a):
    s = ease_back(lt / 0.9)
    logo_spin(c, W / 2, 245, max(8, int(220 * s)), a * clamp(lt / 0.4), lt)
    e = ease_out((lt - 0.5) / 0.7)
    text(c, "KAYRA", F_TITLE, W / 2, 400 + (1 - e) * 30, a * e, anchor="c", spacing=34)
    e2 = ease_out((lt - 1.0) / 0.7)
    text(c, T["out_dl"], F_H2, W / 2, 600 + (1 - e2) * 20, a * e2, fill=MUTED, anchor="c")
    e3 = ease_out((lt - 1.3) / 0.7)
    tl = cached("url", lambda: text_layer("github.com/Hydra-Ai-tr/kayra", font("seguisb.ttf", 58), TEXT))
    pill = cached("pill", lambda: _pill(tl))
    put(c, pill, W / 2 - pill.width / 2, 680 + (1 - e3) * 20, a * e3)
    e4 = ease_out((lt - 1.8) / 0.7)
    text(c, T["out_note"], F_SMALL, W / 2, 850, a * e4, fill=(130, 170, 190), anchor="c")


SCENES = [
    ("intro", 5.0, scene_intro),
    ("no", 5.2, scene_no),
    ("demo", REC_DUR + 0.6, scene_demo),
    ("brains", 7.5, scene_brains),
    ("cli", 9.0, scene_cli),
    ("features", 5.5, scene_features),
    ("outro", 6.5, scene_outro),
]
FADE = 0.45


def timeline():
    t, out = 0.0, []
    for name, d, fn in SCENES:
        out.append((name, t, d, fn))
        t += d
    return out, t


# ------------------------------------------------------------------ audio

SR = 44100


def read_wav(path):
    with wave.open(path, "rb") as wf:
        sr, n, ch = wf.getframerate(), wf.getnframes(), wf.getnchannels()
        x = np.frombuffer(wf.readframes(n), np.int16).astype(np.float64) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(1)
    if sr != SR:
        x = np.interp(np.arange(int(len(x) * SR / sr)) * sr / SR, np.arange(len(x)), x)
    return x


def voice_track(total, demo_start):
    n = int(total * SR)
    v = np.zeros(n)
    duck = np.ones(n)
    for e in EVENTS:
        if e["type"] != "voice":
            continue
        x = read_wav(os.path.join(SRC, e["wav"]))
        i0 = int((demo_start + e["t"]) * SR)
        L = min(len(x), n - i0)
        if L <= 0:
            continue
        v[i0:i0 + L] += x[:L]
        a0, a1 = max(0, i0 - int(0.15 * SR)), min(n, i0 + L + int(0.45 * SR))
        duck[a0:a1] = 0.32
    # smooth the ducking
    k = int(0.25 * SR)
    cs = np.concatenate([[0.0], np.cumsum(np.pad(duck, (k // 2, k - k // 2 - 1), mode="edge"))])
    duck = (cs[k:] - cs[:-k]) / k
    peak = np.abs(v).max() + 1e-9
    return v / peak * 0.92, duck


def music(total, starts):
    n = int(total * SR)
    t = np.arange(n) / SR
    bpm = 100
    beat = 60 / bpm
    left = np.zeros(n)
    right = np.zeros(n)
    hz = lambda m: 440 * 2 ** ((m - 69) / 12)
    # D minor - Bb - F - C, two bars each
    prog = [(50, [62, 65, 69]), (46, [58, 62, 65]), (53, [60, 65, 69]), (48, [60, 64, 67])]
    chord_len = beat * 8
    k, cs = 0, 0.0
    while cs < total:
        root, notes = prog[k % 4]
        i0, i1 = int(cs * SR), min(n, int((cs + chord_len + 1.0) * SR))
        tt = t[i0:i1] - cs
        e = np.clip(tt / 1.2, 0, 1) * np.clip((chord_len + 1.0 - tt) / 1.2, 0, 1)
        for m in notes:
            for det, side in ((-0.1, 0), (0.1, 1)):
                f = hz(m) * (1 + det / 100)
                w = np.sin(2 * np.pi * f * tt) + 0.3 * np.sin(4 * np.pi * f * tt) + 0.1 * np.sin(6 * np.pi * f * tt)
                (left if side == 0 else right)[i0:i1] += 0.045 * e * w
        bass = (np.sin(2 * np.pi * hz(root - 12) * tt) + 0.3 * np.sin(4 * np.pi * hz(root - 12) * tt)) * 0.14 * e
        left[i0:i1] += bass
        right[i0:i1] += bass
        cs += chord_len
        k += 1
    drum_on, arp_on, end = starts["no"], starts["demo"], starts["outro"] + 2.5
    rng2 = np.random.default_rng(7)
    kick = np.zeros(n)
    pump = np.ones(n)
    bt = drum_on
    while bt < end:
        i0 = int(bt * SR)
        L = min(n - i0, int(0.35 * SR))
        x = np.arange(L) / SR
        f = 48 + 95 * np.exp(-x * 32)
        kick[i0:i0 + L] += np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-x * 9) * 0.5
        D = min(n - i0, int(0.3 * SR))
        pump[i0:i0 + D] = np.minimum(pump[i0:i0 + D], 0.55 + 0.45 * (np.arange(D) / D))
        bt += beat
    hat = np.zeros(n)
    bt = drum_on + beat / 2
    while bt < end:
        i0 = int(bt * SR)
        L = min(n - i0, int(0.05 * SR))
        nz = np.diff(np.concatenate([[0], rng2.standard_normal(L)]))
        hat[i0:i0 + L] += nz * np.exp(-np.arange(L) / SR * 80) * 0.045
        bt += beat
    arp = np.zeros(n)
    bt, step = arp_on, 0
    while bt < end:
        notes = prog[int(bt // chord_len) % 4][1]
        m = notes[[0, 1, 2, 1, 2, 0, 1, 2][step % 8]] + 12
        i0 = int(bt * SR)
        L = min(n - i0, int(0.35 * SR))
        x = np.arange(L) / SR
        arp[i0:i0 + L] += (np.sin(2 * np.pi * hz(m) * x) + 0.25 * np.sign(np.sin(2 * np.pi * hz(m) * x)) * 0.3) * np.exp(-x * 10) * 0.05
        bt += beat / 2
        step += 1
    pan = 0.5 + 0.3 * np.sin(t * 0.7)
    left = left * pump + kick + hat * 0.8 + arp * (1 - pan)
    right = right * pump + kick + hat * 1.2 + arp * pan
    for s in list(starts.values())[1:]:
        i0 = int(max(0, s - 0.35) * SR)
        L = min(n - i0, int(0.7 * SR))
        x = np.arange(L) / L
        sm = np.convolve(rng2.standard_normal(L), np.ones(24) / 24, mode="same")
        a = np.sin(np.pi * x) ** 2 * 0.06
        left[i0:i0 + L] += sm * a
        right[i0:i0 + L] += sm * a * 0.9
    fade = np.clip(t / 2.0, 0, 1) * np.clip((total - t) / 3.0, 0, 1)
    st = np.stack([left * fade, right * fade], 1)
    return st / (np.abs(st).max() + 1e-9)


def soundtrack(total, starts):
    mus = music(total, starts)
    voice, duck = voice_track(total, starts["demo"])
    mix = mus * 0.5 * duck[:, None] + voice[:, None]
    mix = np.tanh(mix * 1.1) / np.tanh(1.1)
    path = os.path.join(SRC, "soundtrack_%s.wav" % LANG)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes((np.clip(mix, -1, 1) * 32767).astype(np.int16).tobytes())
    return path


# ------------------------------------------------------------------ render

def frame_at(tl, t):
    c = background(t)
    for name, s, d, fn in tl:
        if s <= t < s + d:
            lt = t - s
            a = clamp(lt / FADE) * clamp((d - lt) / FADE)
            if name == "intro":
                a = clamp((d - lt) / FADE)
            if name == "outro":
                a = clamp(lt / FADE) * clamp((d - lt) / 1.2)
            fn(c, lt, d, a)
    return c


def stills(times):
    global REC
    tl, total = timeline()
    for t in times:
        REC = None
        frame_at(tl, t).convert("RGB").resize((960, 540), Image.LANCZOS).save(os.path.join(SRC, "still_%s_%05.1f.png" % (LANG, t)))
    print("total %.1fs" % total, {name: round(s, 1) for name, s, _, _ in tl})


def render():
    tl, total = timeline()
    starts = {name: s for name, s, _, _ in tl}
    wav = soundtrack(total, starts)
    out = os.path.join(HERE, "kayra-tanitim-%s.mp4" % LANG)
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    cmd = [ff, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS),
           "-i", "-", "-i", wav, "-c:v", "libx264", "-preset", "slow", "-crf", "20", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", out]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    frames = int(total * FPS)
    for fi in range(frames):
        p.stdin.write(frame_at(tl, fi / FPS).convert("RGB").tobytes())
        if fi % 300 == 0:
            print("%s %d/%d" % (LANG, fi, frames), flush=True)
    p.stdin.close()
    p.wait()
    # chapters for the YouTube description
    marks = {name: starts["demo"] + mt for mt, name in MARKS}
    points = [0, marks["voice"], marks["info"], marks["timer"], marks["calc"], starts["brains"]]
    with open(os.path.join(HERE, "bolumler-%s.txt" % LANG), "w", encoding="utf-8") as f:
        for s, title in zip(points, T["chapters"]):
            f.write("%d:%02d %s\n" % (int(s) // 60, int(s) % 60, title))
    print("done", out, "%.1fs" % total)


if __name__ == "__main__":
    if "--still" in sys.argv:
        stills([float(x) for x in sys.argv[sys.argv.index("--still") + 1].split(",")])
    else:
        render()
