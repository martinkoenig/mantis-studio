"""Generate explicit, frozen C views and field conversions. No serialization codec.

The catalog is an L2 snapshot, deliberately independent of parsing future domain
headers. Changing it is an ABI change requiring review, not routine regeneration.
"""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[3]
# name, C++ type, fields: name:type; [] bounded array; ? typed Evidence;
# ! required construction-only optional; ~ optional trigger (absence is not evidence).
models = '''
DataType|schema::DataTypeId|name:text version:u32
Hash|Hash|algorithm:text hex:text
Version|SemanticVersion|major:u32 minor:u32 patch:u32
CalibrationReference|calibration::Reference|id:id schema_version:u32 revision:u64
ClockDomain|time::ClockDomain|id:id name:text
CoordinateFrame|spatial::CoordinateFrame|id:id name:text
SyncGroup|time::SyncGroup|id:id trigger:u64
ClockMapping|time::ClockMapping|source:ClockDomain target:ClockDomain scale:f64 offset_ns:f64 uncertainty_ns:f64
StepInstance|data::StepInstance|run_id:RunId repetition_index:u64 step_index:u32
StreamIdentity|data::StreamIdentity|id:StreamId generation:GenerationId
SourceFrameKey|data::SourceFrameKey|camera:ComponentId stream:StreamIdentity native_sequence:u64
FrameSetKey|data::FrameSetKey|run_id:RunId stream:StreamIdentity sequence:u64
BundleKey|data::BundleKey|run_id:RunId sequence:BundleSequence
EvidenceKey|data::EvidenceKey|run_id:RunId ordinal:CausalOrdinal
TriggerKey|data::TriggerKey|run_id:RunId source:ComponentId controller_generation:GenerationId sequence:EventSequence
NativeTriggerIdentity|data::NativeTriggerIdentity|controller:ComponentId generation:GenerationId value:u64
ContentReference|data::ContentReference|id:id type:DataType hash:?Hash revision:u64
ProgramReference|data::ProgramReference|id:ProgramId hash:?Hash content:?ContentReference
ExactCalibrationReference|data::ExactCalibrationReference|calibration:CalibrationReference content:?ContentReference
ClockIdentity|data::ClockIdentity|domain:ClockDomain generation:GenerationId
SemanticTimestamp|data::SemanticTimestamp|nanoseconds:i64 clock:ClockIdentity
RuntimeTimestamp|data::RuntimeTimestamp|time:MonotonicTimestamp clock:ClockIdentity
TimeInterval|data::TimeInterval|start:SemanticTimestamp end:SemanticTimestamp
ClockMappingEvidence|data::ClockMappingEvidence|mapping:ClockMapping source_generation:GenerationId target_generation:GenerationId reference:ContentReference
EvidenceSource|data::EvidenceSource|source:ComponentId method:EvidenceMethod reference:?ContentReference
Acknowledgement|data::Acknowledgement|request:RequestId stage:!AcknowledgementStage result:!AcknowledgementResult evidence:EvidenceSource time:?SemanticTimestamp scope:EvidenceScope
EmitterCommand|data::EmitterCommand|request:RequestId target:ComponentId state:EmitterState dispatched:RuntimeTimestamp
StateObservation|data::StateObservation|state:!EmitterState scope:EvidenceScope evidence:EvidenceSource time:?SemanticTimestamp coverage:?TimeInterval
ExposureEvidence|data::ExposureEvidence|requested_duration:?Duration startup_readback_duration:?Duration integration_duration:?Duration interval:?TimeInterval evidence:EvidenceSource uncertainty_ns:?f64
ExposureAssociation|data::ExposureAssociation|frame:SourceFrameKey trigger:TriggerKey method:AssociationMethod evidence:EvidenceSource native_trigger:?NativeTriggerIdentity
SyncEvidence|data::SyncEvidence|group:SyncGroup quality:SyncQuality hardware_association:?ExposureAssociation
CameraFrameEvidence|data::CameraFrameEvidence|frame:SourceFrameKey camera_role:text width:u32 height:u32 source_timestamp:?SemanticTimestamp host_received:?RuntimeTimestamp timestamp_meaning:?TimestampMeaning exposure:?ExposureEvidence sync:?SyncEvidence camera_calibration:?ExactCalibrationReference rig_calibration:?ExactCalibrationReference original_calibration:?ExactCalibrationReference
ExposureEffectiveState|data::ExposureEffectiveState|frame:SourceFrameKey state:!EmitterState scope:EvidenceScope evidence:EvidenceSource coverage:TimeInterval
CameraEffectiveState|data::CameraEffectiveState|frame:SourceFrameKey state:?ExposureEffectiveState
EmitterEvidence|data::EmitterEvidence|emitter:ComponentId commanded:?EmitterCommand acknowledged:?Acknowledgement observed:?StateObservation exposure_effective:[]CameraEffectiveState
CameraParticipant|data::CameraParticipant|component:ComponentId stream:StreamId role:text
Participants|data::Participants|cameras:[]CameraParticipant emitters:[]ComponentId controllers:[]ComponentId
TriggerIntent|data::TriggerIntent|controller:ComponentId request:RequestId endpoints:[]ComponentId
CaptureIntent|data::CaptureIntent|mode:!CaptureMode cameras:[]ComponentId trigger:~TriggerIntent
EmitterIntent|data::EmitterIntent|emitter:ComponentId state:EmitterState
AcquisitionStep|data::AcquisitionStep|index:u32 label:text emitters:[]EmitterIntent capture:CaptureIntent evidence_requirement:!EvidenceRequirement required_scope:EvidenceScope settle:Duration max_duration:Duration
RunBounds|data::RunBounds|max_duration:Duration max_on_duration:Duration max_step_instances:u64 max_commands:u64 max_events:u64 max_bytes:u64 max_in_flight_captures:u64
AcquisitionProgram|data::AcquisitionProgram|type:DataType identity:ProgramReference participants:Participants steps:[]AcquisitionStep repetitions:u64 bounds:RunBounds
TriggerEvent|data::TriggerEvent|type:DataType key:TriggerKey step:StepInstance request:RequestId native_trigger:?NativeTriggerIdentity kind:!TriggerKind acknowledgement:?Acknowledgement evidence:EvidenceSource device_time:?SemanticTimestamp host_received:?RuntimeTimestamp host_dispatched:?RuntimeTimestamp uncertainty_ns:?f64 clock_mapping:?ClockMappingEvidence intended_endpoints:[]ComponentId actual_endpoints:?ComponentList requested_exposure:?SourceFrameKey exposure_association:?ExposureAssociation
ImplementationIdentity|data::ImplementationIdentity|implementation:id version:Version build:text configuration:?ContentReference
LossAccounting|data::LossAccounting|kind:LossKind source:ComponentId count:?u64 reason:AcquisitionReason frame:?SourceFrameKey request:?RequestId
UnresolvedRequest|data::UnresolvedRequest|request:RequestId target:ComponentId reason:AcquisitionReason
AcquisitionEvidence|data::AcquisitionEvidence|type:DataType key:EvidenceKey program:ProgramReference step:?StepInstance causal_predecessors:[]EvidenceKey participants:Participants implementations:[]ImplementationIdentity emitters:[]EmitterEvidence frameset:?FrameSetKey frames:[]CameraFrameEvidence triggers:[]TriggerKey clock_mappings:[]ClockMappingEvidence rig_calibration:?ExactCalibrationReference disposition:!AcquisitionDisposition reason:AcquisitionReason unresolved_requests:[]UnresolvedRequest losses:[]LossAccounting diagnostic:text
EmitterPatternIdentity|data::EmitterPatternIdentity|emitter:ComponentId pattern:PatternId revision:u64
LineIdentity|data::LineIdentity|emitter:ComponentId pattern:PatternId pattern_revision:u64 local_line:LineId
ProgramCorrelation|data::ProgramCorrelation|program:ProgramReference step:StepInstance
PreprocessingTransform|data::PreprocessingTransform|original_from_processed:matrix9 reference:?ContentReference
ObservationKey|data::ObservationKey|run_id:?RunId producer_stream:ProducerStreamId producer_generation:GenerationId sequence:ObservationSequence
LaserObservationContext|data::LaserObservationContext|source:CameraFrameEvidence frameset:?FrameSetKey bundle:?BundleKey raw_input:?ContentReference optical_frame:CoordinateFrame preprocessing:?PreprocessingTransform requested_emitters:[]ComponentId emitter_evidence:[]EmitterEvidence emitter_patterns:[]EmitterPatternIdentity correlation:?ProgramCorrelation acquisition_evidence:?EvidenceKey triggers:[]TriggerKey clock_mappings:[]ClockMappingEvidence producer:ImplementationIdentity parameters:?ContentReference exact_inputs:[]ContentReference origin:!ObservationOrigin producer_completed:?RuntimeTimestamp packet_quality_flags:u32
LaserObservation|data::LaserObservation|type:DataType key:ObservationKey context:LaserObservationContext disposition:!ObservationDisposition sample_count:u64 emitter_dictionary:[]ComponentId line_dictionary:[]LineIdentity attributes:[]Attribute confidence_interpretation:?ImplementationIdentity diagnostic:text
PacketHeader|data::Header|sequence:SequenceNumber timestamp:DeviceTimestamp received:MonotonicTimestamp sync:SyncGroup sync_quality:SyncQuality calibration:CalibrationReference frame:CoordinateFrame metadata:Metadata
DeviceTimestamp|time::DeviceTimestamp|nanoseconds:i64 domain:ClockDomain
'''
model = {}
for row in models.strip().splitlines():
    name, cpp, fields = row.split('|')
    model[name] = (cpp, [f.split(':',1) for f in fields.split()])
