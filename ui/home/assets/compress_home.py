"""Offline packaging only; Pillow is not an application/build dependency."""
from pathlib import Path
from PIL import Image

for path in Path(__file__).parent.glob('*.png'):
    Image.open(path).convert('RGB').save(path.with_suffix('.jpg'), quality=92, optimize=True)
    path.unlink()
