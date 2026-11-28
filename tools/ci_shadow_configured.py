"""Bounded, read-only configured diagnostics; no command from input is executed.

Caller pins bytes and asserts source context. This is not an authenticated build
receipt, a preprocessor, a complete dependency scanner or a selection engine.
"""
from pathlib import Path
import re
import sys

import ci_shadow_inventory as reader
import ci_shadow_policy as policy

MAX_TOTAL_BYTES = 16_000_000
MAX_FILES = 8192
MAX_RECORDS = 16384
PROTO = 'protocol/protobuf/mantis.proto'
PROTO_OUTPUTS = ('mantis.pb.cc', 'mantis.pb.h')
PROTO_OWNER = 'mantis-wire'
PROTO_RECEIPT = ('build', 'mantis-protobuf-ninja-query.json')
PYTHON_PROTO_OUTPUT = 'python/mantis/mantis_pb2.py'
PYTHON_PROTO_OWNER = 'mantis-python'
PYTHON_PROTO_RECEIPT = ('build', 'mantis-protobuf-python-ninja-query.json')
PYTHON_PROTO_INPUT_SCHEMA = 'ci110-configured-inputs-v3'
ALG03_INPUT_SCHEMA = 'ci110-configured-inputs-v4'
ALG03_SCRIPT = 'tests/unit/alg03_enclosure.py'
ALG03_TEST = 'alg03-enclosure'
ALG03_NUMERIC = 'mantis-alg03-numeric-tests'
CALIBRATION_INPUT_SCHEMA = 'ci110-configured-inputs-v5'
DIRECT_TEST_INPUT_SCHEMAS = (ALG03_INPUT_SCHEMA, CALIBRATION_INPUT_SCHEMA)
CALIBRATION_SOURCE = 'sdk/python/mantis/calibration.py'
CALIBRATION_INIT = 'sdk/python/mantis/__init__.py'
CALIBRATION_SCRIPT = 'tests/unit/python_calibration.py'
CALIBRATION_TEST = 'python-calibration'
CALIBRATION_COPY_RECEIPT = ('build', 'mantis-python-calibration-copy-trace.json')
# Reviewed source bytes: the script inserts argv[1]/python first, imports
# mantis and calls mantis.Calibration; __init__ re-exports .calibration.Calibration.
# Changes to either route require reinspection, not token/substring matching.
CALIBRATION_IMPORT_DIGESTS = {
    CALIBRATION_SCRIPT: '4378480203792b8461a5f69b0d8fc5494faf3002e2278779af6285ad9655367e',
    CALIBRATION_INIT: '5931e132b1d40f7e7394f286cb3cc538e64907eea0dfaf976faa2c0af714692a',
}


class InvalidInput(ValueError):
    """Only locally defined stable diagnostic codes may cross the boundary."""


class BoundedSet(set):
    def add(self, value):
        check(value in self or len(self) < MAX_RECORDS, 'configured-edge-or-unknown-limit')
        super().add(value)


def check(ok, reason):
    if not ok:
        raise InvalidInput(reason)


def label(value):
    return isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9_.:@+ -]{1,180}', value) is not None


def relative(value):
    return policy.path_ok(value) and ':' not in value


