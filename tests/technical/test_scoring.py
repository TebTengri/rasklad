"""Scientific scoring regressions: false-change dilution and known intent."""
import importlib.util
from pathlib import Path
import unittest
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

    def test_always_keep_fails_recovery_and_empty_sample_insufficient(self):
        occ = m.rates({'valid': {'eligible': 20000}, 'wrong': {'eligible': 1000}})
        typ = m.rates({'valid': {'eligible': 12000}})
        self.assertEqual(m.screen(occ, typ)['status'], 'fail')
        self.assertEqual(m.screen(m.rates({}), m.rates({}))['status'], 'insufficient')


if __name__ == '__main__':
    unittest.main()
