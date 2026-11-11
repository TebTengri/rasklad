"""Independent synthetic checks for the experimental data boundary, no corpus."""
import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('prep', Path(__file__).resolve().parents[2] / 'tools/prepare_technical_evaluation.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


class PreparationTests(unittest.TestCase):
    def test_fences_and_tildes(self):
        self.assertEqual(list(p.spans('hello\n```rust\nworld\n```\n~~~\nother\n~~~\nend')),
                         [('valid', 'hello'), ('protected', 'world'), ('protected', 'other'), ('valid', 'end')])

    def test_fence_marker_and_length(self):
        self.assertEqual(list(p.spans('````\na\n```\n~~~\nb\n````')),
                         [('protected', 'a'), ('protected', '```'), ('protected', '~~~'), ('protected', 'b')])

    def test_inline_run_and_unmatched(self):
        self.assertEqual(list(p.inline_spans('a ``b`c`` d `unclosed')),
                         [('valid', 'a '), ('protected', 'b`c'), ('valid', ' d `unclosed')])

    def test_borrowed_english_and_protected_not_corrupted(self):
        rows = list(p.rows_for_document(20, 'ru', 'Rust `hello` слово'))
        self.assertEqual([r[2] for r in rows], ['valid', 'wrong', 'protected', 'valid', 'wrong'])
        self.assertEqual([r[3] for r in rows], ['us', 'ru', 'us', 'ru', 'us'])
        self.assertEqual(rows[1][4], 'Кгые')
        self.assertEqual(rows[-1][4], 'ckjdj')

    def test_case_and_russian_punctuation(self):
        for word in ['Hello', 'ПРИВЕТ', 'любовь', 'ёжик']:
            rows = list(p.rows_for_document(0, 'en', word))
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[1][-1], word)
        self.assertEqual(list(p.rows_for_document(0, 'ru', 'любовь'))[1][4], 'k.,jdm')

    def test_mixed_and_long_not_fabricated(self):
        rows = list(p.rows_for_document(0, 'ru', 'Rustслово ' + 'a' * 33))
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(r[2] == 'valid' for r in rows))
        with self.assertRaises(ValueError):
            list(p.rows_for_document(0, 'en', 'a' * 4097))


if __name__ == '__main__':
    unittest.main()
