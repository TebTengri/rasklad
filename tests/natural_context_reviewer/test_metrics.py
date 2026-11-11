import importlib.util
from pathlib import Path
import unittest
s=importlib.util.spec_from_file_location('routereval',Path(__file__).resolve().parents[2]/'tools/evaluate_natural_router.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)


class NaturalRouterMetricsTests(unittest.TestCase):
    def test_observed_hit_preserved_despite_context_and_model(self):
        f=['us','test','','','нужно','указать','prose','1','1','1','C','K']
        vote,codes,eligible=m.reference(f)
        self.assertTrue(eligible);self.assertEqual(vote,'R')
        self.assertEqual(codes[1:7],'KKKKKK')
        self.assertEqual(codes[7],'C')  # legacy005 comparison only

    def test_context_veto_breaks_wrong_prefix_episode(self):
        f=['us','test','','','simple','words','prose','0','1','0','C','C']
        _,codes,_=m.reference(f)
        self.assertEqual(codes[1:4],'CCA')
        f[4:6]=['нужно','указать']
        self.assertEqual(m.reference(f)[1][1:4],'CCC')

    def test_shared_ambiguity_and_unknown_gates(self):
        f=['us','test','','','нужно','указать','prose','0','1','1','C','C']
        self.assertEqual(m.reference(f)[1][1:7],'AAAAAA')
        f[6]='unknown';f[9]='0'
        self.assertEqual(m.reference(f)[1][1:7],'AAAAAA')

    def test_model_only_suggest_filtered_without_auto_recovery(self):
        f=['us','test','','','simple','words','prose','0','0','0','C','A']
        _,codes,_=m.reference(f)
        self.assertEqual(codes[4:7],'SAA')
        f[4:6]=['нужно','указать']
        self.assertEqual(m.reference(f)[1][4:7],'SSS')

    def test_word_type_uses_any_false_event_not_first_context(self):
        label={'gold':'K','shared':False}
        safe=m.event_mask(label,'K',True)
        unsafe=m.event_mask(label,'C',True)
        combined=safe|unsafe
        self.assertTrue(combined&m.FIELD_BITS['eligibleFalseAuto'])
        self.assertEqual((combined&m.FIELD_BITS['count']).bit_count(),1)
        self.assertFalse(safe&m.FIELD_BITS['eligibleFalseAuto'])

    def test_architecture_selection_and_recall_loss_fallback(self):
        import copy
        report={'methods':{}}
        for name,rate in [('safeFull',0.98),('safeConfirmed',0.90),('contextConfirmed',0.90),('hybrid',0.90),('contextHybrid',0.90)]:
            report['methods'][name]={}
            for scope in ['holdout/en','holdout/ru','holdout/pooled']:
                unit={'valid':{'falseAuto':0,'falseSuggest':0},
                      'single':{'ordinaryRecoveryRate':rate,'correctSuggest':1},
                      'burst':{'ordinaryRecoveryRate':rate,'correctSuggest':1}}
                report['methods'][name][scope]={'occurrences':copy.deepcopy(unit),'wordTypes':copy.deepcopy(unit),
                                              'autoScreen':{'status':'pass'},'proposalScreen':{'status':'pass'}}
        m.architecture(report)
        self.assertEqual(report['architecture']['algorithmCandidate'],'safeConfirmed')
        self.assertFalse(report['architecture']['mandatoryContext'])  # zero measured safety gain
        self.assertFalse(report['architecture']['contextFilterCredited'])
        report['methods']['safeConfirmed']['holdout/ru']['wordTypes']['single']['ordinaryRecoveryRate']=0.85
        m.architecture(report)
        self.assertEqual(report['architecture']['algorithmCandidate'],'manualOnly')

    def test_unicode_alias_not_an_anchor(self):
        self.assertEqual(m.anchor('KIND'),'-')
        self.assertEqual(m.anchor('ЁЛОЧКА'),'R')


if __name__=='__main__':unittest.main()
