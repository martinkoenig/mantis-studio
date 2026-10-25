"""Offline packaging only; Pillow is not an application/build dependency."""
from pathlib import Path
from PIL import Image

for path in sorted(Path(__file__).parent.glob('*.png')):
    with Image.open(path) as image:
        if path.stem == 'hero':
            image.convert('RGB').save(path.with_suffix('.jpg'), quality=92, optimize=True)
            path.unlink()
        else:
            if image.mode != 'RGBA' or image.getchannel('A').getextrema() != (0, 255):
                raise ValueError(f'{path.name}: expected genuine transparent foreground')
            bounds = image.getchannel('A').getbbox()
            if not bounds or bounds[0] < 1 or bounds[1] < 1 or bounds[2] >= image.width or bounds[3] >= image.height:
                raise ValueError(f'{path.name}: cropped subject; fix Blender framing')
            # Discard unused camera space, retaining a common transparent border.
            # This is lossless, does not resize geometry, and is idempotent.
            subject = image.crop(bounds)
            foreground = Image.new('RGBA', (subject.width + 16, subject.height + 16))
            foreground.paste(subject, (8, 8))
            foreground.save(path, optimize=True, compress_level=9)
