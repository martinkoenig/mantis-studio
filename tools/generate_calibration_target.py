#!/usr/bin/env python3
"""Deterministic vector M8 fixture; not a frozen product board recommendation."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shlex
import subprocess
import sys

VERSION = 1
DEFAULT = dict(target_type="charuco", squares_x=8, squares_y=6,
               nominal_square_size_mm=40.0, nominal_marker_size_mm=25.0,
               dictionary="DICT_6X6_250", layout="black_square_at_origin",
               page_width_mm=420.0, page_height_mm=297.0, margin_mm=20.0)


def validate(s):
    if set(s) != set(DEFAULT):
        raise ValueError("Target specification fields differ from generator schema")
    if s['target_type'] not in ('charuco', 'checkerboard') or s['layout'] != 'black_square_at_origin':
        raise ValueError('Only verified black_square_at_origin layout is supported')
    if s['dictionary'] != 'DICT_6X6_250':
        raise ValueError('Only DICT_6X6_250 is supported by this generator')
    for field in ('squares_x', 'squares_y'):
        if type(s[field]) is not int or not 4 <= s[field] <= 100:
            raise ValueError('Grid dimensions must be integers in [4,100]')
    for field in ('nominal_square_size_mm', 'nominal_marker_size_mm', 'page_width_mm', 'page_height_mm', 'margin_mm'):
        if isinstance(s[field], bool) or not isinstance(s[field], (float, int)) or not math.isfinite(s[field]) or s[field] <= 0:
            raise ValueError(f'{field} must be finite and positive')
    if s['nominal_marker_size_mm'] >= s['nominal_square_size_mm'] or s['squares_x'] * s['squares_y'] > 500:
        raise ValueError('Invalid marker size or dictionary capacity')
    w, h = active_extents(s)
    if w + 2*s['margin_mm'] > s['page_width_mm'] or h + 2*s['margin_mm'] > s['page_height_mm']:
        raise ValueError('Active grid and margins exceed page dimensions')


def active_extents(s):
    return s['squares_x'] * s['nominal_square_size_mm'], s['squares_y'] * s['nominal_square_size_mm']


def backend(tool, mode, value):
    result = subprocess.run([str(tool), mode], input=json.dumps(value, allow_nan=False),
                            text=True, capture_output=True, timeout=3600)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    return json.loads(result.stdout)


def rectangles(s, markers):
    """Physical coordinates in mm; marker IDs increase over white squares row-major."""
    validate(s)
    square, marker = s['nominal_square_size_mm'], s['nominal_marker_size_mm']
    w, h = active_extents(s)
    ox, oy = (s['page_width_mm']-w)/2, (s['page_height_mm']-h)/2
    result, index = [], 0
    for y in range(s['squares_y']):
        for x in range(s['squares_x']):
            if (x+y) % 2 == 0:
                result.append((ox+x*square, oy+y*square, square, square))
            elif s['target_type'] == 'charuco':
                bits = markers[index]
                if len(bits) != 8 or any(len(row) != 8 or set(row)-{'0','1'} for row in bits):
                    raise ValueError('Invalid dictionary marker bits')
                for row in range(8):
                    for col in range(8):
                        if bits[row][col] == '0':
                            result.append((ox+x*square+(square-marker)/2+col*marker/8,
                                           oy+y*square+(square-marker)/2+row*marker/8, marker/8, marker/8))
                index += 1
    return result


def svg_bytes(s, markers):
    rects = rectangles(s, markers)
    fmt = lambda n: format(n, '.17g')
    w, h = fmt(s['page_width_mm']), fmt(s['page_height_mm'])
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}">',
             '<title>Mantis M8 validation fixture v1; print actual size</title>',
             '<metadata>'+json.dumps(s, sort_keys=True, separators=(',', ':'))+'</metadata>',
             f'<rect width="{w}" height="{h}" fill="white"/>', '<g fill="black" shape-rendering="crispEdges">']
    lines += [f'<rect x="{fmt(x)}" y="{fmt(y)}" width="{fmt(rw)}" height="{fmt(rh)}"/>' for x,y,rw,rh in rects]
    lines += ['</g>', '</svg>', '']
    return '\n'.join(lines).encode()


def generate(s, tool, output, command):
    validate(s)
    native = backend(tool, 'markers', s)
    data = svg_bytes(s, native['markers'])
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    w,h = active_extents(s)
    metadata = dict(schema_version=1, generator='mantis-m8-svg', generator_version=VERSION,
                    specification=s, sha256=hashlib.sha256(data).hexdigest(),
                    svg_path=str(output.resolve()), generation_command=command,
                    active_grid_width_mm=w, active_grid_height_mm=h,
                    margins_mm=dict(left=(s['page_width_mm']-w)/2, right=(s['page_width_mm']-w)/2,
                                    top=(s['page_height_mm']-h)/2, bottom=(s['page_height_mm']-h)/2),
                    marker_ids='row-major over white squares, starting at 0', opencv_version=native['opencv_version'])
    output.with_suffix('.json').write_text(json.dumps(metadata, sort_keys=True, indent=2)+'\n')
    output.with_suffix('.sha256').write_text(metadata['sha256']+'  '+output.name+'\n')
    return metadata


def verify_metadata(m, tool):
    if m['schema_version'] != 1 or m['generator_version'] != VERSION or m['generator'] != 'mantis-m8-svg':
        raise ValueError('Unsupported target metadata')
    s = m['specification']; validate(s)
    data = Path(m['svg_path']).read_bytes()
    native = backend(tool, 'markers', s)
    if data != svg_bytes(s, native['markers']) or hashlib.sha256(data).hexdigest() != m['sha256']:
        raise ValueError('Target SVG bytes/specification/hash mismatch')
    if active_extents(s) != (m['active_grid_width_mm'], m['active_grid_height_mm']):
        raise ValueError('Target active extents mismatch')
    return m


def load_metadata(path, tool):
    return verify_metadata(json.loads(Path(path).read_text()), tool)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool', type=Path, default=Path('build/release/bin/mantis-x1-calibration-validation'))
    p.add_argument('--output', type=Path, default=Path('validation-output/target.svg'))
    for k,v in DEFAULT.items():
        p.add_argument('--'+k.replace('_','-'), type=type(v), default=v)
    a=p.parse_args(); s={k:getattr(a,k) for k in DEFAULT}
    m=generate(s,a.tool,a.output,shlex.join([sys.executable,*sys.argv]))
    print(json.dumps(m,indent=2))


if __name__=='__main__': main()
