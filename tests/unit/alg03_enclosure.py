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
        print(f"{case['name']}: actual_L2_error_mm={error} reported_radius_mm={radius} policy_mm={allowance}")
