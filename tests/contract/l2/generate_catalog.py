"""Generate ABI test catalogs from the explicit L2 model and frozen public headers.

Never copy/update the frozen headers here. --check is read-only and runs in CTest.
The semantic model is read as a literal, without executing its view generator.
"""
import ast
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
tree = ast.parse((HERE.parent / "projected-light/generate_views.py").read_text())
models = next(ast.literal_eval(node.value) for node in tree.body
              if isinstance(node, ast.Assign) and any(
                  isinstance(target, ast.Name) and target.id == "models" for target in node.targets))
layouts = {}
evidence = {"PatternId", "GenerationId", "StreamId", "u32"}
for row in models.strip().splitlines():
    name, _, fields = row.split("|")
    members = ["struct_size", "abi_version"]
    for token in fields.split():
        field, kind = token.split(":", 1)
        members.append(field)
        if kind.startswith("[]"):
            members.append(field + "_count")
        if kind.startswith(("?", "!")):
            evidence.add(kind[1:])
    if name == "AcquisitionProgram":
        members.append("terminal_policy")
    layouts["Mantis" + name + "V1"] = members
for kind in sorted(evidence):
    name = {"u32": "UInt32", "u64": "UInt64", "f64": "Float64"}.get(kind, kind)
    layouts["MantisEvidence" + name + "V1"] = ["struct_size", "abi_version", "presence", "value"]

# The few hand-written public tables/views; generated semantic fields above remain
# single-source. These lists intentionally describe the final L2 snapshot only.
manual = """
Host|struct_size abi_version allocate retain release write_map read_map publish log
Attribute|struct_size abi_version name unit scalar_type rank shape stride buffer offset bytes
Packet|struct_size abi_version type_id schema_version sequence device_time_ns clock_id calibration_id calibration_revision coordinate_frame attributes attribute_count
DeviceDescriptor|struct_size abi_version id name capabilities capability_count
Device|struct_size abi_version describe create destroy start next stop
DiscoveredDevice|struct_size abi_version id parent_id name capabilities capability_count metadata_json
Observation|struct_size abi_version packet host_receive_ns sync_group sync_trigger sync_quality metadata_json
FrameSet|struct_size abi_version observation frames frame_count
Acquisition|struct_size abi_version enumerate open destroy start next stop diagnostics
NodeDescriptor|struct_size abi_version id input_type output_type input_schema output_schema deterministic backend
Processor|struct_size abi_version describe process
Exporter|struct_size abi_version input_type input_schema export_file
Plugin|struct_size abi_version id version initialize shutdown query_interface
ComponentList|struct_size abi_version items count
MetadataEntry|struct_size abi_version key value
Metadata|struct_size abi_version items count
DataPacket|struct_size abi_version type header attributes attribute_count frames frame_count
AcquisitionBundle|struct_size abi_version type key published evidence frameset triggers trigger_count member_count
SemanticPacket|struct_size abi_version kind data evidence trigger bundle laser
ContractError|struct_size abi_version category code
ProjectedLimits|struct_size abi_version max_components max_steps max_bundle_members max_cameras max_step_instances max_commands max_events max_bytes max_in_flight_captures max_run_duration_ns max_on_duration_ns max_step_duration_ns max_pending_bundles max_call_timeout_ms watchdog interlock fail_off
ProjectedImageSource|struct_size abi_version stream_id physical_identity width height
ProjectedComponent|struct_size abi_version id parent_id name role participant_kind image_source capabilities capability_count controls control_count participants participant_count trigger_endpoints trigger_endpoint_count emitter_states capture_modes trigger_modes evidence_methods evidence_scopes emitter_state_count capture_mode_count trigger_mode_count evidence_method_count evidence_scope_count pattern pattern_revision lines line_count
ProjectedGraph|struct_size abi_version parent_id components component_count frameset_stream limits
ProgramValidation|struct_size abi_version accepted error diagnostic
ProjectedStatus|struct_size abi_version state run generation step commands_available evidence_available error
AbortOutcome|struct_size abi_version run fenced_generation inhibited stale_work_fenced off_requested emitters emitter_count error
ProjectedLight|struct_size abi_version enumerate open validate prepare start next status abort stop destroy diagnostics
"""
for row in manual.strip().splitlines():
    name, fields = row.split("|")
    layouts["Mantis" + name + "V1"] = fields.split()
