import importlib.util
from pathlib import Path
import unittest
s=importlib.util.spec_from_file_location('ctxeval',Path(__file__).resolve().parents[2]/'tools/evaluate_context_experiment.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)


class ContextReviewerTests(unittest.TestCase):
    def test_opposite_prefix_with_ambiguity_changes_and_vetoes(self):
        f=['us','test','','','нужно','указать','prose','1','1','1','A','K']
        self.assertEqual(m.reference(f),('R','KAKCAS'))
        # Exactly same observable fields for the opposite gold intent.
        a=m.delta({'gold':'C'},m.reference(f)[1][3])
        b=m.delta({'gold':'K'},m.reference(f)[1][3])
        self.assertEqual(a['autoRecovered'],1)
        self.assertEqual(b['falseAuto'],1)

    def test_suggest_is_not_auto_recovery(self):
        c=m.delta({'gold':'C'},'S')
        self.assertEqual(c['autoRecovered'],0)
        self.assertEqual(c['correctProposals'],1)
        k=m.delta({'gold':'K'},'S')
        self.assertEqual(k['falseAuto'],0)
        self.assertEqual(k['falseProposals'],1)

    def test_mixed_empty_and_unicode_scripts(self):
        self.assertEqual(m.script('ёЛОЧКА'),'R')
        self.assertEqual(m.script('αβγδ'),'-')
        f=['us','test','','','нужно','write','prose','1','1','1','A','K']
        self.assertEqual(m.reference(f),('-','KAKAAA'))

    def test_unknown_and_protected_precede_context(self):
        f=['us','test','','','нужно','указать','unknown','1','1','1','A','K']
        self.assertEqual(m.reference(f)[1],'KAKAAA')
        f[6]='protected'
        self.assertEqual(m.reference(f)[1],'KAKKKK')

    def test_ambiguous_keep_has_its_own_budget(self):
        from collections import defaultdict,Counter
        g=defaultdict(Counter)
        for key in ['proseKeep','protectedKeep','unknownKeep','ambiguousKeep','ambiguousCorrect','controlCorrect']:
            g[key]['count']=100
        g['ambiguousCorrect']['autoRecovered']=80
        g['controlCorrect']['autoRecovered']=80
        g['ambiguousKeep']['falseAuto']=1
        self.assertEqual(m.budget(g)['status'],'fail')
        self.assertFalse(m.budget(g)['checks']['ambiguousKeepZeroFalse'])


if __name__=='__main__':unittest.main()
