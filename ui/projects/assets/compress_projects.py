"""Reuse accepted M1 lossless RGBA packaging, scoped to Projects assets only."""
from pathlib import Path
source = Path(__file__).resolve().parents[2] / 'home/assets/compress_home.py'
exec(compile(source.read_text(), str(source), 'exec'))
