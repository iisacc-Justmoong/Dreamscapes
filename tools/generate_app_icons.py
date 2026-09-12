#!/usr/bin/env python3
"""Convert Dreamscapes's supplied artwork into deterministic platform icon assets.

Requires Pillow (asset generation only, never an application dependency).
The Illustrator source and exported PNG are left untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'resources/Appicon/Artboard 1.png'
OUTPUT = ROOT / 'resources/Appicon/generated'
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
MATTE = (20, 16, 39)


def generate(source=SOURCE, output=OUTPUT):
    source, output = Path(source), Path(output)
    original = Image.open(source).convert('RGBA')
    if original.size != (1024, 1024):
        raise ValueError('The approved export must be exactly 1024 × 1024 pixels.')
    artwork = Image.alpha_composite(Image.new('RGBA', original.size, MATTE + (255,)), original).convert('RGB')
    emitted = {}

    def save(relative, im):
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        im.save(path, format='PNG', optimize=True)
        emitted[relative] = {'width': im.width, 'height': im.height, 'mode': im.mode}
        return path

    def write(relative, value):
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)
        emitted[relative] = {}

    def resize(im, size):
        return im.resize((size, size), Image.Resampling.LANCZOS)

    save('common/Dreamscapes.png', artwork)
    save('store/app-store-1024.png', artwork)
    save('store/google-play-512.png', resize(artwork, 512))

    # Legacy macOS ICNS needs an authored silhouette; iOS applies its own mask.
    mac = Image.new('RGBA', (1024, 1024))
    mask = Image.new('L', (1024, 1024))
    ImageDraw.Draw(mask).rounded_rectangle((100, 100, 923, 923), radius=185, fill=255)
    shadow_mask = Image.new('L', (1024, 1024))
    shadow_mask.paste(mask, (0, 12))
    shadow = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    shadow.putalpha(shadow_mask.filter(ImageFilter.GaussianBlur(16)).point(lambda a: round(a * 0.24)))
    mac.alpha_composite(shadow)
    tile = Image.new('RGBA', (1024, 1024))
    tile.paste(resize(artwork, 824), (100, 100))
    tile.putalpha(mask)
    mac.alpha_composite(tile)
    iconset = 'macos/Dreamscapes.iconset'
    for points in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            suffix = '@2x' if scale == 2 else ''
            save(f'{iconset}/icon_{points}x{points}{suffix}.png', resize(mac, points * scale))
    save('macos/Dreamscapes.png', resize(mac, 512))
    mac.save(output / 'macos/Dreamscapes.icns', format='ICNS')
    emitted['macos/Dreamscapes.icns'] = {}

    # Explicit sizes also support the project's iOS 16 minimum deployment target.
    slots = []
    for idiom, sizes in [('iphone', [(20, (2, 3)), (29, (2, 3)), (40, (2, 3)), (60, (2, 3))]),
                         ('ipad', [(20, (1, 2)), (29, (1, 2)), (40, (1, 2)), (76, (1, 2)), (83.5, (2,))]),
                         ('ios-marketing', [(1024, (1,))])]:
        for points, scales in sizes:
            for scale in scales:
                name = f'icon-{points}@{scale}x.png'
                save(f'ios/Assets.xcassets/AppIcon.appiconset/{name}', resize(artwork, round(points * scale)))
                slots.append({'idiom': idiom, 'size': f'{points}x{points}', 'scale': f'{scale}x', 'filename': name})
    info = {'author': 'iisacc', 'version': 1}
    write('ios/Assets.xcassets/Contents.json', json.dumps({'info': info}, indent=2) + '\n')
    write('ios/Assets.xcassets/AppIcon.appiconset/Contents.json', json.dumps({'images': slots, 'info': info}, indent=2) + '\n')

    foreground = Image.new('RGBA', (432, 432))
    foreground.paste(resize(artwork, 264), (84, 84))  # 66dp artwork in a 108dp layer.
    monochrome = Image.new('RGBA', (432, 432), (255, 255, 255, 0))
    luminance = ImageOps.grayscale(resize(artwork, 264)).point(lambda v: max(0, min(255, round((v - 40) * 255 / 200))))
    mono_alpha = Image.new('L', (432, 432))
    mono_alpha.paste(luminance, (84, 84))
    monochrome.putalpha(mono_alpha)
    for density, factor in [('ldpi', .75), ('mdpi', 1), ('hdpi', 1.5), ('xhdpi', 2), ('xxhdpi', 3), ('xxxhdpi', 4)]:
        size = round(48 * factor)
        save(f'android/res/mipmap-{density}/ic_launcher.png', resize(artwork, size))
        round_icon = resize(artwork, size).convert('RGBA')
        circle = Image.new('L', (size * 4, size * 4))
        ImageDraw.Draw(circle).ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
        round_icon.putalpha(resize(circle, size))
        save(f'android/res/mipmap-{density}/ic_launcher_round.png', round_icon)
        save(f'android/res/drawable-{density}/ic_launcher_foreground.png', resize(foreground, round(108 * factor)))
        save(f'android/res/drawable-{density}/ic_launcher_monochrome.png', resize(monochrome, round(108 * factor)))
    write('android/res/values/icon_colors.xml', '<resources><color name="ic_launcher_background">#141027</color></resources>\n')
    for version in (26, 33):
        mono = '\n    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />' if version == 33 else ''
        xml = ('<?xml version="1.0" encoding="utf-8"?>\n<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
               '    <background android:drawable="@color/ic_launcher_background" />\n'
               '    <foreground android:drawable="@drawable/ic_launcher_foreground" />' + mono + '\n</adaptive-icon>\n')
        for name in ('ic_launcher', 'ic_launcher_round'):
            write(f'android/res/mipmap-anydpi-v{version}/{name}.xml', xml)

    win = output / 'windows/Dreamscapes.ico'
    win.parent.mkdir(parents=True, exist_ok=True)
    artwork.save(win, format='ICO', sizes=[(size, size) for size in SIZES])
    emitted['windows/Dreamscapes.ico'] = {'sizes': list(SIZES)}
    for size in (16, 22, 24, 32, 48, 64, 128, 256, 512, 1024):
        save(f'linux/hicolor/{size}x{size}/apps/com.iisacc.dreamscapes.png', resize(artwork, size))
    for size in (16, 32, 48, 192, 512):
        save(f'web/icon-{size}.png', resize(artwork, size))
    save('web/apple-touch-icon.png', resize(artwork, 180))
    artwork.save(output / 'web/favicon.ico', format='ICO', sizes=[(s, s) for s in (16, 32, 48)])
    emitted['web/favicon.ico'] = {'sizes': [16, 32, 48]}
    maskable = Image.new('RGB', (1024, 1024), MATTE)
    maskable.paste(resize(artwork, 720), (152, 152))
    for size in (192, 512):
        save(f'web/maskable-{size}.png', resize(maskable, size))
    write('web/manifest.webmanifest', json.dumps({
        'id': './', 'name': 'Dreamscapes', 'short_name': 'Dreamscapes', 'start_url': './Dreamscapes.html',
        'scope': './', 'display': 'standalone', 'background_color': '#0B0B0B', 'theme_color': '#141027',
        'icons': [{'src': f'icon-{s}.png', 'sizes': f'{s}x{s}', 'type': 'image/png', 'purpose': 'any'} for s in (192, 512)]
                 + [{'src': f'maskable-{s}.png', 'sizes': f'{s}x{s}', 'type': 'image/png', 'purpose': 'maskable'} for s in (192, 512)]
    }, indent=2) + '\n')
    for relative, metadata in emitted.items():
        metadata['sha256'] = hashlib.sha256((output / relative).read_bytes()).hexdigest()
    manifest = {'source': 'Artboard 1.png', 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'matte': '#141027', 'files': emitted}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    result = generate(args.source, args.output)
    print(f'Generated {len(result["files"])} icon files in {args.output}')
