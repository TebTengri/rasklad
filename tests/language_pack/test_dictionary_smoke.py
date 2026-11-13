# SPDX-License-Identifier: MIT
"""Only malformed/project-authored stdin; no real dictionaries or models."""
from pathlib import Path
import json
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[2]
NATIVE=ROOT/'build/browser-spell-native/native_spell_fixture'

@unittest.skipUnless(NATIVE.is_file(),'Run bin/prepare-browser-spell first')
class DictionaryOracleGuards(unittest.TestCase):
    def test_bad_protocol_cannot_fall_back_to_default_success_fixture(self):
        for raw in [b'{}',b'{',json.dumps({'schema':1,'profiles':{'en':{},'ru':{}}}).encode()]:
            with self.subTest(length=len(raw)):
                r=subprocess.run([str(NATIVE),'--stdin-profiles'],input=raw,capture_output=True,timeout=5)
                self.assertEqual(r.returncode,1)
                self.assertEqual(r.stdout,b'')

    def test_unknown_mode_cannot_open_default_fixture(self):
        r=subprocess.run([str(NATIVE),'--unknown'],capture_output=True,timeout=5)
        self.assertEqual(r.returncode,1)
        self.assertEqual(r.stdout,b'')

if __name__=='__main__':unittest.main()