ids = 'ProgramId RunId GenerationId ComponentId StreamId ProducerStreamId RequestId PatternId LineId'.split()
seqs = 'EventSequence BundleSequence ObservationSequence CausalOrdinal'.split()+['SequenceNumber']
enums = {
'EvidenceMethod':'software_dispatch controller_report register_readback electrical_readback optical_sensor validated_executor camera_metadata software_association imported',
'EvidenceScope':'controller_register electrical_enable optical_emission',
'EmitterState':'off on', 'AcknowledgementStage':'acceptance completion', 'AcknowledgementResult':'success rejected failed',
'TimestampMeaning':'exposure_start exposure_end driver_delivery device_event',
'AssociationMethod':'native_trigger validated_executor software_correspondence imported',
'SyncQuality':'unknown software hardware', 'CaptureMode':'none free_running hardware_trigger',
'EvidenceRequirement':'commanded_only controller_acknowledged exposure_effective',
'TriggerKind':'requested acknowledged_accepted acknowledged_completed rejected observed timed_out cancelled',
'AcquisitionDisposition':'startup captured control_only completed failed stopped cancelled',
'AcquisitionReason':'none timeout rejected device_failure transport_failure evidence_missing contradictory_evidence resource_limit user_stop user_cancel cleanup_failure',
'LossKind':'command trigger frame exposure bundle record excluded_frame',
'ObservationOrigin':'real synthetic imported', 'ObservationDisposition':'success extractor_failed extractor_unavailable'}
primitives = {'u32':'uint32_t','u64':'uint64_t','i64':'int64_t','f64':'double','text':'const char *','id':'const char *','Duration':'int64_t','MonotonicTimestamp':'int64_t'}
def ct(t):
    if t in ids: return 'const char *'
    if t in seqs: return 'uint64_t'
    if t in enums: return 'uint32_t'
    return primitives.get(t, 'Mantis'+t+'V1')
