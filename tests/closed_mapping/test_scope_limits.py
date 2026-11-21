import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('closed_mapping_under_test', ROOT / 'tools/closed_hyprland_mapping.py')
closed = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = closed
spec.loader.exec_module(closed)
SLACK = dict(prefix='rasklad-slack-', memory_mib=3072, tasks=384, cpu_percent=200, runtime_seconds=660)


def props(argv):
    return dict(a[11:].split('=', 1) for a in argv if a.startswith('--property='))


class ScopeLimits(unittest.TestCase):
    def test_mapping_defaults_unchanged(self):
        argv = closed.scope_command(['/bin/true'], 'rasklad-mapping-1.scope')
        self.assertEqual(props(argv), {'MemoryMax': '1536M', 'MemorySwapMax': '0', 'TasksMax': '96',
                                       'CPUQuota': '100%', 'RuntimeMaxSec': '27s', 'TimeoutStopSec': '1s'})
        guard = argv[argv.index('-c') + 1]
        self.assertIn("bounded('memory.max',1610612736)", guard)
        self.assertIn("bounded('pids.max',96)", guard)
        self.assertEqual(argv[-1], '/bin/true')

    def test_slack_limits_and_guard_match(self):
        argv = closed.scope_command(['/bin/true'], 'rasklad-slack-1.scope', SLACK)
        self.assertEqual(props(argv)['MemoryMax'], '3072M')
        self.assertEqual(props(argv)['RuntimeMaxSec'], '660s')
        guard = argv[argv.index('-c') + 1]
        self.assertIn(str(3072 << 20), guard)
        self.assertIn("bounded('pids.max',384)", guard)

    def test_unit_must_match_prefix(self):
        with self.assertRaises(closed.base.Refused):
            closed.scope_command(['/bin/true'], 'rasklad-mapping-1.scope', SLACK)
        with self.assertRaises(closed.base.Refused):
            closed.scope_command(['/bin/true'], 'rasklad-slack-1.scope')

    def test_rejects_limits_above_ceiling_or_malformed(self):
        for bad in (dict(SLACK, memory_mib=8192), dict(SLACK, tasks=0), dict(SLACK, runtime_seconds=True),
                    dict(SLACK, prefix='other-'), {k: v for k, v in SLACK.items() if k != 'tasks'},
                    dict(SLACK, cpu_percent='100')):
            with self.assertRaises(closed.base.Refused):
                closed.scope_command(['/bin/true'], 'rasklad-slack-1.scope', bad)


class GuardEvaluation(unittest.TestCase):
    # Runs the real guard text against fake cgroup files (paths substituted only in this test copy).
    def run_guard(self, limits_, memory, pids, cpu, swap='0'):
        root = Path(tempfile.mkdtemp())
        (root / 'proc').write_text('0::/user.slice/x.scope\n')
        cg = root / 'cg/user.slice/x.scope'
        cg.mkdir(parents=True)
        for name, value in (('memory.max', memory), ('pids.max', pids), ('cpu.max', cpu), ('memory.swap.max', swap)):
            (cg / name).write_text(value + '\n')
        argv = closed.scope_command(['/bin/true'], limits_['prefix'] + '1.scope', limits_)
        guard = argv[argv.index('-c') + 1].replace('/proc/self/cgroup', str(root / 'proc')).replace('/sys/fs/cgroup', str(root / 'cg'))
        return subprocess.run([sys.executable, '-I', '-c', guard, '/bin/true']).returncode

    def test_slack_scope_with_200_percent_cpu_passes(self):
        self.assertEqual(self.run_guard(SLACK, str(3072 << 20), '384', '200000 100000'), 0)

    def test_cpu_above_requested_quota_refused(self):
        self.assertEqual(self.run_guard(SLACK, str(3072 << 20), '384', '300000 100000'), 72)
        self.assertEqual(self.run_guard(closed.MAPPING_LIMITS, str(1536 << 20), '96', '200000 100000'), 72)

    def test_memory_or_tasks_above_limit_refused(self):
        self.assertEqual(self.run_guard(SLACK, str(4096 << 20), '384', '200000 100000'), 72)
        self.assertEqual(self.run_guard(SLACK, str(3072 << 20), '385', '200000 100000'), 72)

    def test_mapping_guard_passes_with_original_limits(self):
        self.assertEqual(self.run_guard(closed.MAPPING_LIMITS, str(1536 << 20), '96', '100000 100000'), 0)


class PrefixCeilings(unittest.TestCase):
    def test_mapping_prefix_cannot_exceed_original_limits(self):
        for key, value in (('memory_mib', 1537), ('tasks', 97), ('cpu_percent', 101), ('runtime_seconds', 28)):
            with self.assertRaises(closed.base.Refused):
                closed.scope_command(['/bin/true'], 'rasklad-mapping-1.scope', dict(closed.MAPPING_LIMITS, **{key: value}))

    def test_non_dict_refused(self):
        for bad in (None, [('prefix', 'rasklad-slack-')], 'rasklad-slack-'):
            with self.assertRaises(closed.base.Refused):
                closed.scope_command(['/bin/true'], 'rasklad-slack-1.scope', bad)


if __name__ == '__main__':
    unittest.main()
