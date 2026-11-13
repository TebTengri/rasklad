import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('latency', pathlib.Path(__file__).parents[1] / 'tools/input_latency_report.py')
latency = importlib.util.module_from_spec(spec)
spec.loader.exec_module(latency)


def fixture():
    return {'report': {'overall_passed': True, 'scenario': 'fcitx-perf',
        'timing_sent_ns': [1_000_000_000 + i * 10_000_000 for i in range(60)],
        'field_assertions_client_view': {'timing_complete': True,
            'timing_observed_ns': [1_001_000_000 + i * 10_000_000 for i in range(60)]},
        'fixture_resources': {'clock_ticks_per_second': 100, 'phases': {
            'idle-start': {'cpu_ticks': 1, 'wall_ns': 1},
            'idle-end': {'cpu_ticks': 1, 'wall_ns': 1_000_000_001},
            'typing-start': {'cpu_ticks': 1, 'wall_ns': 2_000_000_001},
            'typing-end': {'cpu_ticks': 3, 'wall_ns': 3_000_000_001}},
            'final': {'rss_kib': 1000, 'peak_rss_kib': 1200}}}}


class LatencyReportTests(unittest.TestCase):
    def test_units_and_quantiles(self):
        r = latency.summarize(fixture())
        self.assertEqual(r['editor_state_latency_ms'], dict.fromkeys(['p50','p95','p99','max'], 1.0))
        self.assertEqual(r['daemon_resources']['typing_cpu_ms'], 20)
        self.assertEqual(r['daemon_resources']['cpu_resolution_ms'], 10)

    def test_failed_or_missing_observations_rejected(self):
        for key in ['overall_passed', 'timing_complete']:
            d = fixture()
            target = d['report'] if key == 'overall_passed' else d['report']['field_assertions_client_view']
            target[key] = False
            with self.assertRaises(ValueError): latency.summarize(d)
        d = fixture(); d['report']['timing_sent_ns'].pop()
        with self.assertRaises(ValueError): latency.summarize(d)

    def test_bad_timestamps_rejected(self):
        for bad in [0, True, -1, 1.5, 999_999_999, 90_000_000_000]:
            d = fixture(); d['report']['field_assertions_client_view']['timing_observed_ns'][0] = bad
            with self.assertRaises(ValueError): latency.summarize(d)
        d = fixture(); a = d['report']['timing_sent_ns']; a[1] = a[0]
        with self.assertRaises(ValueError): latency.summarize(d)

    def test_bad_resource_window_rejected(self):
        d = fixture(); d['report']['fixture_resources']['phases']['typing-end']['cpu_ticks'] = 0
        with self.assertRaises(ValueError): latency.summarize(d)


if __name__ == '__main__': unittest.main()
