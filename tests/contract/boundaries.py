"""Architectural include checks, paired with a separate Qt-disabled CI build."""
from pathlib import Path
import re
import sys
root = Path(sys.argv[1])
foundation = ["base", "memory", "schema", "data", "time", "spatial", "calibration", "device-api", "pipeline-api", "artifact-api"]
for module in foundation:
    for path in (root / "src" / module / "include").rglob("*.hpp"):
        text = path.read_text()
        for include in re.findall(r'#include\s*[<"]([^>"]+)', text):
            assert not re.match(r"(Q[A-Z]|Qt|cuda|vulkan|Eigen|opencv|sqlite)", include), (path, include)
        assert "QColor" not in text
        assert not re.search(r'#include\s*[<"]linux/', text), path
for folder in ["apps/studio", "apps/cli", "src/client"]:
    for path in (root / folder).rglob("*"):
        if path.suffix not in (".cpp", ".hpp"): continue
        text = path.read_text()
        assert not re.search(r'#include.*(services|pipeline_runtime|device_runtime|artifact_store|plugin_runtime|jobs\.hpp)', text), path
for path in (root / "plugins").rglob("*.cpp"):
    includes = re.findall(r'#include\s*[<"](mantis/[^>"]+)', path.read_text())
    assert all(x in ("mantis/plugin.h", "mantis/sdk.hpp") for x in includes), path
for path in (root / "sdk/c").rglob("*.h"):
    assert "std::" not in path.read_text()
    assert not re.search(r'#include\s*[<"]linux/', path.read_text()), path

for path in (root / "plugins/first-party/devices").rglob("*.cpp"):
    text = path.read_text()
    assert not re.search(r"\bsystem\s*\(", text), path
    if path.name != "linux.cpp":
        assert not re.search(r'#include\s*[<"]linux/', text), path
print("Foundation, frontend, plugin and Linux media implementation boundaries passed")
