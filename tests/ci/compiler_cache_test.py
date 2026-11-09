"""Dependency/policy fixtures and real compiler-cache regressions for Architecture.

Run with python3-yaml, ccache, gcc and g++ available. No project test is replaced.
"""
import ast
import datetime
import fnmatch
import importlib.util
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "tools/ci_ccache.py"
spec = importlib.util.spec_from_file_location("ci_ccache", HELPER)
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)
WORKFLOW = yaml.load((ROOT / ".github/workflows/build.yml").read_text(), Loader=yaml.BaseLoader)


def expression(text, context, success=True):
    """Evaluate this workflow's boolean/string subset, after actionlint validation."""
    text = text.strip().removeprefix("${{").removesuffix("}}").strip()
    text = text.replace("success()", repr(success)).replace("always()", "True")
    text = re.sub(r"(?:github|steps|runner|matrix)\.[\w.-]+",
                  lambda match: repr(context[match.group()]), text)
    text = text.replace("&&", " and ").replace("||", " or ")
    tree = ast.parse(text, mode="eval")
    allowed = (ast.Expression, ast.Constant, ast.BoolOp, ast.And, ast.Or,
               ast.Compare, ast.Eq, ast.NotEq)
    if not all(isinstance(node, allowed) for node in ast.walk(tree)):
        raise ValueError("Unsupported workflow expression")
    return eval(compile(tree, "<workflow-expression>", "eval"), {"__builtins__": {}})


class WorkflowPolicy(unittest.TestCase):
    def test_event_matrix_preserves_pr_merge_results_and_trusted_pushes(self):
        events = WORKFLOW["on"]
        self.assertEqual(set(events), {"push", "pull_request", "workflow_dispatch"})
        self.assertEqual(events["pull_request"], "")  # No base/path filters.
        patterns = events["push"]["branches"]
        for branch, expected in (("develop", True), ("main", True),
                                 ("integration/staged", True), ("integration/a/b", True),
                                 ("feature/with-pr", False), ("feature/without-pr", False)):
            self.assertEqual(any(fnmatch.fnmatchcase(branch, p) for p in patterns), expected, branch)
        for job in WORKFLOW["jobs"].values():
            self.assertNotIn("if", job)
            checkout = job["steps"][0]
            self.assertEqual(checkout, {"uses": "actions/checkout@v4"})
        self.assertEqual(set(WORKFLOW["jobs"]), {"linux", "sanitizers"})
        matrix = WORKFLOW["jobs"]["linux"]["strategy"]["matrix"]
        self.assertEqual(set(itertools.product(matrix["runner"], matrix["studio"])), {
            ("ubuntu-24.04", "ON"), ("ubuntu-24.04", "OFF"),
            ("ubuntu-24.04-arm", "ON"), ("ubuntu-24.04-arm", "OFF")})
        self.assertEqual(WORKFLOW["jobs"]["sanitizers"]["runs-on"], "ubuntu-24.04")

    def test_only_successful_trusted_push_misses_publish(self):
        for job in WORKFLOW["jobs"].values():
            saves = [step for step in job["steps"] if step.get("id") == "cache-save"]
            self.assertEqual(len(saves), 1)
            save = saves[0]
            trim = next(s for s in job["steps"] if s.get("run") == "python3 tools/ci_ccache.py trim")
            self.assertEqual(trim["if"], save["if"])
            self.assertTrue(save["uses"].startswith("actions/cache/save@"))
            for event, ref, hit, passed in itertools.product(
                    ("push", "pull_request", "workflow_dispatch"),
                    ("refs/heads/develop", "refs/heads/main", "refs/heads/integration/a",
                     "refs/pull/8/merge", "refs/heads/feature/a"), ("true", "false", ""), (True, False)):
                context = {"github.event_name": event, "github.ref": ref,
                           "steps.cache-restore.outputs.cache-hit": hit}
                expected = passed and event == "push" and ref in (
                    "refs/heads/develop", "refs/heads/main") and hit != "true"
                self.assertEqual(bool(expression(save["if"], context, passed)), expected)
            restores = [s for s in job["steps"] if s.get("id") == "cache-restore"]
            self.assertEqual(len(restores), 1)
            self.assertTrue(restores[0]["uses"].startswith("actions/cache/restore@"))
            self.assertEqual(restores[0]["with"], {
                "path": "${{ steps.cache-config.outputs.directory }}",
                "key": "${{ steps.cache-config.outputs.key }}",
                "restore-keys": "${{ steps.cache-config.outputs.prefix }}"})
            self.assertEqual(save["with"]["key"], restores[0]["with"]["key"])
            for step in job["steps"]:
                self.assertNotIn("continue-on-error", step)
                if "cache-hit" in step.get("if", ""):
                    self.assertTrue(step.get("id") == "cache-save" or step == trim)

    def test_concurrency_distinguishes_prs_branches_events_and_manual_runs(self):
        group = WORKFLOW["concurrency"]["group"]
        def render(event, ref, pr=0, run=1):
            context = {"github.workflow": "architecture", "github.event_name": event,
                       "github.ref": ref, "github.event.pull_request.number": pr,
                       "github.run_id": run}
            return re.sub(r"\$\{\{(.*?)\}\}", lambda m: str(expression(m[0], context)), group)
        self.assertEqual(render("pull_request", "refs/pull/7/merge", 7, 1),
                         render("pull_request", "refs/pull/7/merge", 7, 2))
        values = [render("pull_request", "refs/pull/7/merge", 7),
                  render("pull_request", "refs/pull/8/merge", 8),
                  render("push", "refs/heads/develop"), render("push", "refs/heads/main"),
                  render("push", "refs/heads/integration/a"),
                  render("workflow_dispatch", "refs/heads/develop", run=1),
                  render("workflow_dispatch", "refs/heads/develop", run=2)]
        self.assertEqual(len(set(values)), len(values))
        self.assertEqual(WORKFLOW["concurrency"]["cancel-in-progress"], "true")

    def test_test_commands_and_sanitizer_environment_stay_strict(self):
        for name, job in WORKFLOW["jobs"].items():
            tests = [s for s in job["steps"] if s.get("run", "").startswith("ctest ")]
            directory = "build/ci" if name == "linux" else "build/sanitizers"
            self.assertEqual(len(tests), 1)
            self.assertEqual(tests[0]["run"], f"ctest --test-dir {directory} --output-on-failure")
            self.assertNotIn("if", tests[0])
            if name == "sanitizers":
                self.assertEqual(tests[0]["env"], {"ASAN_OPTIONS": "detect_leaks=1",
                                                   "UBSAN_OPTIONS": "halt_on_error=1"})
            summary = job["steps"][-1]
            self.assertTrue(expression(summary["if"], {"steps.cache-config.outcome": "success"}, False))


