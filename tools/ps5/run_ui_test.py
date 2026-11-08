#!/usr/bin/env python3
import io
import os
import sys
import time
from ftplib import FTP, error_perm, error_reply
from PIL import Image

PS5_HOST = os.environ.get("PS5_HOST", "192.168.15.17")
FTP_PORT = int(os.environ.get("FTP_PORT", "2121"))
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ps5_test_shots"
AUTOPLAY_CMD = sys.argv[2] if len(sys.argv) > 2 else "AUTOTEST 25 streammenutest"

os.makedirs(OUT_DIR, exist_ok=True)

print(f"Connecting to PS5 FTP at {PS5_HOST}:{FTP_PORT}...")
ftp = FTP()
ftp.connect(PS5_HOST, FTP_PORT, timeout=15)
ftp.login()

ppm_names = [
    "stream_menu.ppm",
    "stream_menu_muted.ppm",
    "stream_menu_controls.ppm",
    "stream_menu_usb.ppm",
    "stream_menu_sticks.ppm",
    "stream_menu_triggers.ppm",
    "stream_menu_gyro.ppm",
    "settings_gyro_row.ppm",
    "settings_gyro_dialog.ppm",
    "settings.ppm",
    "ui.ppm"
]

for d in ["/data/psbox", "/data/homebrew/PPSA99810"]:
    try:
        ftp.cwd(d)
        for name in ppm_names + ["autoplay.txt", "xcloud.log"]:
            try:
                ftp.sendcmd(f"DELE {name}")
            except (error_perm, error_reply):
                pass
    except (error_perm, error_reply):
        pass

# Upload autoplay.txt
data = (AUTOPLAY_CMD + "\n").encode()
for d in ["/data/psbox", "/data/homebrew/PPSA99810"]:
    try:
        ftp.cwd(d)
        ftp.storbinary("STOR autoplay.txt", io.BytesIO(data))
        print(f"Uploaded autoplay.txt to {d}: {AUTOPLAY_CMD}")
    except (error_perm, error_reply) as e:
        print(f"Warning storing autoplay.txt to {d}: {e}")

ftp.quit()

print("Launching PPSA99810...")
os.system(f"PS5_HOST={PS5_HOST} python3 '/home/notebook-storm/Documentos/PS5 Ports/PS5_Vulkan/tools/ps5_console.py' launch PPSA99810")

deadline = time.time() + 45
finished = False
log_content = ""

while time.time() < deadline:
    time.sleep(3)
    try:
        f = FTP()
        f.connect(PS5_HOST, FTP_PORT, timeout=10)
        f.login()
        f.cwd("/data/psbox")
        buf = io.BytesIO()
        f.retrbinary("RETR xcloud.log", buf.write)
        f.quit()
        log_content = buf.getvalue().decode(errors="replace")
        if "AUTOPLAY END" in log_content:
            print("Detected AUTOPLAY END in log!")
            finished = True
            break
    except Exception as e:
        pass

print(f"Finished: {finished}, downloading artifacts...")
f = FTP()
f.connect(PS5_HOST, FTP_PORT, timeout=15)
f.login()

# Download xcloud.log
try:
    f.cwd("/data/psbox")
    with open(os.path.join(OUT_DIR, "xcloud.log"), "wb") as lf:
        f.retrbinary("RETR xcloud.log", lf.write)
    print("Downloaded xcloud.log")
except Exception as e:
    print(f"Failed to download xcloud.log: {e}")

# Download PPMs
for d in ["/data/psbox"]:
    try:
        f.cwd(d)
        for name in ppm_names:
            try:
                buf = io.BytesIO()
                f.retrbinary(f"RETR {name}", buf.write)
                data = buf.getvalue()
                if len(data) > 0:
                    out_path = os.path.join(OUT_DIR, name)
                    with open(out_path, "wb") as pf:
                        pf.write(data)
                    print(f"Downloaded {name} from {d} ({len(data)} bytes)")
            except (error_perm, error_reply):
                pass
    except (error_perm, error_reply):
        pass

# Clean up autoplay.txt
for d in ["/data/psbox", "/data/homebrew/PPSA99810"]:
    try:
        f.cwd(d)
        ftp_cmd = f.sendcmd("DELE autoplay.txt")
        print(f"Deleted autoplay.txt from {d}")
    except (error_perm, error_reply):
        pass

f.quit()

# Kill application
print("Killing PPSA99810...")
os.system(f"PS5_HOST={PS5_HOST} python3 '/home/notebook-storm/Documentos/PS5 Ports/PS5_Vulkan/tools/ps5_console.py' kill PPSA99810")

# Convert PPM to PNG
for name in os.listdir(OUT_DIR):
    if name.endswith(".ppm"):
        ppm_path = os.path.join(OUT_DIR, name)
        png_path = os.path.join(OUT_DIR, name[:-4] + ".png")
        try:
            with Image.open(ppm_path) as im:
                im.save(png_path, "PNG")
            print(f"Converted {name} -> {os.path.basename(png_path)}")
        except Exception as e:
            print(f"Error converting {name}: {e}")

print("Done! Artifacts in", OUT_DIR)
