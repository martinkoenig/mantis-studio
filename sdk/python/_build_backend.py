"""Source-matched PEP 517 builds. No generated source is trusted as input.

The repository schema is authoritative. An sdist carries the same bytes and a
reproducible integrity manifest so that rebuilding needs no repository access.
Hashes detect stale/corrupt inputs, not a malicious party rewriting all inputs.
"""
from contextlib import contextmanager
import gzip
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
COMPILER_DISTRIBUTION = '21.11'
COMPILER_VERSION = b'libprotoc 3.21.11'
BUILD_RUNTIME = '3.20.0'
SOURCE_FILES = ('pyproject.toml', '_build_backend.py', 'README.md', 'LICENSE',
                'MANIFEST.in', 'mantis/__init__.py', 'mantis/calibration.py')


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _run(args):
    result = subprocess.run(args, check=True, capture_output=True, timeout=60)
    return result.stdout


def _distribution_version(name):
    # Query the standard filesystem finder directly. Some frontend in-tree
    # finders discard Context.name and return the project's metadata for every
    # dependency query; never accept that unrelated identity.
    context = importlib.metadata.DistributionFinder.Context(name=name)
    distributions = importlib.metadata.MetadataPathFinder.find_distributions(context)
    try:
        return next(iter(distributions)).version
    except StopIteration:
        raise importlib.metadata.PackageNotFoundError(name) from None


@contextmanager
def _stage():
    # Never consult PATH for protoc, reuse a generated module, or write into the
    # checkout. Each hook owns and retires its complete scratch directory.
    compiler_distribution = _distribution_version('protoc-wheel-0')
    if compiler_distribution != COMPILER_DISTRIBUTION:
        raise RuntimeError('Expected compiler distribution protoc-wheel-0==' +
                           COMPILER_DISTRIBUTION + '; found ' + compiler_distribution)
    import google.protobuf
    if (_distribution_version('protobuf') != BUILD_RUNTIME or
            google.protobuf.__version__ != BUILD_RUNTIME):
        raise RuntimeError('Expected build protobuf==' + BUILD_RUNTIME)
    import setuptools
    import wheel
    if (_distribution_version('setuptools') != '75.8.2' or setuptools.__version__ != '75.8.2' or
            _distribution_version('wheel') != '0.45.1' or wheel.__version__ != '0.45.1'):
        raise RuntimeError('Build-tool identity mismatch (setuptools/wheel)')
    from protoc import PROTOC_EXE
    compiler_version = _run([PROTOC_EXE, '--version']).strip()
    if compiler_version != COMPILER_VERSION:
        raise RuntimeError('Compiler version mismatch: expected ' + repr(COMPILER_VERSION) +
                           '; executed ' + repr(compiler_version) +
                           ' from protoc-wheel-0==' + compiler_distribution)
    # Execution diagnostics belong in frontend logs, never in portable artifacts.
    # This also identifies the actual executable in pip's isolated overlay.
    print('MANTIS_COMPILER_IDENTITY ' + json.dumps({
        'distribution': {'name': 'protoc-wheel-0', 'version': compiler_distribution},
        'version': compiler_version.decode('ascii'), 'argv': [str(PROTOC_EXE), '--version'],
        'exit': 0, 'binary_sha256': _sha(Path(PROTOC_EXE).read_bytes())}, sort_keys=True),
        flush=True)
    canonical = ROOT.parent.parent / 'protocol/protobuf/mantis.proto'
    bundled = ROOT / '_schema/mantis.proto'
    manifest = ROOT / '_schema/provenance.json'
    is_sdist = manifest.exists() or bundled.exists()
    if is_sdist:
        schema = bundled.read_bytes()
        expected = json.loads(manifest.read_text(encoding='utf-8'))
        if canonical.exists() and canonical.read_bytes() != schema:
            raise RuntimeError('Bundled schema differs from canonical schema')
    else:
        schema = canonical.read_bytes()  # Missing canonical schema is fatal.
    with tempfile.TemporaryDirectory(prefix='mantis-pep517-') as temporary:
        stage = Path(temporary)
        for name in SOURCE_FILES:
            destination = stage / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        (stage / '_schema').mkdir()
        (stage / '_schema/mantis.proto').write_bytes(schema)
        _run([PROTOC_EXE, '-I' + str(stage / '_schema'),
              '--python_out=' + str(stage / 'mantis'),
              '--descriptor_set_out=' + str(stage / 'descriptor.pb'),
              str(stage / '_schema/mantis.proto')])
        generated = (stage / 'mantis/mantis_pb2.py').read_bytes()
        # Execute generated code in the frontend's declared build environment.
        # A nested python -I would discard pip's isolated dependency overlay.
        namespace = runpy.run_path(str(stage / 'mantis/mantis_pb2.py'))
        if namespace['Request'].FromString(b'\x08\x01').protocol_version != 1:
            raise RuntimeError('Generated module/runtime validation failed')
        provenance = {'format': 2, 'schema': 'protocol/protobuf/mantis.proto',
                      'schema_sha256': _sha(schema),
                      'compiler_distribution': {'name': 'protoc-wheel-0',
                                                'version': compiler_distribution},
                      'compiler_version': compiler_version.decode('ascii'),
                      'build_protobuf': BUILD_RUNTIME,
                      'generated_sha256': _sha(generated),
                      'descriptor_set_sha256': _sha((stage / 'descriptor.pb').read_bytes())}
        if is_sdist and expected != provenance:
            raise RuntimeError('Schema/generated provenance mismatch')
        previous = ROOT / 'mantis/mantis_pb2.py'
        if is_sdist or previous.exists():
            if previous.read_bytes() != generated:
                raise RuntimeError('Missing or stale generated module')
        (stage / '_schema/provenance.json').write_text(
            json.dumps(provenance, sort_keys=True, indent=2) + '\n', encoding='utf-8')
        # Include provenance and canonical bytes in installed wheels, too.
        shutil.copyfile(stage / '_schema/provenance.json', stage / 'mantis/_provenance.json')
        shutil.copyfile(stage / '_schema/mantis.proto', stage / 'mantis/mantis.proto')
        old_cwd = Path.cwd()
        try:
            os.chdir(stage)
            yield stage
        finally:
            os.chdir(old_cwd)