class DependencyProvisioning(unittest.TestCase):
    """Exercise the actual dependency scripts with builtin-only command fixtures.

    ARM runner names are selectors, not native ARM or resolved-closure evidence.
    No real sudo, APT, dpkg, tee, compiler or network operation runs here.
    """

    SHIMS = r'''
record() { printf '%s\0' "$@" >> "$CI111_TRACE"; printf '\0' >> "$CI111_TRACE"; }
sudo() {
  record sudo "$@"
  [[ "$1" == apt-get ]] || return 96
  case "$2" in
    update) return "$CI111_UPDATE_EXIT" ;;
    install) return "$CI111_INSTALL_EXIT" ;;
    *) return 97 ;;
  esac
}
dpkg-query() {
  record dpkg-query "$@"
  printf '%s\n' 'libopencv-dev fixture-version' 'libopencv-contrib-dev fixture-version'
}
tee() {
  [[ "$#" == 2 && "$1" == -a && "$2" == "$GITHUB_STEP_SUMMARY" ]] || return 98
  while IFS= read -r line; do printf '%s\n' "$line" >> "$GITHUB_STEP_SUMMARY"; done
}
'''

    def setUp(self):
        self.expected = json.loads((ROOT / 'tests/fixtures/ci/dependency-argv.json').read_text())
        self.normal = next(s for s in WORKFLOW['jobs']['linux']['steps'] if s.get('name') == 'Dependencies')
        self.sanitizer = next(s for s in WORKFLOW['jobs']['sanitizers']['steps']
                              if 'apt-get install ' in s.get('run', ''))

    def run_dependencies(self, step, runner=None, studio=None, update_exit=0, install_exit=0):
        with tempfile.TemporaryDirectory(prefix='mantis-dependency-fixture-') as directory:
            root = Path(directory)
            trace, summary, sentinel = root / 'trace.bin', root / 'summary.txt', root / 'injected.txt'
            # Do not inherit credentials, shell startup hooks or dependency configuration.
            env = {'PATH': '/mantis-no-external-programs', 'LANG': 'C',
                   'CI111_TRACE': str(trace), 'GITHUB_STEP_SUMMARY': str(summary),
                   'CI111_SENTINEL': str(sentinel), 'CI111_UPDATE_EXIT': str(update_exit),
                   'CI111_INSTALL_EXIT': str(install_exit)}
            if runner is not None:
                env['MANTIS_CI_RUNNER'] = runner
            if studio is not None:
                env['MANTIS_CI_STUDIO'] = studio
            result = subprocess.run(['/bin/bash', '--noprofile', '--norc', '-e', '-o', 'pipefail',
                                     '-c', self.SHIMS + step['run']], env=env, cwd=root,
                                    capture_output=True, text=True, timeout=5)
            raw = trace.read_bytes() if trace.exists() else b''
            return {'exit': result.returncode,
                    'calls': [part.decode().split('\0') for part in raw.split(b'\0\0') if part],
                    'stderr': result.stderr,
                    'summary': summary.read_text() if summary.exists() else '',
                    'injected': sentinel.exists()}

    def assert_argv(self, step, packages, runner=None, studio=None):
        result = self.run_dependencies(step, runner, studio)
        self.assertEqual(result['exit'], 0, result['stderr'])
        self.assertEqual(result['calls'], [
            ['sudo', 'apt-get', 'update'],
            ['sudo', 'apt-get', 'install', '--assume-yes', *packages],
            ['dpkg-query', '-W', 'libopencv-dev', 'libopencv-contrib-dev'],
        ], 'Exact APT argv and package root order must match the independent fixture')
        self.assertEqual(result['summary'], 'libopencv-dev fixture-version\nlibopencv-contrib-dev fixture-version\n')
        self.assertFalse(result['injected'])

    def test_matrix_values_are_data_and_dependencies_cannot_be_skipped(self):
        self.assertEqual(self.normal['env'], {
            'MANTIS_CI_RUNNER': '${{ matrix.runner }}', 'MANTIS_CI_STUDIO': '${{ matrix.studio }}'})
        self.assertNotIn('${{', self.normal['run'])
        for step in (self.normal, self.sanitizer):
            self.assertNotIn('if', step)
            self.assertNotIn('continue-on-error', step)

    def test_on_argv(self):
        for runner in ('ubuntu-24.04', 'ubuntu-24.04-arm'):
            with self.subTest(runner=runner):
                self.assert_argv(self.normal, self.expected['ordinary_on'], runner, 'ON')

    def test_off_argv(self):
        for runner in ('ubuntu-24.04', 'ubuntu-24.04-arm'):
            with self.subTest(runner=runner):
                self.assert_argv(self.normal, self.expected['ordinary_off'], runner, 'OFF')

    def test_sanitizer_argv_unchanged(self):
        self.assert_argv(self.sanitizer, self.expected['sanitizer'])

    def test_unknown_matrix_studio(self):
        for studio in (None, '', 'on', 'OFF ', 'AUTO', 'ON\nOFF',
                       'OFF"; printf injected > "$CI111_SENTINEL"; #',
                       '$(printf injected > "$CI111_SENTINEL")'):
            with self.subTest(studio=studio):
                result = self.run_dependencies(self.normal, 'ubuntu-24.04', studio)
                self.assertEqual(result['exit'], 64, 'Unknown Studio must fail closed before APT')
                self.assertEqual(result['calls'], [])
                self.assertIn('Unsupported Studio mode', result['stderr'])
                self.assertFalse(result['injected'])

    def test_unknown_matrix_runner(self):
        for runner in (None, '', 'ubuntu-22.04', 'windows-latest', 'ubuntu-24.04-arm ',
                       'ubuntu-24.04"; printf injected > "$CI111_SENTINEL"; #'):
            with self.subTest(runner=runner):
                result = self.run_dependencies(self.normal, runner, 'ON')
                self.assertEqual(result['exit'], 64, 'Unknown runner must fail closed before APT')
                self.assertEqual(result['calls'], [])
                self.assertIn('Unsupported dependency runner', result['stderr'])
                self.assertFalse(result['injected'])

    def test_update_failure_is_primary_and_stops_install(self):
        for studio in ('ON', 'OFF'):
            result = self.run_dependencies(self.normal, 'ubuntu-24.04', studio, update_exit=17)
            self.assertEqual(result['exit'], 17, 'Update failure must remain primary')
            self.assertEqual(result['calls'], [['sudo', 'apt-get', 'update']])
            self.assertEqual(result['summary'], '')

    def test_install_failure_is_primary_and_stops_receipt(self):
        for studio in ('ON', 'OFF'):
            result = self.run_dependencies(self.normal, 'ubuntu-24.04-arm', studio, install_exit=23)
            self.assertEqual(result['exit'], 23, 'Install failure must remain primary')
            self.assertEqual(len(result['calls']), 2)
            self.assertEqual(result['calls'][0], ['sudo', 'apt-get', 'update'])
            self.assertEqual(result['calls'][1][:4], ['sudo', 'apt-get', 'install', '--assume-yes'])
            self.assertEqual(result['summary'], '')


