"""Run from the repository root with PYTHONPATH=build/debug/python."""
import sys
import mantis

studio = mantis.connect()
scanner = studio.devices.list()[0]
capture = studio.capture.start(scanner)
try:
    artifact = studio.pipeline.run(capture=capture, recipe="example").wait()
finally:
    capture.stop()
output = sys.argv[1] if len(sys.argv) > 1 else "python-output.ply"
studio.export(artifact, output)
print(f"Exported {artifact.id} to {output}")