def get_requires_for_build_wheel(config_settings=None):
    return []  # All dependencies are fixed in pyproject.toml.


def get_requires_for_build_sdist(config_settings=None):
    return []


def prepare_metadata_for_build_wheel(metadata_directory, config_settings=None):
    from setuptools import build_meta
    destination = str(Path(metadata_directory).resolve())
    with _stage():
        return build_meta.prepare_metadata_for_build_wheel(destination, config_settings)


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    from setuptools import build_meta
    destination = Path(wheel_directory).resolve()
    metadata = str(Path(metadata_directory).resolve()) if metadata_directory else None
    with _stage() as stage:
        output = stage / 'output'
        output.mkdir()
        name = build_meta.build_wheel(str(output), config_settings, metadata)
        destination.mkdir(parents=True, exist_ok=True)
        # Copy to a private sibling then replace, never expose partial artifacts.
        with tempfile.TemporaryDirectory(dir=destination, prefix='.mantis-') as tmp:
            shutil.copyfile(output / name, Path(tmp) / name)
            os.replace(Path(tmp) / name, destination / name)
        return name


def build_sdist(sdist_directory, config_settings=None):
    from setuptools import build_meta
    destination = Path(sdist_directory).resolve()
    with _stage() as stage:
        output = stage / 'output'
        output.mkdir()
        name = build_meta.build_sdist(str(output), config_settings)
        # setuptools' gzip header and tar mtimes otherwise depend on wall time.
        epoch = int(os.environ.get('SOURCE_DATE_EPOCH', '0'))
        destination.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination, prefix='.mantis-') as tmp:
            target = Path(tmp) / name
            with tarfile.open(output / name, 'r:gz') as source, target.open('wb') as raw:
                with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=epoch) as gz:
                    with tarfile.open(fileobj=gz, mode='w', format=tarfile.PAX_FORMAT) as archive:
                        for member in sorted(source.getmembers(), key=lambda item: item.name):
                            # egg-info is disposable setuptools cache, not source.
                            # In-tree frontend metadata finders can mistake it for
                            # dependency metadata; regenerate it in the next hook.
                            if any(part.endswith('.egg-info') for part in Path(member.name).parts):
                                continue
                            member.mtime = epoch
                            member.uid = member.gid = 0
                            member.uname = member.gname = ''
                            member.mode = 0o755 if member.isdir() else 0o644
                            member.pax_headers = {}
                            archive.addfile(member, source.extractfile(member) if member.isfile() else None)
            os.replace(target, destination / name)
        return name
