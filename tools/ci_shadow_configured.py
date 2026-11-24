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
        check(manifest['schema'] == 'ci110-configured-inputs-v1', 'configured-input-version')
        policy.identity(manifest['source'])
        check(manifest['source'] == expected, 'configured-source-identity-mismatch')
        check(label(manifest['configuration']) and label(manifest['generator']), 'configured-context')
        policy.keys(manifest['roots'], 'source build', 'configured-roots')
        self.roots = {}
        for name, value in manifest['roots'].items():
            check(isinstance(value, str) and Path(value).is_absolute(), 'configured-root-path')
            root = Path(value)
            check(root == root.resolve() and root.is_dir(), 'configured-root-not-canonical')
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
    for row in records(by_kind['cmakeFiles']['inputs']):
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
    check(tests, 'configured-empty-tests')
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