def cpp(t):
    if t in model: return model[t][0]
    if t in enums: return 'time::SyncQuality' if t=='SyncQuality' else 'data::TriggerEvent::Kind' if t=='TriggerKind' else 'data::'+t
    if t in ids or t in seqs[:-1]: return 'data::'+t
    return {'SequenceNumber':'time::SequenceNumber','id':'Id','text':'std::string','Duration':'data::Duration','MonotonicTimestamp':'time::MonotonicTimestamp','Attribute':'data::Attribute','Metadata':'data::Metadata','ComponentList':'std::vector<data::ComponentId>'}.get(t,ct(t))
def en(t): return 'MantisEvidence'+{'u32':'UInt32','u64':'UInt64','f64':'Float64'}.get(t,t)+'V1'
def snake(s): return re.sub(r'(?<!^)(?=[A-Z])','_',s).upper()
evidence = sorted({"PatternId", "GenerationId", "u32"} | {t[1:] for _,fields in model.values() for _,t in fields if t.startswith('?') or t.startswith('!')})
h=['/* L2 frozen typed in-memory views. Generated by tests/contract/projected-light/generate_views.py. */', '#ifndef MANTIS_SEMANTIC_VIEWS_H', '#define MANTIS_SEMANTIC_VIEWS_H', '#include <mantis/plugin.h>', '#ifdef __cplusplus\nextern "C" {\n#endif', '''#define MANTIS_ACQUISITION_PROGRAM "org.mantis.AcquisitionProgram"
#define MANTIS_ACQUISITION_BUNDLE "org.mantis.AcquisitionBundle"
#define MANTIS_ACQUISITION_EVIDENCE "org.mantis.AcquisitionEvidence"
#define MANTIS_TRIGGER_EVENT "org.mantis.TriggerEvent"
#define MANTIS_LASER_OBSERVATION "org.mantis.LaserObservation"
#define MANTIS_MAX_BUNDLE_MEMBERS 64u
#define MANTIS_MAX_PROGRAM_STEPS 256u
#define MANTIS_MAX_PARTICIPANTS 64u
#define MANTIS_MAX_SEMANTIC_ENTRIES 4096u
/* Strings: NUL terminated UTF-8, <=1024 bytes (identities <=256).
 * Evidence: established iff value != NULL; unknown/unavailable require NULL.
 * Arrays: NULL iff count == 0; counts bounded by L1 and the total view budget.
 * Every view is borrowed synchronously. Retained bulk storage uses MantisBuffer.
 * No recursive graphs: data children are only FrameSet -> ImageFrame.
 */
enum { MANTIS_PRESENCE_ESTABLISHED = 0u, MANTIS_PRESENCE_UNKNOWN = 1u,
       MANTIS_PRESENCE_UNAVAILABLE = 2u };
''']
for t,vals in enums.items():
    h.append('enum { '+', '.join('MANTIS_'+snake(t)+'_'+v.upper()+' = '+str(i)+'u' for i,v in enumerate(vals.split()))+' };')
