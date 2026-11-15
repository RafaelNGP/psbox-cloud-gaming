#!/usr/bin/env python3
import os
import sys

EXCLUDED_DIRS = {"media_showcase", "__pycache__"}


def generate_asset_list(out_path=None):
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
    assets_dir = os.path.join(root, "assets")
    list_file = os.path.abspath(out_path) if out_path else os.path.join(assets_dir, "asset-list.txt")

    entries = []
    for dirpath, dirnames, filenames in os.walk(assets_dir):
        dirnames[:] = sorted(d for d in dirnames if d not in EXCLUDED_DIRS and not d.startswith("."))
        for f in filenames:
            if f == "asset-list.txt" or f.startswith("."):
                continue
            full_path = os.path.join(dirpath, f)
            rel_path = os.path.relpath(full_path, assets_dir).replace(os.sep, "/")
            entries.append(rel_path)

    entries.sort()
    os.makedirs(os.path.dirname(list_file), exist_ok=True)
    with open(list_file, "w", encoding="utf-8", newline="\n") as fp:
        for entry in entries:
            fp.write(entry + "\n")
    print(f"Generated {list_file} ({len(entries)} assets)")


if __name__ == "__main__":
    generate_asset_list(sys.argv[1] if len(sys.argv) > 1 else None)
