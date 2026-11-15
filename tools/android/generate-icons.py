#!/usr/bin/env python3
import os
import sys
from PIL import Image

def generate_icons():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
    src_icon = os.path.join(root, "ps5/sce_sys/icon0.png")
    if not os.path.exists(src_icon):
        print(f"Error: source icon {src_icon} not found", file=sys.stderr)
        sys.exit(1)

    res_dir = os.path.join(root, "android/app/src/main/res")
    img_orig = Image.open(src_icon).convert("RGB")
    w_orig, h_orig = img_orig.size

    # Extract anti-aliased white logo with alpha channel
    logo_rgba = Image.new("RGBA", (w_orig, h_orig), (0, 0, 0, 0))
    for y in range(h_orig):
        bg = img_orig.getpixel((0, y))
        for x in range(w_orig):
            p = img_orig.getpixel((x, y))
            if p != bg:
                a = max(0, min(255, int(round((p[1] - bg[1]) * 255.0 / (255.0 - bg[1])))))
                if a > 0:
                    logo_rgba.putpixel((x, y), (255, 255, 255, a))

    logo_cropped = logo_rgba.crop(logo_rgba.getbbox())

    legacy_densities = {
        "mipmap-mdpi": 48,
        "mipmap-hdpi": 72,
        "mipmap-xhdpi": 96,
        "mipmap-xxhdpi": 144,
        "mipmap-xxxhdpi": 192,
    }

    adaptive_densities = {
        "mipmap-mdpi": 108,
        "mipmap-hdpi": 162,
        "mipmap-xhdpi": 216,
        "mipmap-xxhdpi": 324,
        "mipmap-xxxhdpi": 432,
    }

    # Generate legacy icons
    for folder, size in legacy_densities.items():
        out_dir = os.path.join(res_dir, folder)
        os.makedirs(out_dir, exist_ok=True)

        # Legacy square icon
        resized = img_orig.resize((size, size), Image.Resampling.LANCZOS)
        out_path = os.path.join(out_dir, "ic_launcher.png")
        resized.save(out_path, "PNG")

        # Legacy round icon
        round_path = os.path.join(out_dir, "ic_launcher_round.png")
        resized.save(round_path, "PNG")
        print(f"Generated legacy icons in {folder} ({size}x{size})")

    # Generate adaptive foreground icons (108dp with full edge-to-edge gradient + centered safe logo)
    for folder, size in adaptive_densities.items():
        out_dir = os.path.join(res_dir, folder)
        os.makedirs(out_dir, exist_ok=True)

        canvas = Image.new("RGBA", (size, size))
        for y in range(size):
            t = y / (size - 1) if size > 1 else 0.0
            r = int(round(36 + (8 - 36) * t))
            g = int(round(160 + (82 - 160) * t))
            b = int(round(36 + (8 - 36) * t))
            for x in range(size):
                canvas.putpixel((x, y), (r, g, b, 255))

        # Safe area: logo height is ~54% of canvas (stays within 66dp safe circle)
        target_h = int(size * 0.54)
        target_w = int(target_h * logo_cropped.width / logo_cropped.height)
        logo_resized = logo_cropped.resize((target_w, target_h), Image.Resampling.LANCZOS)

        ox = (size - target_w) // 2
        oy = (size - target_h) // 2
        canvas.paste(logo_resized, (ox, oy), logo_resized)

        fg_path = os.path.join(out_dir, "ic_launcher_foreground.png")
        canvas.save(fg_path, "PNG")
        print(f"Generated adaptive foreground in {folder} ({size}x{size})")

    # Generate drawable/ic_launcher_background.xml
    drawable_dir = os.path.join(res_dir, "drawable")
    os.makedirs(drawable_dir, exist_ok=True)
    bg_xml_path = os.path.join(drawable_dir, "ic_launcher_background.xml")
    with open(bg_xml_path, "w", encoding="utf-8") as f:
        f.write('''<?xml version="1.0" encoding="utf-8"?>
<shape xmlns:android="http://schemas.android.com/apk/res/android">
    <gradient
        android:angle="270"
        android:startColor="#24a024"
        android:endColor="#085208"
        android:type="linear" />
</shape>
''')
    print(f"Generated {bg_xml_path}")

    # Generate mipmap-anydpi-v26/ic_launcher.xml and ic_launcher_round.xml
    v26_dir = os.path.join(res_dir, "mipmap-anydpi-v26")
    os.makedirs(v26_dir, exist_ok=True)
    adaptive_xml = '''<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@drawable/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
'''
    for name in ["ic_launcher.xml", "ic_launcher_round.xml"]:
        xml_path = os.path.join(v26_dir, name)
        with open(xml_path, "w", encoding="utf-8") as f:
            f.write(adaptive_xml)
        print(f"Generated {xml_path}")

    # Banner for Android TV Leanback (320x180)
    banner = Image.new("RGBA", (320, 180), (18, 18, 24, 255))
    icon_aspect = img_orig.width / img_orig.height
    target_h = 140
    target_w = int(target_h * icon_aspect)
    icon_resized = img_orig.resize((target_w, target_h), Image.Resampling.LANCZOS)
    offset_x = (320 - target_w) // 2
    offset_y = (180 - target_h) // 2
    banner.paste(icon_resized, (offset_x, offset_y))
    banner_path = os.path.join(drawable_dir, "banner.png")
    banner.save(banner_path, "PNG")
    print(f"Generated {banner_path} (320x180)")

if __name__ == "__main__":
    generate_icons()