alltypes=list(model)+['ComponentList','MetadataEntry','Metadata','DataPacket','AcquisitionBundle','SemanticPacket']
for n in alltypes: h.append(f'typedef struct Mantis{n}V1 Mantis{n}V1;')
for t in evidence:
    h.append(f'typedef struct {en(t)} {{\n    uint32_t struct_size, abi_version, presence;\n    {ct(t)} const *value;\n}} {en(t)};')
h += ['struct MantisComponentListV1 {\n    uint32_t struct_size, abi_version;\n    const char *const *items;\n    uint32_t count;\n};', 'struct MantisMetadataEntryV1 {\n    uint32_t struct_size, abi_version;\n    const char *key, *value;\n};','struct MantisMetadataV1 {\n    uint32_t struct_size, abi_version;\n    const MantisMetadataEntryV1 *items;\n    uint32_t count;\n};']
# topologically sort by embedded fields (evidence pointers permit incomplete types).
order=[]
def visit(n):
    if n in order: return
    for _,t in model[n][1]:
        if t in model: visit(t)
    order.append(n)
for n in model: visit(n)
for n in order:
    fields=model[n][1]
    h.append('struct Mantis'+n+'V1 {\n    uint32_t struct_size, abi_version;')
    for f,t in fields:
        if t.startswith('[]'): h.append(f'    {ct(t[2:])} const *{f};\n    uint32_t {f}_count;')
        elif t.startswith('?') or t.startswith('!'): h.append(f'    {en(t[1:])} {f};')
        elif t.startswith('~'): h.append(f'    const {ct(t[1:])} *{f};')
        elif t=='matrix9': h.append(f'    double {f}[9];')
        else: h.append(f'    {ct(t.removeprefix("!"))} {f};')
    if n=='AcquisitionProgram': h.append('    uint32_t terminal_policy; /* must be MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF */')
    h.append('};')
h += ['''enum { MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF = 0u };
struct MantisDataPacketV1 {
    uint32_t struct_size, abi_version;
    MantisDataTypeV1 type;
    MantisPacketHeaderV1 header;
    const MantisAttributeV1 *attributes;
    uint32_t attribute_count;
    const MantisDataPacketV1 *frames;
    uint32_t frame_count;
};
struct MantisAcquisitionBundleV1 {
    uint32_t struct_size, abi_version;
    MantisDataTypeV1 type;
    MantisBundleKeyV1 key;
    MantisRuntimeTimestampV1 published;
    MantisAcquisitionEvidenceV1 evidence;
    const MantisDataPacketV1 *frameset;
    const MantisTriggerEventV1 *triggers;
    uint32_t trigger_count, member_count; /* exactly 1 + !!frameset + trigger_count <=64 */
};
enum { MANTIS_SEMANTIC_DATA = 0u, MANTIS_SEMANTIC_EVIDENCE = 1u,
       MANTIS_SEMANTIC_TRIGGER = 2u, MANTIS_SEMANTIC_BUNDLE = 3u,
       MANTIS_SEMANTIC_LASER = 4u };
/* Exactly one matching typed pointer, all other pointers NULL. No children here. */
struct MantisSemanticPacketV1 {
    uint32_t struct_size, abi_version, kind;
    const MantisDataPacketV1 *data;
    const MantisAcquisitionEvidenceV1 *evidence;
    const MantisTriggerEventV1 *trigger;
    const MantisAcquisitionBundleV1 *bundle;
    const MantisLaserObservationV1 *laser;
};
typedef int (*MantisSemanticEmitV1)(void *, const MantisSemanticPacketV1 *);
#ifdef __cplusplus
}
#endif
#endif''']
(ROOT/'sdk/c/include/mantis/semantic_views.h').write_text('\n'.join(h)+'\n')
# explicit converters using shared bounds/lifetime helpers in semantic_views.cpp.
out=['// Generated field conversions; no serialization or runtime policy.']
for n in model:
    out += [f'{model[n][0]} decode(const Mantis{n}V1 &, Reader &);',f'Mantis{n}V1 encode(const {model[n][0]} &, Writer &);']