class RealCache(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="mantis-cache-regression-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.base_env = {k: v for k, v in os.environ.items() if not k.startswith("CCACHE_")}
        self.base_env.update(RUNNER_TEMP=str(self.root), RUNNER_OS="Linux", RUNNER_ARCH="X64",
                             GITHUB_ENV=str(self.root / "env"), GITHUB_OUTPUT=str(self.root / "outputs"),
                             GITHUB_STEP_SUMMARY=str(self.root / "summary"))
        self.env, self.outputs = self.prepare()
        self.run_command([sys.executable, str(HELPER), "reset"])

    def run_command(self, command, *, env=None, expected=0):
        result = subprocess.run(command, env=env or self.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, expected, result.stdout)
        return result.stdout

    def prepare(self, studio="OFF", sanitizer="OFF", arch="X64"):
        env = self.base_env | {"RUNNER_ARCH": arch}
        self.run_command([sys.executable, str(HELPER), "prepare", "--studio", studio,
                          "--sanitizer", sanitizer], env=env)
        read = lambda p: dict(line.split("=", 1) for line in Path(p).read_text().splitlines())
        return env | read(env["GITHUB_ENV"]), read(env["GITHUB_OUTPUT"])

    def stats(self):
        return {k: int(v) for k, v in (line.split() for line in
                self.run_command(["ccache", "--print-stats"]).splitlines())}

    def compile(self, language="c", value=11):
        source = self.root / ("probe.c" if language == "c" else "probe.cpp")
        if not source.exists():
            source.write_text("int main(void) { return VALUE; }\n")
        compiler = "gcc" if language == "c" else "g++"
        obj = self.root / "probe.o"
        obj.unlink(missing_ok=True)
        self.run_command(["ccache", compiler, "-g", f"-DVALUE={value}", "-c", str(source), "-o", str(obj)])
        self.run_command([compiler, str(obj), "-o", str(self.root / "probe")])
        return subprocess.run([str(self.root / "probe")]).returncode

    def test_both_languages_compile_cold_then_reuse_warm(self):
        for language in ("c", "c++"):
            before = self.stats()
            self.assertEqual(self.compile(language), 11)
            cold = self.stats()
            self.assertEqual(cold["cache_miss"], before["cache_miss"] + 1)
            self.assertEqual(self.compile(language), 11)
            warm = self.stats()
            self.assertEqual(warm["direct_cache_hit"] + warm["preprocessed_cache_hit"],
                             cold["direct_cache_hit"] + cold["preprocessed_cache_hit"] + 1)

    def test_source_and_compiler_options_invalidate(self):
        self.assertEqual(self.compile(), 11)
        before = self.stats()["cache_miss"]
        self.assertEqual(self.compile(value=12), 12)
        self.assertEqual(self.stats()["cache_miss"], before + 1)
        (self.root / "probe.c").write_text("int main(void) { return VALUE + 1; }\n")
        self.assertEqual(self.compile(value=12), 13)
        self.assertEqual(self.stats()["cache_miss"], before + 2)

    def test_corrupt_result_cannot_return_false_success(self):
        self.assertEqual(self.compile(), 11)
        results = list(Path(self.env["CCACHE_DIR"]).rglob("*R"))
        self.assertTrue(results)
        for result in results:
            result.write_bytes(b"corrupt cache result")
        self.assertEqual(self.compile(), 11)  # Must compile again or fail, never stale output.
        self.assertEqual(self.stats()["cache_miss"], 2)

    def test_restored_configuration_cannot_disable_correctness(self):
        directory = Path(self.env["CCACHE_DIR"])
        directory.mkdir(exist_ok=True)
        (directory / "ccache.conf").write_text("compiler_check = none\nhash_dir = false\nsloppiness = time_macros\n")
        self.run_command([sys.executable, str(HELPER), "reset"])
        self.assertFalse(Path(self.env["CCACHE_CONFIGPATH"]).is_relative_to(directory))
        bad = self.env | {"CCACHE_COMPILERCHECK": "none"}
        result = subprocess.run([sys.executable, str(HELPER), "reset"], env=bad,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsafe effective ccache option", result.stdout)

    def test_all_configuration_namespaces_miss_even_in_shared_directory(self):
        keys = set()
        for arch, studio, sanitizer in (("X64", "ON", "OFF"), ("X64", "OFF", "OFF"),
                                        ("ARM64", "ON", "OFF"), ("ARM64", "OFF", "OFF"),
                                        ("X64", "OFF", "ON")):
            self.env, output = self.prepare(studio, sanitizer, arch)
            keys.add(output["prefix"])
            self.run_command(["ccache", "--zero-stats"])
            self.assertEqual(self.compile(), 11)
            self.assertEqual(self.stats()["cache_miss"], 1)
        self.assertEqual(len(keys), 5)  # Namespace test only; host compiler stays native.

    def test_failures_propagate_and_timing_does_not_override_exit(self):
        self.run_command([sys.executable, str(HELPER), "build", sys.executable,
                          "-c", "raise SystemExit(7)"], expected=7)
        outputs = Path(self.env["GITHUB_OUTPUT"]).read_text()
        self.assertIn("seconds=", outputs)
        self.assertEqual(self.compile(), 11)
        (self.root / "probe.c").write_text("#error deliberately invalid source\n")
        (self.root / "probe.o").unlink()
        result = subprocess.run([sys.executable, str(HELPER), "build", "ccache", "gcc", "-c",
                                 "-g", "-DVALUE=11", str(self.root / "probe.c"), "-o", str(self.root / "probe.o")],
                                env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "probe.o").exists())
        (self.root / "valid.c").write_text("int main(void) { return 0; }\n")
        (self.root / "malformed.conf").write_text("not a configuration entry\n")
        bad = self.env | {"CCACHE_CONFIGPATH": str(self.root / "malformed.conf")}
        result = subprocess.run([sys.executable, str(HELPER), "build", "ccache", "gcc", "-c",
                                 str(self.root / "valid.c"), "-o", str(self.root / "valid.o")], env=bad,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"ccache: error:", result.stdout)
        self.assertFalse((self.root / "valid.o").exists())
        tools = self.root / "failed-tool"
        tools.mkdir()
        (tools / "ccache").write_text("#!/bin/sh\nexit 23\n")
        (tools / "ccache").chmod(0o755)
        self.run_command([sys.executable, str(HELPER), "build", "ccache", "gcc", "-c",
                          str(self.root / "valid.c")],
                         env=self.env | {"PATH": str(tools) + os.pathsep + self.env["PATH"]}, expected=23)

    def test_weekly_refresh_is_bounded_and_unsafe_environment_rejected(self):
        monday = datetime.datetime(2026, 10, 26, tzinfo=datetime.timezone.utc)
        key = cache.snapshot_key(self.outputs["prefix"], monday)
        for day in range(7):
            self.assertEqual(cache.snapshot_key(self.outputs["prefix"], monday + datetime.timedelta(days=day)), key)
        self.assertNotEqual(cache.snapshot_key(self.outputs["prefix"], monday + datetime.timedelta(days=7)), key)
        self.assertNotEqual(cache.prefix("Linux", "X64", "OFF", "ON"), self.outputs["prefix"])
        with self.assertRaises(ValueError):
            cache.prefix("Linux", "unknown", "OFF", "OFF")
        result = subprocess.run([sys.executable, str(HELPER), "prepare", "--studio", "OFF", "--sanitizer", "OFF"],
                                env=self.base_env | {"CCACHE_SLOPPINESS": "time_macros"},
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0)

    def test_summary_records_current_counters_without_changing_failure_status(self):
        self.assertEqual(self.compile(), 11)
        env = self.env | {"CACHE_RESTORE": "success", "CACHE_EXACT_HIT": "",
                          "CACHE_SAVE": "skipped", "BUILD_SECONDS": "1.250"}
        self.run_command([sys.executable, str(HELPER), "summary"], env=env)
        summary = Path(env["GITHUB_STEP_SUMMARY"]).read_text()
        self.assertIn('"CACHE_SAVE": "skipped"', summary)
        self.assertIn('"BUILD_SECONDS": "1.250"', summary)
        self.assertIn("Cacheable calls", summary)
        self.assertIn("Misses", summary)
        self.assertIn("not cache transfer or tests", summary)


if __name__ == "__main__":
    unittest.main()