layouts["MantisProcessorV2"] = ["struct_size", "abi_version", "describe", "process"]

headers = "\n".join((HERE / "include/mantis" / name).read_text()
                    for name in ("plugin.h", "semantic_views.h", "projected_light.h"))
def strip_comments(text):
    # C comments are whitespace; comment-looking text inside literals is data.
    pattern = r'''("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|/\*.*?\*/|//[^\n]*'''
    return re.sub(pattern, lambda match: match[1] if match[1] is not None else " ", text, flags=re.S)
clean = strip_comments(headers)
structs = dict(re.findall(r"\bstruct\s+(Mantis\w+)\s*\{([^}]+)\}", clean))
assert set(layouts) == set(structs), "Catalog must cover every frozen public struct"
# Verify the explicit catalog against the snapshot, never against live headers.
for name, body in structs.items():
    members = []
    for declaration in body.split(";"):
        if not declaration.strip():
            continue
        callback = re.search(r"\(\*(\w+)\)", declaration)
        if callback:
            members.append(callback[1])
        else:
            for field in declaration.split(","):
                members.append(re.search(r"(\w+)\s*(?:\[[^]]+\])?\s*$", field)[1])
    assert members == layouts[name], (name, "Frozen field catalog differs from snapshot")

aliases = re.findall(r"typedef\s+[^;]+\(\*(Mantis\w+)\)\s*\(", clean)
layout = ["/* Generated from the explicit L2 field catalog; frozen ABI test evidence. */"]
for name, fields in layouts.items():
    layout.append(f"ABI_TYPE({name})")
    layout.extend(f"ABI_FIELD({name}, {field})" for field in fields)
layout.extend(f"ABI_TYPE({name})" for name in aliases)
numeric = sorted(set(re.findall(r"\b(MANTIS_[A-Z0-9_]+)\s*=\s*\d+[uUlL]*", clean)) |
                 set(re.findall(r"^#define\s+(MANTIS_[A-Z0-9_]+)\s+\d+[uUlL]*\s*$", clean, re.M)))
strings = sorted(set(re.findall(r'^#define\s+(MANTIS_[A-Z0-9_]+)\s+"[^"\n]*"', clean, re.M)))
values = ["/* All frozen public numeric constants and string identities. */"]
values.extend(f"ABI_VALUE({name})" for name in numeric)
values.extend(f"ABI_STRING({name})" for name in strings)
for filename, lines in (("layout.inc", layout), ("values.inc", values)):
    text = "\n".join(lines) + "\n"
    path = HERE / filename
    if "--check" in sys.argv:
        assert path.read_text() == text, (filename, "Frozen catalog changed; do not refresh to bless an ABI break")
    else:
        path.write_text(text)
if "--check" in sys.argv:
    # Layout alone cannot detect e.g. same-width scalar reinterpretation, callback
    # signature changes, or a new field squeezed into padding. Preserve the old
    # public declarations too, ignoring comments/formatting and allowing new names.
    root = HERE.parents[2]
    current = "\n".join((root / "sdk/c/include/mantis" / name).read_text()
                        for name in ("plugin.h", "semantic_views.h", "projected_light.h"))
    current = strip_comments(current)
    current_structs = dict(re.findall(r"\bstruct\s+(Mantis\w+)\s*\{([^}]+)\}", current))
    def tokens(declaration):
        return re.findall(r"\w+|[^\s]", declaration)
    for name, body in structs.items():
        assert name in current_structs and tokens(body) == tokens(current_structs[name]), (
            name, "Frozen public field types/declarations changed; add a versioned structure")
    pattern = r"typedef\s+([^;]+?\(\*(Mantis\w+)\)\s*\([^;]+\));"
    current_aliases = {name: declaration for declaration, name in re.findall(pattern, current)}
    for declaration, name in re.findall(pattern, clean):
        assert name in current_aliases and tokens(declaration) == tokens(current_aliases[name]), (
            name, "Frozen callback signature changed; add a versioned interface")
print(f"L2 frozen catalogs: {len(layouts)} structs, {len(aliases)} callback aliases, "
      f"{sum(map(len, layouts.values()))} fields, {len(numeric)} numeric constants, {len(strings)} IDs")
