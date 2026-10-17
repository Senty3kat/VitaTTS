"""Рисует экраны хост-превью в /tmp/shots/*.png: python3 shot.py (из папки host, после сборки preview.bin)"""
import subprocess, os
from PIL import Image
os.makedirs("/tmp/shots", exist_ok=True)
for name in ("main", "ru", "long", "empty", "press", "speak", "syl", "syl_auto", "syl_wait"):
    subprocess.run(["./preview.bin", "/tmp/frame.raw", name], check=True)
    Image.frombuffer("RGBA", (960, 544), open("/tmp/frame.raw", "rb").read(), "raw", "RGBA", 0, 1).convert("RGB").save(f"/tmp/shots/{name}.png")
print("ok")
