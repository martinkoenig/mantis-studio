"""Independent stored-f32 Brown5 truth via Decimal contraction, not Newton/J.

The production executable supplies exact binary floating inputs/results. Decimal
conversion is exact; 100-digit working arithmetic and 512 fixed-point steps put
roundoff and residual below 1e-80 for this explicitly bounded synthetic fixture.
No production inverse/normalization/intersection helper computes expected truth.
"""
import decimal
import json
import math
import subprocess
import struct
import sys

D = decimal.Decimal
with decimal.localcontext() as ctx:
    ctx.prec = 100
    run = subprocess.run([sys.argv[1], '--enclosure-data'], capture_output=True, text=True, timeout=25)
    if run.returncode:
        raise RuntimeError(run.stderr)
    cases = json.loads(run.stdout)
    assert cases
    for case in cases:
        if case.get('kind') == 'declared-model':
            # Analytical affine centre witness, independently evaluated from
            # exact fixture geometry/bounds (not reported diagnostic values).
            es, eq, tau, q, vn, eo, ev = D(1), D(0), D(1000), D(1), D(1), D(1), D(0)
            bound = eo + tau*ev + (es + tau*eq)/(q-eq)*(vn+ev)
            radius = D.from_float(float(case['radius']))
            allowance = D.from_float(float(case['allowance']))
            assert case['diagnostic_valid'] & (1 << 7), case
            assert D(1) <= bound <= radius < bound+D('1e-12'), ('declared bound under-report', case)
            assert case['reasons'] == (0 if bound < allowance else 1 << 14), case
            if case['reasons']:
                assert case['position'] == [0, 0, 0], case
            print(f'declared-model: independent_bound_mm={bound} reported_mm={radius} allowance_mm={allowance}')
            continue
        m = {k: D(v) for k, v in case['model'].items()}
        # C++ writes round-trip decimals; recover the exact binary64 values.
        m = {k: D.from_float(float(v)) for k, v in m.items()}
        u, v = (D.from_float(float(x)) for x in case['pixel'])
        xd, yd = (u-m['cx'])/m['fx'], (v-m['cy'])/m['fy']
        x, y = xd, yd
        def perturb(x, y):
            r = x*x+y*y
            a = m['k1']*r+m['k2']*r*r+m['k3']*r*r*r
            return (x*a+2*m['p1']*x*y+m['p2']*(r+2*x*x),
                    y*a+m['p1']*(r+2*y*y)+2*m['p2']*x*y)
        # Independent contraction bound from absolute row sums of the
        # perturbation derivative on the specified square, checked here.
        a, b = (D.from_float(float(z)) for z in case['domain'])
        rho = a*a+b*b
        radial = abs(m['k1'])*rho+abs(m['k2'])*rho**2+abs(m['k3'])*rho**3
        deriv = abs(m['k1'])+2*abs(m['k2'])*rho+3*abs(m['k3'])*rho**2
        tang = max(2*abs(m['p1'])*b+6*abs(m['p2'])*a,
                   6*abs(m['p1'])*b+2*abs(m['p2'])*a)+2*abs(m['p1'])*a+2*abs(m['p2'])*b
        contraction = radial+2*rho*deriv+tang
        assert contraction < D('.5')
        for _ in range(512):
            px, py = perturb(x, y)
            x, y = xd-px, yd-py
            assert abs(x) <= a and abs(y) <= b
        px, py = perturb(x, y)
        residual = ((x+px-xd)**2+(y+py-yd)**2).sqrt()
        assert residual/(1-contraction) < D('1e-85')
        # Synthetic transform is exactly optical->rig (x,z,-y), origin(-50,0,0),
        # plane rig Y=1000: homogeneous rays avoid production normalization.
        truth = [-50+1000*x, D(1000), -1000*y]
        actual = [D.from_float(float(z)) for z in case['position']]
        error = sum((p-q)**2 for p,q in zip(actual, truth)).sqrt()
        radius = D.from_float(float(case['radius']))
        allowance = D.from_float(float(case['allowance']))
        assert case['reasons'] == 0, case
        assert error <= radius <= allowance, (case['name'], error, radius, allowance)
        if error != 0:
            assert error+D('1e-80') <= radius, (case['name'], 'oracle margin')
        if all(m[k] == 0 for k in ('k1', 'k2', 'k3', 'p1', 'p2')):
            # IEEE f32 adjacent spacing derived by bit stepping independently
            # of C++ nextafter/certify/interval mapping. The plane map is affine:
            # p=(-50+1000*(u-cx)/fx,1000,-1000*(v-cy)/fy).
            def half_spacing(value):
                bits = struct.unpack('<I', struct.pack('<f', float(value)))[0]
                neighbours = [D.from_float(struct.unpack('<f', struct.pack('<I', bits+s))[0])
                              for s in (-1, 1)]
                return max(abs(n-value) for n in neighbours)/2
            hu, hv = half_spacing(u), half_spacing(v)
            within = abs(x)+hu/m['fx'] <= a and abs(y)+hv/m['fy'] <= b
            if not within:
                assert not case['diagnostic_valid'] & (1 << 8), ('cell crosses domain', case)
                assert case['quantization_radius'] == 0, case
            else:
                assert case['diagnostic_valid'] & (1 << 8), case
                corners = [[-50+1000*((u+su*hu-m['cx'])/m['fx']), D(1000),
                            -1000*((v+sv*hv-m['cy'])/m['fy'])]
                           for su in (-1, 1) for sv in (-1, 1)]
                witness = max(sum((p-q)**2 for p, q in zip(actual, corner)).sqrt() for corner in corners)
                quantization = D.from_float(float(case['quantization_radius']))
                assert witness > 0 and witness <= quantization, ('rounding-cell under-report', case['name'], witness, quantization)
                print(f"{case['name']}: independent_cell_corner_mm={witness} reported_quantization_mm={quantization}")
        print(f"{case['name']}: actual_L2_error_mm={error} reported_radius_mm={radius} policy_mm={allowance}")
