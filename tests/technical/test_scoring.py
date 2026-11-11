"""Scientific scoring regressions: false-change dilution and known intent."""
import importlib.util
from pathlib import Path
import unittest
import tempfile
import json
import hashlib
s = importlib.util.spec_from_file_location('summary', Path(__file__).resolve().parents[2] / 'tools/summarize_technical_evaluation.py')
m = importlib.util.module_from_spec(s)
s.loader.exec_module(m)


class ScoringTests(unittest.TestCase):
    def test_known_valid_intent_not_excused_by_dictionary_ambiguity(self):
        role, counts = m.outcome('valid', True, True, 'C')
        self.assertEqual(role, 'valid')
        self.assertEqual(counts['unsafe'], 1)
        self.assertEqual(counts['recovered'], 0)

    def test_ambiguous_wrong_is_never_a_recovery(self):
        role, counts = m.outcome('wrong', True, True, 'C')
        self.assertEqual(role, 'ambiguousWrong')
        self.assertEqual(counts['unsafe'], 1)
        self.assertEqual(counts['recovered'], 0)

    def test_unsupported_words_cannot_dilute_false_change_budget(self):
        occ = m.rates({'valid': {'count': 1000000, 'eligible': 500, 'eligibleChanges': 1},
                       'wrong': {'eligible': 500, 'eligibleRecovered': 400}})
        typ = m.rates({'valid': {'eligible': 500, 'eligibleChanges': 1}})
        self.assertFalse(m.screen(occ, typ)['checks']['validOccurrenceBudget'])

    def test_frequency_cannot_hide_a_type_error(self):
        occ = m.rates({'valid': {'eligible': 100000, 'eligibleChanges': 1},
                       'wrong': {'eligible': 1000, 'eligibleRecovered': 700}})
        typ = m.rates({'valid': {'eligible': 1000, 'eligibleChanges': 1}})
        screen = m.screen(occ, typ)
        self.assertTrue(screen['checks']['validOccurrenceBudget'])
        self.assertFalse(screen['checks']['validTypeBudget'])
        self.assertEqual(screen['status'], 'fail')

    def test_complete_synthetic_aggregation_and_gate_screen(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'data').mkdir()
            (root / 'build/experiment-004-data').mkdir(parents=True)
            (root / 'build/experiment-002-data').mkdir()
            source_root = Path(__file__).resolve().parents[2]
            (root / 'data/experiment-004-protocol.json').write_bytes(
                (source_root / 'data/experiment-004-protocol.json').read_bytes())
            old = []
            for lang, text in [('en', 'hello'), ('ru', 'привет')]:
                raw = ('1\t' + lang + '\t' + text + '\n').encode()
                (root / f'build/experiment-002-data/{lang}.tsv').write_bytes(raw)
                old.append({'language': lang, 'sha256': hashlib.sha256(raw).hexdigest()})
            (root / 'data/evaluation-002-sources.json').write_text(json.dumps({'evaluation_sources': old}))
            rows, actions, documents = [], [], []
            for doc in range(40):
                lang = 'en' if doc < 20 else 'ru'
                documents.append({'index': doc, 'language': lang, 'sha256': str(doc)})
                for role, codes in [('valid', 'KAAAAAAAA'), ('wrong', 'KCCCCCCCC'), ('protected', 'KCCCCKKKK')]:
                    ordinal = len(rows)
                    original = 'hello' if lang == 'en' else 'привет'
                    text = original
                    layout = 'us' if lang == 'en' else 'ru'
                    if role == 'wrong':
                        text = 'руддщ' if lang == 'en' else 'ghbdtn'
                        layout = 'ru' if lang == 'en' else 'us'
                    rows.append(f'{doc}\t{lang}\t{role}\t{layout}\t{text}\t{original}\n')
                    actions.append(f'{ordinal}\t1\t0\t1\t{codes}\n')
            raw = ''.join(rows).encode()
            (root / 'build/experiment-004-data/cases.tsv').write_bytes(raw)
            decision_path = root / 'build/decisions.tsv'
            decision_path.write_text(''.join(actions))
            manifest = {'status': 'approved-local-evaluation', 'scope': 'local-evaluation-only',
                        'release_approved': False, 'protocol_sha256': m.PROTOCOL_SHA, 'documents': documents,
                        'cases': {'path': 'build/experiment-004-data/cases.tsv', 'sha256': hashlib.sha256(raw).hexdigest(),
                                  'bytes': len(raw), 'rows': 120}}
            report = m.summarize(root, manifest, decision_path)
            full = report['methods']['hunspellFull']['pooled']
            guarded = report['methods']['protectedHunspellFull']['pooled']
            self.assertEqual(full['occurrences']['wrong']['recovered'], 40)
            self.assertNotIn('falsePer10000Eligible', full['occurrences']['wrong'])
            self.assertEqual(full['occurrences']['wrong']['changesPer10000Eligible'], 10000)
            self.assertEqual(full['types']['wrong']['recovered'], 2)
            self.assertEqual(full['occurrences']['protected']['changes'], 40)
            self.assertEqual(full['screen']['status'], 'fail')
            self.assertEqual(guarded['occurrences']['protected']['changes'], 0)
            self.assertEqual(guarded['screen']['status'], 'pass')
            self.assertEqual(report['methods']['alwaysKeep']['pooled']['screen']['status'], 'fail')

    def test_always_keep_fails_recovery_and_empty_sample_insufficient(self):
        occ = m.rates({'valid': {'eligible': 20000}, 'wrong': {'eligible': 1000}})
        typ = m.rates({'valid': {'eligible': 12000}})
        self.assertEqual(m.screen(occ, typ)['status'], 'fail')
        self.assertEqual(m.screen(m.rates({}), m.rates({}))['status'], 'insufficient')


if __name__ == '__main__':
    unittest.main()