def anchor(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


class Inputs:
    def __init__(self, manifest, expected):
        policy.keys(manifest, 'schema source roots configuration generator index ctest files', 'configured-input-fields')
        check(manifest['schema'] in ('ci110-configured-inputs-v1', 'ci110-configured-inputs-v2',
                                     PYTHON_PROTO_INPUT_SCHEMA, *DIRECT_TEST_INPUT_SCHEMAS),
              'configured-input-version')
        policy.identity(manifest['source'])
        check(manifest['source'] == expected, 'configured-source-identity-mismatch')
        check(label(manifest['configuration']) and label(manifest['generator']), 'configured-context')
        policy.keys(manifest['roots'], 'source build', 'configured-roots')
        self.roots = {}
        for name, value in manifest['roots'].items():
            check(isinstance(value, str) and Path(value).is_absolute(), 'configured-root-path')
            root = Path(value)
            check(root == root.resolve() and root.is_dir(), 'configured-root-not-canonical')
            if manifest['schema'] in DIRECT_TEST_INPUT_SCHEMAS:
                check(value == str(root), 'configured-root-not-canonical')
            self.roots[name] = root
        check(self.roots['source'] != self.roots['build'] and
              self.roots['build'] not in self.roots['source'].parents, 'configured-root-overlap')
        rows = manifest['files']
        check(isinstance(rows, list) and 0 < len(rows) <= MAX_FILES, 'configured-file-limit')
        self.data = {}
        self.pins = []
        total = 0
        for row in rows:
            policy.keys(row, 'root path sha256', 'configured-file-fields')
            check(row['root'] in self.roots and relative(row['path']) and anchor(row['sha256']),
                  'configured-file-path-or-anchor')
            key = (row['root'], row['path'])
            check(key not in self.data, 'configured-duplicate-file')
            path = self.roots[row['root']] / row['path']
            # Reject symlink ancestry as well as leaf links. Cooperative snapshot
            # checks are not an OS sandbox or protection against a hostile host.
            check(path == path.resolve(), 'configured-symlink-path')
            raw = reader.read_regular(path)
            total += len(raw)
            check(total <= MAX_TOTAL_BYTES, 'configured-total-byte-limit')
            check(policy.digest(raw) == row['sha256'], 'configured-file-digest-mismatch')
            self.data[key] = raw
            self.pins.append(dict(row))
        self.pins.sort(key=lambda row: (row['root'], row['path']))
        self.manifest = manifest

    def json(self, path):
        check(relative(path), 'configured-reference-path')
        check(('build', path) in self.data, 'configured-unpinned-reference')
        result = policy.strict_json(self.data['build', path])
        check(isinstance(result, dict), 'configured-document-object')
        return result

    def locate(self, value, default='source'):
        check(isinstance(value, str) and len(value) <= 4096, 'configured-metadata-path')
        path = Path(value)
        if not path.is_absolute():
            check(relative(value), 'configured-relative-path')
            return default, value
        for name in ('build', 'source'):
            try:
                part = path.relative_to(self.roots[name]).as_posix()
            except ValueError:
                continue
            if part == '.':
                return name, ''
            check(relative(part), 'configured-relative-path')
            return name, part
        return None


def records(value):
    check(isinstance(value, list) and len(value) <= MAX_RECORDS and
          all(isinstance(row, dict) for row in value), 'configured-records')
    return value


def version(document, kind, major):
    check(document.get('kind') == kind and isinstance(document.get('version'), dict) and
          type(document['version'].get('major')) is int and document['version']['major'] == major,
          'configured-unsupported-document-version')


def check_proto_query(text, output, source):
    """Recognize only the observed single-output Ninja query shape, not Ninja syntax."""
    unsupported = 'protobuf-ninja-evidence-unsupported'
    check(isinstance(text, str) and len(text) <= 8192, unsupported)
    lines = text.split('\n')
    check(lines[:2] == [output + ':', '  input: CUSTOM_COMMAND'] and
          lines[-3:] == ['  outputs:', '    cmake_object_order_depends_target_' + PROTO_OWNER, '']
          and len(lines) >= 5, unsupported)
    deps = lines[2:-3]
    # Only explicit inputs are admitted. In particular |, || and |@ are not
    # interchangeable with the observed explicit dependency.
    check(all(re.fullmatch(r'    [!-~][ -~]*', d) and d[4] != '|' and not d.endswith(' ')
              for d in deps), unsupported)
    check(len(deps) == len(set(deps)), 'protobuf-ninja-rule-ambiguous')
    check('    ' + str(source) in deps, 'protobuf-ninja-rule-input-missing')


def protobuf_owner(i, generator, configs, cache, cache_path, rows, target_docs):
    """Bind the one caller-captured rule family to a compiled codemodel owner."""
    unsupported = 'protobuf-ninja-evidence-unsupported'
    mismatch = 'protobuf-ninja-evidence-context-mismatch'
    mapping = 'protobuf-ninja-codemodel-mismatch'
    check(i.manifest['configuration'] == 'Debug' and generator.get('name') == 'Ninja' and
          generator.get('multiConfig') is False and len(configs) == 1, unsupported)
    raw = i.data.get(PROTO_RECEIPT)
    if raw is None:
        raise InvalidInput('protobuf-ninja-evidence-missing')
    check(len(raw) <= 32_768, unsupported)
    context_keys = ('source', 'roots', 'configuration', 'generator')
    try:
        receipt = policy.strict_json(raw)
        policy.keys(receipt, 'schema sha256 queries ' + ' '.join(context_keys), unsupported)
    except ValueError:
        raise InvalidInput(unsupported) from None
    check(receipt['schema'] == 'ci110-mantis-protobuf-ninja-query-v1', unsupported)
    check(all(receipt[k] == i.manifest[k] for k in context_keys), mismatch)
    expected_cache = {'CMAKE_BUILD_TYPE': 'Debug', 'CMAKE_GENERATOR': 'Ninja', 'MANTIS_BUILD_STUDIO': 'OFF',
                      'CMAKE_HOME_DIRECTORY': str(i.roots['source']), 'CMAKE_CACHEFILE_DIR': str(i.roots['build'])}
    entries = [(r['name'], r.get('value')) for r in records(cache.get('entries'))
               if r.get('name') in tuple(expected_cache)]
    check(len(entries) == len(expected_cache) and dict(entries) == expected_cache, mismatch)

    owner = next((r for r in rows if r['name'] == PROTO_OWNER), None)
    if owner is None:
        raise InvalidInput(mapping)
    owner_id = owner['id']
    reply = str(Path(i.manifest['index']).parent)
    pins = {'index': ('build', i.manifest['index']), 'cache': ('build', cache_path),
            'owner': ('build', reply + '/' + owner['jsonFile']), PROTO: ('source', PROTO)}
    pins.update({p: ('build', p) for p in ('build.ninja', 'CMakeFiles/rules.ninja')})
    check(all(p in i.data for p in pins.values()), 'protobuf-ninja-evidence-missing')
    check(receipt['sha256'] == {name: policy.digest(i.data[p]) for name, p in pins.items()}, mismatch)
    queries = receipt['queries']
    check(isinstance(queries, dict) and set(queries) == set(PROTO_OUTPUTS), unsupported)

    for output in PROTO_OUTPUTS:
        check_proto_query(queries[output], output, i.roots['source'] / PROTO)
        absolute = i.roots['build'] / output
        # File API source paths inside the source tree may be source-relative,
        # including in-tree build outputs. Compare only these exact spellings.
        paths = {str(absolute)}
        if absolute.is_relative_to(i.roots['source']):
            paths.add(absolute.relative_to(i.roots['source']).as_posix())
        matches = [(tid, idx, item) for tid, doc in target_docs.items()
                   for idx, item in enumerate(doc.get('sources', [])) if item['path'] in paths]
        check(len(matches) <= 1, 'protobuf-ninja-rule-ambiguous')
        check(matches and matches[0][0] == owner_id and matches[0][2].get('isGenerated') is True, mapping)
        _, idx, item = matches[0]
        if output == 'mantis.pb.cc':
            group = item.get('compileGroupIndex')
            groups = target_docs[owner_id].get('compileGroups', [])
            check(type(group) is int and 0 <= group < len(groups) and groups[group].get('language') == 'CXX', mapping)
            indices = groups[group].get('sourceIndexes')
            check(isinstance(indices, list) and all(type(n) is int for n in indices) and indices.count(idx) == 1,
                  mapping)
        else:
            check('compileGroupIndex' not in item, mapping)
    return owner_id


def python_protobuf_owner(i, generator, configs, cache, cache_path, rows, target_docs):
    """Recognize only the fixed Python output and its generated UTILITY rules."""
    unsupported = 'protobuf-python-ninja-evidence-unsupported'
    missing = 'protobuf-python-ninja-evidence-missing'
    mismatch = 'protobuf-python-ninja-evidence-context-mismatch'
    mapping = 'protobuf-python-ninja-codemodel-mismatch'
    ambiguous = 'protobuf-python-ninja-rule-ambiguous'
    check(i.manifest['configuration'] == 'Debug' and generator.get('name') == 'Ninja' and
          generator.get('multiConfig') is False and len(configs) == 1, unsupported)
    raw = i.data.get(PYTHON_PROTO_RECEIPT)
    check(raw is not None, missing)
    check(len(raw) <= 32_768, unsupported)
    context_keys = ('source', 'roots', 'configuration', 'generator')
    try:
        receipt = policy.strict_json(raw)
        policy.keys(receipt, 'schema sha256 query ' + ' '.join(context_keys), unsupported)
    except (ValueError, RecursionError):
        raise InvalidInput(unsupported) from None
    check(receipt['schema'] == 'ci110-mantis-protobuf-python-ninja-query-v1', unsupported)
    check(all(receipt[k] == i.manifest[k] for k in context_keys), mismatch)
    expected_cache = {'CMAKE_BUILD_TYPE': 'Debug', 'CMAKE_GENERATOR': 'Ninja', 'MANTIS_BUILD_STUDIO': 'OFF',
                      'CMAKE_HOME_DIRECTORY': str(i.roots['source']), 'CMAKE_CACHEFILE_DIR': str(i.roots['build'])}
    entries = [(r['name'], r.get('value')) for r in records(cache.get('entries'))
               if r.get('name') in tuple(expected_cache)]
    check(len(entries) == len(expected_cache) and dict(entries) == expected_cache, mismatch)

    owner = next((r for r in rows if r['name'] == PYTHON_PROTO_OWNER), None)
    if owner is None:
        raise InvalidInput(mapping)
    owner_id = owner['id']
    doc = target_docs[owner_id]
    check(doc['type'] == 'UTILITY' and not doc.get('compileGroups') and not doc.get('artifacts'), mapping)
    reply = str(Path(i.manifest['index']).parent)
    pins = {'index': ('build', i.manifest['index']), 'cache': ('build', cache_path),
            'owner': ('build', reply + '/' + owner['jsonFile']), PROTO: ('source', PROTO),
            'build.ninja': ('build', 'build.ninja'), 'CMakeFiles/rules.ninja': ('build', 'CMakeFiles/rules.ninja')}
    check(all(p in i.data for p in pins.values()), missing)
    check(receipt['sha256'] == {name: policy.digest(i.data[p]) for name, p in pins.items()}, mismatch)

    utility_node = 'CMakeFiles/' + PYTHON_PROTO_OWNER
    query = receipt['query']
    check(isinstance(query, str) and len(query) <= 8192, unsupported)
    lines = query.split('\n')
    check(len(lines) >= 7 and lines[:2] == [PYTHON_PROTO_OUTPUT + ':', '  input: CUSTOM_COMMAND'] and
          lines[-5] == '  outputs:' and lines[-1] == '', unsupported)
    outputs = lines[-4:-1]
    check(len(set(outputs)) == len(outputs), ambiguous)
    check(set(outputs) == {'    ' + output for output in
                           (PYTHON_PROTO_OWNER, utility_node, str(i.roots['build'] / utility_node))}, unsupported)
    deps = lines[2:-5]
    check(all(re.fullmatch(r'    [!-~][ -~]*', d) and d[4] != '|' and not d.endswith(' ')
              for d in deps), unsupported)
    check(len(deps) == len(set(deps)), ambiguous)
    check('    ' + str(i.roots['source'] / PROTO) in deps, 'protobuf-python-ninja-rule-input-missing')

    # File API lists .rule markers, not the .py output as a compiled source.
    # A second association (even a different spelling of the same path) is ambiguous.
    for rule in (utility_node + '.rule', PYTHON_PROTO_OUTPUT + '.rule'):
        absolute = i.roots['build'] / rule
        paths = {str(absolute)}
        if absolute.is_relative_to(i.roots['source']):
            paths.add(absolute.relative_to(i.roots['source']).as_posix())
        matches = [(tid, item) for tid, target in target_docs.items()
                   for item in target.get('sources', []) if item['path'] in paths]
        check(len(matches) <= 1, ambiguous)
        check(matches and matches[0][0] == owner_id and matches[0][1].get('isGenerated') is True and
              'compileGroupIndex' not in matches[0][1], mapping)
    return owner_id


def configured_interpreter(i, cmake_version, generator, configs, cache, test):
    """Return the configured interpreter spelling for the supported context only."""
    unsupported = test + '-context-unsupported'
    check(i.manifest['configuration'] == 'Debug' and generator.get('name') == 'Ninja' and
          generator.get('multiConfig') is False and len(configs) == 1, unsupported)
    # Cache/codemodel do not normally report the target OS. Read only the pinned
    # generator output for this exact CMake version; recognize the native Linux
    # template without interpreting CMake code, includes or a toolchain file.
    system = i.data.get(('build', f'CMakeFiles/{cmake_version}/CMakeSystem.cmake'))
    if system is None:  # optional scoped evidence
        raise InvalidInput(test + '-system-pin-missing')
    native = len(system) <= 4096 and re.fullmatch(
        rb'set\(CMAKE_HOST_SYSTEM "Linux-(?P<release>[A-Za-z0-9._+~-]+)"\)\n'
        rb'set\(CMAKE_HOST_SYSTEM_NAME "Linux"\)\n'
        rb'set\(CMAKE_HOST_SYSTEM_VERSION "(?P=release)"\)\n'
        rb'set\(CMAKE_HOST_SYSTEM_PROCESSOR "(?P<arch>[A-Za-z0-9_]+)"\)\n\n\n\n'
        rb'set\(CMAKE_SYSTEM "Linux-(?P=release)"\)\n'
        rb'set\(CMAKE_SYSTEM_NAME "Linux"\)\n'
        rb'set\(CMAKE_SYSTEM_VERSION "(?P=release)"\)\n'
        rb'set\(CMAKE_SYSTEM_PROCESSOR "(?P=arch)"\)\n\n'
        rb'set\(CMAKE_CROSSCOMPILING "FALSE"\)\n\nset\(CMAKE_SYSTEM_LOADED 1\)\n', system)
    if not native:
        raise InvalidInput(unsupported)
    context = {'CMAKE_BUILD_TYPE': 'Debug', 'CMAKE_GENERATOR': 'Ninja', 'MANTIS_BUILD_STUDIO': 'OFF',
               'CMAKE_HOME_DIRECTORY': str(i.roots['source']), 'CMAKE_CACHEFILE_DIR': str(i.roots['build'])}
    if test == CALIBRATION_TEST:
        check(native.group('arch') == b'x86_64', unsupported)
        context['BUILD_TESTING'] = 'ON'
    entries = records(cache.get('entries'))
    pinned = [(r['name'], r.get('value')) for r in entries if r.get('name') in context]
    check(len(pinned) == len(context) and dict(pinned) == context, test + '-cache-mismatch')
    selected = [r.get('value') for r in entries if r.get('name') == '_Python3_EXECUTABLE']
    override = [r.get('value') for r in entries if r.get('name') == 'Python3_EXECUTABLE']
    check(len(selected) == 1 and (not override or override == selected), test + '-interpreter-unresolved')
    python = selected[0]
    # Compare the cache's absolute spelling, not a realpath (the interpreter
    # may be a symlink). Reject relative, aliased or nonliteral path spellings.
    check(isinstance(python, str) and 0 < len(python) <= 4096 and python.startswith('/') and
          '\\' not in python and python.isprintable() and
          all(part not in ('', '.', '..') for part in python[1:].split('/')),
          test + '-interpreter-unresolved')
    return python


def check_alg03_registration(i, python, target, artifacts, registration):
    """Match exactly one named registration using already pinned metadata, never argv I/O."""
    check(('source', ALG03_SCRIPT) in i.data, 'alg03-enclosure-script-pin-missing')
    script = str(i.roots['source'] / ALG03_SCRIPT)
    check(i.locate(script) == ('source', ALG03_SCRIPT), 'alg03-enclosure-context-unsupported')

    artifact_error = 'alg03-enclosure-artifact-unresolved'
    if target is None or target['type'] != 'EXECUTABLE':
        raise InvalidInput(artifact_error)
    outputs = target.get('artifacts', [])
    check(len(outputs) == 1, artifact_error)
    spelling = outputs[0]['path']
    loc = i.locate(spelling, 'build')
    if loc is None or loc[0] != 'build' or not loc[1] or artifacts.get(loc) != {target['id']}:
        raise InvalidInput(artifact_error)
    artifact = str(i.roots['build'] / loc[1])
    check(spelling in (loc[1], artifact), artifact_error)

    check(registration is not None, 'alg03-enclosure-registration-missing')
    command = registration.get('command')
    check(command is not None, 'alg03-enclosure-command-missing')
    check(command == [python, '-B', script, artifact], 'alg03-enclosure-argv-mismatch')


def check_calibration_registration(i, python, ctest, registration, cmake_lines):
    """Match the named argv and its exact top-level add_test backtrace."""
    if registration is None:
        raise InvalidInput('python-calibration-registration-missing')
    script = str(i.roots['source'] / CALIBRATION_SCRIPT)
    build = str(i.roots['build'])
    check(i.locate(script) == ('source', CALIBRATION_SCRIPT) and
          registration.get('command') == [python, script, build], 'python-calibration-argv-mismatch')
    props = {r['name']: r.get('value') for r in registration.get('properties', [])}
    check(props.get('WORKING_DIRECTORY') == build and not {'ENVIRONMENT', 'ENVIRONMENT_MODIFICATION'} & props.keys(),
          'python-calibration-context-unsupported')

    def at(rows, index):
        return rows[index] if isinstance(rows, list) and type(index) is int and 0 <= index < len(rows) else None

    graph = ctest.get('backtraceGraph')
    error = 'python-calibration-registration-unresolved'
    if not isinstance(graph, dict):
        raise InvalidInput(error)
    node = at(graph.get('nodes'), registration.get('backtrace'))
    if not isinstance(node, dict):
        raise InvalidInput(error)
    check(at(graph.get('commands'), node.get('command')) == 'add_test' and
          at(graph.get('files'), node.get('file')) in ('CMakeLists.txt', str(i.roots['source'] / 'CMakeLists.txt')),
          error)
    line = node.get('line')
    declaration = (b'add_test(NAME python-calibration COMMAND ${Python3_EXECUTABLE} '
                   b'${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/python_calibration.py ${CMAKE_BINARY_DIR})')
    check(type(line) is int and 1 <= line <= len(cmake_lines) and
          cmake_lines[line - 1] == declaration and cmake_lines.count(declaration) == 1, error)


def check_calibration_trace(i, expected, evidence_pins):
    """Validate only the scoped projection of a completed expanded CMake trace."""
    missing = 'python-calibration-copy-trace-missing'
    unsupported = 'python-calibration-copy-trace-unsupported'
    raw = i.data.get(CALIBRATION_COPY_RECEIPT)
    check(raw is not None, missing)
    check(len(raw) <= 32_768, unsupported)
    context = ('source', 'roots', 'configuration', 'generator')
    try:
        receipt = policy.strict_json(raw)
        policy.keys(receipt, 'schema sha256 commands ' + ' '.join(context), unsupported)
    except ValueError:
        raise InvalidInput(unsupported) from None
    check(receipt['schema'] == 'ci110-mantis-python-calibration-copy-trace-v3', unsupported)
    check(all(receipt[k] == i.manifest[k] for k in context) and
          receipt['sha256'] == {name: policy.digest(i.data[key]) for name, key in evidence_pins.items()},
          'python-calibration-copy-trace-context-mismatch')
    # Count invocations, not distinct values: a repeated or competing command
    # is ambiguous even if it wrote exactly the same bytes. No evaluation of
    # unknown commands, directory destinations or implicit path resolution.
    commands = receipt['commands']
    check(isinstance(commands, list) and len(commands) == len(expected) and
          all(isinstance(command, dict) and all(type(command.get(key)) is int
              for key in ('line', 'frame', 'global_frame')) for command in commands) and
          all(commands.count(command) == 1 for command in expected), 'python-calibration-copyonly-unresolved')


def check_calibration_copy(i, cmake_inputs, cache_path, python, ctest, registration):
    """Fixed COPYONLY and import evidence, not a CMake or Python evaluator."""
    copies = ((CALIBRATION_SOURCE, 'python/mantis/calibration.py'), (CALIBRATION_INIT, 'python/mantis/__init__.py'))
    required = [('source', path) for path in ('CMakeLists.txt', CALIBRATION_SCRIPT)]
    required.extend(key for src, dst in copies for key in (('source', src), ('build', dst)))
    check(all(key in i.data for key in required), 'python-calibration-pin-missing')
    cmake_lines = [line.strip() for line in i.data['source', 'CMakeLists.txt'].splitlines()]
    check_calibration_registration(i, python, ctest, registration, cmake_lines)
    expected = []
    for src, dst in copies:
        declaration = f'configure_file({src} ${{CMAKE_BINARY_DIR}}/{dst} COPYONLY)'.encode()
        check([r for r in cmake_inputs if r.get('path') == src] == [{'path': src}] and
              cmake_lines.count(declaration) == 1, 'python-calibration-copyonly-unresolved')
        check(i.data['source', src] == i.data['build', dst], 'python-calibration-copy-bytes-mismatch')
        expected.append({'cmd': 'configure_file', 'file': str(i.roots['source'] / 'CMakeLists.txt'),
                         'line': cmake_lines.index(declaration) + 1, 'frame': 1, 'global_frame': 1,
                         'args': [src, str(i.roots['build'] / dst), 'COPYONLY']})
    check(all(policy.digest(i.data['source', path]) == digest for path, digest in CALIBRATION_IMPORT_DIGESTS.items()),
          'python-calibration-import-unrecognized')
    pins = {'index': ('build', i.manifest['index']), 'cache': ('build', cache_path),
            'ctest': ('build', i.manifest['ctest']), **{path: (root, path) for root, path in required}}
    check_calibration_trace(i, expected, pins)


def import_inputs(inputs, expected, changes):
    """Import observations, projecting ONLY reachability onto the existing policy."""
    i = Inputs(inputs, expected)
    index_path = inputs['index']
    index = i.json(index_path)
    cmake = index['cmake']
    check(isinstance(cmake, dict) and isinstance(cmake.get('generator'), dict) and
          cmake['generator'].get('name') == inputs['generator'], 'configured-generator-mismatch')
    cmake_version = cmake.get('version', {}).get('string')
    check(label(cmake_version), 'configured-cmake-version')
    objects = records(index['objects'])
    by_kind = {}
    cache_path = ''
    reply = str(Path(index_path).parent)
    for row in objects:
        kind = row.get('kind')
        check(kind in ('codemodel', 'cache', 'cmakeFiles'), 'configured-unsupported-index-record')
        check(kind not in by_kind and relative(row.get('jsonFile')), 'configured-duplicate-index-record')
        check('/' not in row['jsonFile'], 'configured-reply-reference')
        document = i.json(reply + '/' + row['jsonFile'])
        version(document, kind, 1 if kind == 'cmakeFiles' else 2)
        check(document['version'] == row.get('version'), 'configured-index-version-mismatch')
        by_kind[kind] = document
        if kind == 'cache':
            cache_path = reply + '/' + row['jsonFile']
    check(set(by_kind) == {'codemodel', 'cache', 'cmakeFiles'}, 'configured-missing-index-record')
    model = by_kind['codemodel']
    for doc in (model, by_kind['cmakeFiles']):
        check(doc.get('paths') == {name: str(path) for name, path in i.roots.items()},
              'configured-metadata-root-mismatch')
    unknowns = BoundedSet()
    def unknown(reason, subject='inventory'):
        check(label(reason) and (subject == 'inventory' or relative(subject) or label(subject)),
              'configured-unknown-subject')
        unknowns.add((reason, subject))
    # These remain unconditional even when every supplied record was parsed.
    for reason in ('dependency-completeness-unqualified', 'preprocessor-and-transitive-closure-unqualified',
                   'configuration-coverage-unqualified', 'caller-source-binding-not-authenticated',
                   'ctest-configuration-and-source-binding-unverified'):
        unknown(reason)
    cmake_inputs = records(by_kind['cmakeFiles']['inputs'])
    for row in cmake_inputs:
        loc = i.locate(row.get('path'))
        if row.get('isGenerated'):
            unknown('generated-configure-input')
        elif loc is None:
            unknown('external-configure-input')
        elif loc not in i.data:
            unknown('unpinned-configure-input', loc[1])
    configs = records(model['configurations'])
    check(all(isinstance(c.get('name'), str) for c in configs) and
          len({c['name'] for c in configs}) == len(configs), 'configured-duplicate-configuration')
    chosen = [c for c in configs if c['name'] == inputs['configuration']]
    check(len(chosen) == 1, 'configured-configuration-mismatch')
    if len(configs) != 1 or cmake['generator'].get('multiConfig'):
        unknown('multi-configuration-inventory')
    rows = records(chosen[0]['targets'])
    targets, target_docs, names, artifacts = {}, {}, {}, {}
    file_edges, dependency_edges, test_edges = BoundedSet(), BoundedSet(), BoundedSet()
    for row in rows:
        tid, name = row['id'], row['name']
        check(label(tid) and label(name) and tid not in targets and name not in names,
              'configured-duplicate-or-invalid-target')
        filename = row['jsonFile']
        check(relative(filename) and '/' not in filename, 'configured-target-reference')
        doc = i.json(reply + '/' + filename)
        check(doc.get('id') == tid and doc.get('name') == name, 'configured-target-identity-mismatch')
        check(doc.get('type') in ('EXECUTABLE', 'STATIC_LIBRARY', 'SHARED_LIBRARY', 'MODULE_LIBRARY',
                                  'OBJECT_LIBRARY', 'INTERFACE_LIBRARY', 'UTILITY'), 'configured-target-type')
        targets[tid] = {'dependencies': [], 'sources': []}
        target_docs[tid] = doc
        names[name] = tid
        if doc['type'] in ('UTILITY', 'INTERFACE_LIBRARY'):
            unknown('noncompiled-target-closure', tid)
        if doc['type'] == 'OBJECT_LIBRARY':
            # Real CMake may retain /./ components in object-artifact names.
            # They are not executable test artifacts. Do not repair paths or
            # infer a runnable command from them; retain the target edges only.
            unknown('object-artifacts-unresolved', tid)
            continue
        for item in records(doc.get('artifacts', [])):
            loc = i.locate(item['path'], 'build')
            check(loc is not None and loc[0] == 'build', 'configured-external-artifact')
            artifacts.setdefault(loc, set()).add(tid)
    check(targets, 'configured-empty-targets')
    source_paths = {path for root, path in i.data if root == 'source'}
    for path in source_paths:
        if path.endswith(('.proto', '.qml', '.qrc')):
            unknown('protobuf-or-qml-generation-unresolved', path)
    for tid, doc in target_docs.items():
        dependencies = records(doc.get('dependencies', []))
        dep_ids = [d['id'] for d in dependencies]
        check(all(label(d) for d in dep_ids) and len(set(dep_ids)) == len(dep_ids),
              'configured-duplicate-dependency')
        for dep in dep_ids:
            if dep not in targets:
                unknown('unresolved-target-dependency', tid)
            else:
                targets[tid]['dependencies'].append(dep)
                dependency_edges.add((dep, tid, 'observed', 'codemodel-dependency'))
        include_dirs = []
        for group in records(doc.get('compileGroups', [])):
            for inc in records(group.get('includes', [])):
                loc = i.locate(inc['path'])
                if loc is None:
                    unknown('external-include-directory', tid)
                else:
                    include_dirs.append(loc)
        pending = []
        seen = set()
        sources = records(doc.get('sources', []))
        seen_sources = set()
        for source in sources:
            loc = i.locate(source['path'])
            check(loc not in seen_sources, 'configured-duplicate-source')
            seen_sources.add(loc)
            if loc is None:
                unknown('external-source', tid)
            elif source.get('isGenerated') or loc[0] == 'build':
                unknown('generated-source-unresolved', tid)
            elif loc not in i.data:
                unknown('unpinned-target-source', loc[1])
            else:
                file_edges.add((loc[1], tid, 'observed', 'codemodel-source'))
                pending.append(loc[1])
        # Literal include discovery is conservative inference, NOT compilation.
        # Comments/conditional branches can add false positives; macros, system
        # and generated includes stay unknown. Never read a discovered path.
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            if not path.endswith(('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl')):
                unknown('unsupported-source-language', path)
                continue
            try:
                text = i.data['source', path].decode('utf-8')
            except UnicodeError:
                unknown('unsupported-source-encoding', path)
                continue
            for line in text.splitlines():
                if not re.match(r'\s*#\s*include\b', line):
                    continue
                match = re.match(r'\s*#\s*include\s*"([^"\n]+)"', line)
                if not match or not relative(match[1]):
                    unknown('nonliteral-or-unsupported-include', path)
                    continue
                part = match[1]
                candidates = [('source', (Path(path).parent / part).as_posix())]
                candidates += [(root, (Path(directory) / part).as_posix()) for root, directory in include_dirs]
                resolved = sorted({p for root, p in candidates if root == 'source' and p in source_paths})
                if not resolved:
                    unknown('unresolved-literal-include', path)
                for header in resolved:
                    file_edges.add((header, tid, 'conservative-inference', 'literal-include-from:' + path))
                    pending.append(header)
    if inputs['schema'] in ('ci110-configured-inputs-v2', PYTHON_PROTO_INPUT_SCHEMA, *DIRECT_TEST_INPUT_SCHEMAS):
        try:
            owner = protobuf_owner(i, cmake['generator'], configs, by_kind['cache'], cache_path, rows, target_docs)
        except InvalidInput as exc:
            unknown(str(exc), PROTO)
        else:
            file_edges.add((PROTO, owner, 'observed', 'ninja-query-protobuf-cxx'))
    # Separate opt-in keeps the canonical v1/v2 diagnostic replayable as-is.
    if inputs['schema'] in (PYTHON_PROTO_INPUT_SCHEMA, *DIRECT_TEST_INPUT_SCHEMAS):
        try:
            owner = python_protobuf_owner(i, cmake['generator'], configs, by_kind['cache'], cache_path,
                                          rows, target_docs)
        except InvalidInput as exc:
            unknown(str(exc), PROTO)
        else:
            file_edges.add((PROTO, owner, 'observed', 'ninja-query-protobuf-python'))
    # Detect cycles without recursion; absence of cycles says nothing about
    # omitted dependencies. The existing reachability worklist handles both.
    remaining = {t: len(info['dependencies']) for t, info in targets.items()}
    consumers = {t: [] for t in targets}
    for target, info in targets.items():
        for dep in info['dependencies']:
            consumers[dep].append(target)
    ready = [t for t, count in remaining.items() if count == 0]
    retired = 0
    while ready:
        retired += 1
        for consumer in consumers[ready.pop()]:
            remaining[consumer] -= 1
            if remaining[consumer] == 0:
                ready.append(consumer)
    if retired != len(targets):
        unknown('target-dependency-cycle')
    ctest = i.json(inputs['ctest'])
    check(ctest.get('kind') == 'ctestInfo' and ctest.get('version') == {'major': 1, 'minor': 0},
          'configured-ctest-version')
    tests = []
    test_ids = set()
    enclosure_registration = None
    calibration_registration = None
    for row in records(ctest['tests']):
        name = row['name']
        check(label(name) and name not in test_ids, 'configured-duplicate-or-invalid-test')
        test_ids.add(name)
        command = row.get('command')
        mapped = []
        if command is None:
            unknown('missing-ctest-command', name)
        else:
            check(isinstance(command, list) and command and all(isinstance(v, str) for v in command),
                  'configured-ctest-command')
            loc = i.locate(command[0], 'build')
            mapped = sorted(artifacts.get(loc, ()))
            if not mapped:
                unknown('unmapped-test-command', name)
            for tid in mapped:
                test_edges.add((tid, name, 'conservative-inference', 'ctest-command-artifact-match'))
            if len(mapped) > 1:
                unknown('ambiguous-test-artifact', name)
        props = records(row.get('properties', []))
        prop_names = [p['name'] for p in props]
        check(all(label(p) for p in prop_names) and len(set(prop_names)) == len(prop_names),
              'configured-duplicate-test-property')
        if any(p.startswith('FIXTURES_') or p in ('DEPENDS', 'REQUIRED_FILES', 'RESOURCE_LOCK',
                                                'RESOURCE_GROUPS') for p in prop_names):
            unknown('test-fixture-or-runtime-dependency-unresolved', name)
        unknown('test-runtime-closure-unqualified', name)
        tests.append({'id': name, 'targets': mapped, 'command_sha256': policy.digest(policy.canonical(command))})
        if name == ALG03_TEST:
            enclosure_registration = row
        if name == CALIBRATION_TEST:
            calibration_registration = row
    check(tests, 'configured-empty-tests')
    file_test_edges = []
    if inputs['schema'] in DIRECT_TEST_INPUT_SCHEMAS:
        try:
            python = configured_interpreter(i, cmake_version, cmake['generator'], configs, by_kind['cache'], ALG03_TEST)
            check_alg03_registration(i, python, target_docs.get(names.get(ALG03_NUMERIC)), artifacts,
                                     enclosure_registration)
        except InvalidInput as exc:
            unknown(str(exc), ALG03_SCRIPT)
        else:
            file_test_edges.append({'file': ALG03_SCRIPT, 'test': ALG03_TEST, 'evidence': 'observed',
                                    'reason': 'ctest-alg03-enclosure-argv-match'})
    if inputs['schema'] == CALIBRATION_INPUT_SCHEMA:
        try:
            python = configured_interpreter(i, cmake_version, cmake['generator'], configs, by_kind['cache'], CALIBRATION_TEST)
            check_calibration_copy(i, cmake_inputs, cache_path, python, ctest, calibration_registration)
        except InvalidInput as exc:
            unknown(str(exc), CALIBRATION_SOURCE)
        else:
            file_test_edges.append({'file': CALIBRATION_SOURCE, 'test': CALIBRATION_TEST,
                                    'evidence': 'conservative-inference',
                                    'reason': 'ctest-python-calibration-copyonly-import-match'})
    for path, tid, _, _ in sorted(file_edges):
        targets[tid]['sources'].append(path)
    for info in targets.values():
        info['sources'] = sorted(set(info['sources']))
        info['dependencies'].sort()
    result = {'schema': 'ci110-configured-diagnostic-v1', 'status': 'ingested',
            'source': inputs['source'], 'configuration': inputs['configuration'],
            'generator': inputs['generator'], 'cmake_version': cmake_version,
            'root_identity_sha256': {name: policy.digest(str(root).encode()) for name, root in i.roots.items()},
            'input_files': i.pins, 'closure_complete': False,
            'unknowns': [{'reason': r, 'subject': s, 'evidence': 'unresolved'} for r, s in sorted(unknowns)],
            'targets': [{'id': t, 'name': target_docs[t]['name'], **targets[t]} for t in sorted(targets)],
            'tests': sorted(tests, key=lambda t: t['id']),
            'file_target_edges': [{'file': p, 'target': t, 'evidence': e, 'reason': r}
                                  for p, t, e, r in sorted(file_edges)],
            'target_target_edges': [{'dependency': d, 'consumer': t, 'evidence': e, 'reason': r}
                                    for d, t, e, r in sorted(dependency_edges)],
            'target_test_edges': [{'target': t, 'test': n, 'evidence': e, 'reason': r}
                                  for t, n, e, r in sorted(test_edges)],
            'target_reasons': {}, 'impacted_test_ids': [],
            'execution_authorized': False, 'full_matrix_would_select': True,
            'limits': ['caller-pins-are-not-authentication', 'no-complete-product-closure',
                       'no-provider-event-authentication', 'no-signatures-or-OS-isolation',
                       'no-historical-recall-or-performance-qualification']}
    if inputs['schema'] in DIRECT_TEST_INPUT_SCHEMAS:
        result['file_test_edges'] = file_test_edges
    project(result, changes)
    return result


def diagnose(path, expected_digest, source, changes):
    result = {'schema': 'ci110-configured-diagnostic-v1', 'status': 'rejected', 'errors': [],
              'execution_authorized': False, 'full_matrix_would_select': True, 'closure_complete': False}
    try:
        check(anchor(expected_digest), 'configured-missing-anchor')
        raw = reader.read_regular(path)
        check(policy.digest(raw) == expected_digest, 'configured-manifest-digest-mismatch')
        result = import_inputs(policy.strict_json(raw), source, changes)
        result['manifest_sha256'] = expected_digest
    except InvalidInput as exc:
        result['errors'] = [str(exc)]
    except (ValueError, reader.ReadError, KeyError, TypeError, AttributeError, IndexError, RecursionError, OSError):
        # Fixed errors only: never echo input paths, arbitrary JSON or OS messages.
        # Detailed raw input stays with the caller's private pinned evidence.
        result['errors'] = ['configured-input-invalid-or-incomplete']
    return result


def project(result, changes):
    """Use the existing policy for impacts; never reinterpret diagnostics as gates."""
    if result['status'] != 'ingested':
        return
    files = {}
    for edge in result['file_target_edges']:
        files.setdefault(edge['file'], {'targets': [], 'tests': []})['targets'].append(edge['target'])
    for edge in result.get('file_test_edges', ()):
        files.setdefault(edge['file'], {'targets': [], 'tests': []})['tests'].append(edge['test'])
    targets = {t['id']: t for t in result['targets']}
    reasons, impacted = policy.closure({'files': files, 'targets': targets, 'tests': result['tests']}, changes)
    result['target_reasons'] = dict(sorted(reasons.items()))
    result['impacted_test_ids'] = sorted(impacted)
    if any(c['path'] not in files for c in changes):
        item = {'evidence': 'unresolved', 'reason': 'changed-path-consumers-unresolved', 'subject': 'inventory'}
        if item not in result['unknowns']:
            result['unknowns'].append(item)
            result['unknowns'].sort(key=lambda u: (u['reason'], u['subject']))


def main():
    if len(sys.argv) != 5:
        return 2
    path, digest, commit, tree = sys.argv[1:]
    result = diagnose(path, digest, {'commit': commit, 'tree': tree}, [])
    sys.stdout.buffer.write(policy.canonical(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
