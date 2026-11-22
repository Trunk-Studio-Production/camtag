#!/usr/bin/env python3
"""Provisional promo images built from the manual screenshots.

Writes docs/img/screens.png (README strip) and docs/img/social-preview.png
(GitHub social preview, 1280x640: Settings > General > Social preview).
Replace them with real on-set photos when we have them.

Usage: python3 scripts/promo_images.py   (needs Pillow)
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SHOTS = ROOT / 'companion' / 'manual'
OUT = ROOT / 'docs' / 'img'
FONT = str(ROOT / 'companion' / 'fonts' / 'InterVariable.woff2')
BG, INK, MUTED, ACCENT = '#121212', '#ecebe6', '#9a9993', '#bbff00'
BODY = '#2b2b29'  # device frame, close to the X3's dark grey


def font(size, weight='Bold'):
    f = ImageFont.truetype(FONT, size)
    f.set_variation_by_name(weight)
    return f


def device(name, h):
    """A screenshot inside a simple rounded frame, height h."""
    shot = Image.open(SHOTS / f'{name}.png').convert('RGB')
    pad = round(h * 0.035)
    sh = h - 2 * pad
    sw = round(shot.width * sh / shot.height)
    shot = shot.resize((sw, sh), Image.LANCZOS)
    w = sw + 2 * pad
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle((0, 0, w - 1, h - 1), radius=round(h * 0.05), fill=BODY)
    img.paste(shot, (pad, pad))
    return img


def strip(names, h, gap):
    devs = [device(n, h) for n in names]
    w = sum(d.width for d in devs) + gap * (len(devs) - 1)
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    x = 0
    for d in devs:
        img.alpha_composite(d, (x, 0))
        x += d.width + gap
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    strip(['lens-port', 'filters-port', 'crew-port'], 560, 40).save(OUT / 'screens.png', optimize=True)

    img = Image.new('RGBA', (1280, 640), BG)
    d = ImageDraw.Draw(img)
    devs = strip(['lens-port', 'filters-port'], 500, 28)
    img.alpha_composite(devs, (1280 - 72 - devs.width, (640 - devs.height) // 2))
    x, y = 72, 178
    d.text((x, y), 'CamTag', font=font(88, 'ExtraBold'), fill=INK)
    y += 120
    for line in ('E-paper badge for', 'the camera department'):
        d.text((x, y), line, font=font(30, 'SemiBold'), fill=INK)
        y += 40
    y += 26
    d.rectangle((x, y, x + 56, y + 5), fill=ACCENT)
    y += 26
    for line in ('Lens chart, filters, crew, notes.', 'Free and open source.'):
        d.text((x, y), line, font=font(22, 'Medium'), fill=MUTED)
        y += 32
    img.convert('RGB').save(OUT / 'social-preview.png', optimize=True)
    print('written', OUT / 'screens.png', OUT / 'social-preview.png')


if __name__ == '__main__':
    main()
