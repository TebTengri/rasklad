import importlib.util
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
s=importlib.util.spec_from_file_location('naturalprep',ROOT/'tools/prepare_natural_context.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)


class NaturalPreparationTests(unittest.TestCase):
    def test_causal_prefix_and_blank_boundary(self):
        rows=list(m.natural_tokens('first words here\nnext word\n\nnew paragraph'))
        self.assertEqual(rows[0][2],['','','',''])
        self.assertEqual(rows[3][2],['','first','words','here'])
        self.assertEqual(rows[5][2],['','','',''])

    def test_protected_resets_both_sides(self):
        rows=list(m.natural_tokens('before words `literal code` after words\n```\nprotected value\n```\nfinish word'))
        after=next(x for x in rows if x[0]=='after')
        finish=next(x for x in rows if x[0]=='finish')
        self.assertEqual(after[2],['','','',''])
        self.assertEqual(finish[2],['','','',''])
        self.assertTrue(all(x[2]==['','','',''] for x in rows if x[1]=='protected'))

    def test_long_token_becomes_prefix_barrier_not_skipped(self):
        rows=list(m.natural_tokens('before '+('a'*33)+' after'))
        self.assertEqual(rows[-1][2],['','','before',''])

    def test_exact_alphabets_no_unicode_aliases(self):
        self.assertTrue(m.native('Hello',0))
        self.assertTrue(m.native('Ёжик',1))
        self.assertFalse(m.native('KIND',0))
        self.assertFalse(m.native('інакше',1))


if __name__=='__main__':unittest.main()
