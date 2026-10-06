"""Versioned M8 characterization math. No optical acceptance envelope exists in M8a."""
import itertools
import math
import statistics

SCHEMA_VERSION = 1
STATES = ('acquisition_integrity', 'calibration_pipeline_execution', 'activation_binding',
          'deterministic_replay', 'characterization', 'final_m8_acceptance')
COEFFICIENTS = ('fx','fy','cx','cy','k1','k2','p1','p2','k3')


def require(condition, reason):
    if not condition: raise ValueError(reason)


def finite(value):
    if isinstance(value, dict):
        for v in value.values(): finite(v)
    elif isinstance(value, (list, tuple)):
        for v in value: finite(v)
    elif isinstance(value, float): require(math.isfinite(value), 'NaN/Inf evidence rejected')


def summary(mode):
    require(mode in ('real','fixture'), 'Unknown evidence mode')
    return dict(schema_version=1, evidence_mode=mode, software_harness='NOT ESTABLISHED',
                **{k:'NOT ESTABLISHED' for k in STATES}, failures=[])


def validate_summary(s):
    finite(s)
    require(s['schema_version']==1 and s['evidence_mode'] in ('real','fixture'), 'Invalid report schema/mode')
    require(s['software_harness'] in ('PASS','FAIL','NOT ESTABLISHED'),'Invalid software harness state')
    require(s['final_m8_acceptance']=='NOT ESTABLISHED', 'M8a cannot establish final acceptance')
    for k in STATES: require(s[k] in ('NOT ESTABLISHED','PASS','FAIL','COMPLETE'), 'Unknown report state')
    if s['evidence_mode']=='fixture': require(s['acquisition_integrity']!='PASS', 'Fixture cannot establish real acquisition')
    if s['failures']: require(s['software_harness']=='FAIL', 'Failure must propagate')


def fail(s, stage, reason):
    s['software_harness']='FAIL'
    if stage in STATES: s[stage]='FAIL'
    s['failures'].append(dict(stage=stage,reason=str(reason)))
    s['final_m8_acceptance']='NOT ESTABLISHED'


def rotation(r):
    require(len(r)==9, 'Rotation must have 9 row-major values')
    finite(r)
    for i in range(3):
        for j in range(3):
            value=math.fsum(r[3*i+k]*r[3*j+k] for k in range(3))
            require(abs(value-(i==j))<=1e-8,'Invalid rotation orthogonality')
    det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6])
    require(abs(det-1)<=1e-8,'Invalid rotation determinant')
    return r


def rotation_delta(a,b):
    rotation(a); rotation(b)
    delta=[math.fsum(b[3*i+k]*a[3*j+k] for k in range(3)) for i in range(3) for j in range(3)]
    # atan2 retains accuracy near identity and pi; clamp trace cosine.
    cosine=max(-1.0,min(1.0,(delta[0]+delta[4]+delta[8]-1)/2))
    sine=math.hypot(delta[7]-delta[5],delta[2]-delta[6],delta[3]-delta[1])/2
    angle=math.atan2(sine,cosine)
    return dict(angle_rad=angle,angle_deg=math.degrees(angle))


def stats(values):
    require(len(values)>=2,'Sample statistics need at least two sessions'); finite(values)
    out=dict(min=min(values),max=max(values),mean=statistics.mean(values),sample_stddev=statistics.stdev(values))
    finite(out); return out


def solutions(s):
    return {r:s['evidence'][r]['payload']['solution'] for r in ('left','right','rig')}


def comparison(a,b):
    sa,sb=solutions(a),solutions(b)
    out=dict(session_a=a['name'],session_b=b['name'],delta_convention='B minus A',classification='CHARACTERIZATION',cameras={})
    for role in ('left','right'):
        x,y=sa[role]['final_model'],sb[role]['final_model']; finite(x); finite(y)
        require(x['fx']>0 and x['fy']>0,'Invalid focal lengths')
        d={k+'_delta':y[k]-x[k] for k in COEFFICIENTS}
        for k in ('fx','fy'):
            d[k+'_absolute_delta_px']=abs(d[k+'_delta'])
            d[k+'_relative_delta']=d[k+'_delta']/x[k]
            d[k+'_absolute_relative_delta']=abs(d[k+'_relative_delta'])
        d['cx_delta_px']=d.pop('cx_delta'); d['cy_delta_px']=d.pop('cy_delta')
        out['cameras'][role]=d
    x,y=sa['rig'],sb['rig']
    tx,ty=x['final_model']['T_right_from_left'],y['final_model']['T_right_from_left']
    require(len(tx)==len(ty)==3,'Translation must have three millimeter values')
    finite(tx); finite(ty)
    out['rig']=dict(baseline_delta_mm=y['rig']['baseline_mm']-x['rig']['baseline_mm'],
                    translation_difference_norm_mm=math.dist(tx,ty),
                    relative_rotation_difference=rotation_delta(x['final_model']['R_right_from_left'],y['final_model']['R_right_from_left']))
    finite(out); return out


def repeatability(sessions):
    sessions=sorted(sessions,key=lambda s:s['name'])
    require(len(sessions)>=3 and len({s['name'] for s in sessions})==len(sessions),'At least three distinct sessions required')
    used=set()
    for s in sessions:
        require(s['evidence']['rig'] is not None,'ChArUco rig required')
        sources={(s['project'],r['id']) for r in s['evidence']['raw_captures']}
        require(sources and not used & sources,'Sessions share captures'); used |= sources
        finite(s['evidence'])
    all_stats={}
    for role in ('left','right'):
        all_stats[role]={k:stats([solutions(s)[role]['final_model'][k] for s in sessions]) for k in COEFFICIENTS}
    all_stats['rig']={k:stats([solutions(s)['rig']['rig'][k] for s in sessions]) for k in ('baseline_mm','relative_rotation_angle_rad')}
    for axis in range(3): all_stats['rig']['translation_'+('x','y','z')[axis]+'_mm']=stats([solutions(s)['rig']['final_model']['T_right_from_left'][axis] for s in sessions])
    return dict(schema_version=1,classification='CHARACTERIZATION',units=dict(intrinsics='pixels',distortion='dimensionless',translation='millimeters',rotation='radians'),
                session_order=[s['name'] for s in sessions],pairs=[comparison(a,b) for a,b in itertools.combinations(sessions,2)],statistics=all_stats)


def activation_report(value):
    require(value['schema_version']==1 and value['status']=='PASS' and value['framesets']>0,'Invalid binding report')
    r=value['binding']; require(r['id']['value'] and r['schema_version']==1 and r['revision']>0,'Invalid exact revision')
    require(value['raw']['state']=='FINALIZED' and value['raw']['schema_version']==2,'Invalid bound capture')
    finite(value); return value