for n in order:
    cppname, fields=model[n]
    out += [f'{cppname} decode(const Mantis{n}V1 &v, Reader &r) {{', '    r.prefix(v);', f'    {cppname} o;']
    for f,t in fields:
        dest=f'o.{f}'; src=f'v.{f}'
        base=t.lstrip('!?~').removeprefix('[]')
        call=f'decode_value<{cpp(base)}>({src}, r)'
        if t.startswith('?'): call=f'decode_evidence<{cpp(base)}>({src}, r)'
        elif t.startswith('!'): call=f'decode_required<{cpp(base)}>({src}, r)'
        elif t.startswith('[]'):
            maximum = {('AcquisitionProgram','steps'):256, ('Participants','cameras'):16,
                ('Participants','emitters'):64, ('Participants','controllers'):64,
                ('AcquisitionStep','emitters'):64, ('CaptureIntent','cameras'):16,
                ('TriggerIntent','endpoints'):64, ('EmitterEvidence','exposure_effective'):16,
                ('AcquisitionEvidence','implementations'):64, ('AcquisitionEvidence','emitters'):64,
                ('AcquisitionEvidence','frames'):16, ('AcquisitionEvidence','clock_mappings'):64,
                ('LaserObservationContext','requested_emitters'):64, ('LaserObservationContext','emitter_evidence'):64,
                ('LaserObservationContext','clock_mappings'):64, ('LaserObservation','attributes'):128,
                ('TriggerEvent','intended_endpoints'):64}.get((n,f),4096)
            call=f'decode_array<{cpp(base)}>({src}, v.{f}_count, r, {maximum})'
        elif t.startswith('~'): call=f'decode_optional<{cpp(base)}>({src}, r)'
        elif t=='matrix9': call=f'decode_matrix({src})'
        out.append(f'    {dest} = {call};')
    if n=='AcquisitionProgram': out.append('    require(v.terminal_policy == MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF, "Invalid terminal policy");')
    out += ['    return o;','}', f'Mantis{n}V1 encode(const {cppname} &v, Writer &w) {{',f'    Mantis{n}V1 o{{}};', '    init(o);']
    for f,t in fields:
        dest=f'o.{f}'; src=f'v.{f}'; base=t.lstrip('!?~').removeprefix('[]')
        if t.startswith('?'): call=f'encode_evidence<{en(base)}>({src}, w)'
        elif t.startswith('[]'):
            out.append(f'    o.{f}_count = count({src}.size());')
            call=f'encode_array<{ct(base)}>({src}, w)'
        elif t.startswith('~'): call=f'encode_optional<{ct(base)}>({src}, w)'
        elif t.startswith('!'): call=f'encode_required<{en(base)}>({src}, w)'
        elif t=='matrix9':
            out.append(f'    std::copy({src}.begin(), {src}.end(), {dest});'); continue
        else: call=f'encode_value({src}, w)'
        out.append(f'    {dest} = {call};')
    if n=='AcquisitionProgram': out.append('    o.terminal_policy = MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF;')
    out += ['    return o;','}']
(ROOT/'src/plugin-runtime/semantic_fields_forward.inc').write_text('\n'.join(out[1:1+2*len(model)])+'\n')
(ROOT/'src/plugin-runtime/semantic_fields.inc').write_text('\n'.join([out[0]]+out[1+2*len(model):])+'\n')
# Compile-time map used for basic enum validation (independent of domain validator).
e=['// Generated explicit enum ranges.']
for n,vals in enums.items():
    e.append(f'template<> constexpr uint32_t enum_max<{cpp(n)}> = {len(vals.split())-1}u;')
    for member in vals.split():
        e.append(f'static_assert(static_cast<uint32_t>({cpp(n)}::{member}) == MANTIS_{snake(n)}_{member.upper()});')
(ROOT/'src/plugin-runtime/semantic_enums.inc').write_text('\n'.join(e)+'\n')
