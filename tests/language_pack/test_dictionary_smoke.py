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

    def test_large_fictional_profiles_are_read_to_eof_from_pipe(self):
        import base64
        fixture=json.loads((ROOT/'build/browser-spell-native/native.json').read_text())
        words=['zorb'+chr(97+i//676)+chr(97+(i//26)%26)+chr(97+i%26) for i in range(5000)]
        dic=(str(len(words))+'\n'+'\n'.join(words)+'\n').encode()
        request={'schema':1,'profiles':{}}
        for lang,p in fixture['profiles'].items():
            request['profiles'][lang]={'affBase64':p['affBase64'],'dicBase64':base64.b64encode(dic).decode(),
                                      'words':[words[0],words[-1],'absentfictional']}
        raw=json.dumps(request).encode()
        self.assertGreater(len(raw),65536)
        result=subprocess.run([str(NATIVE),'--stdin-profiles'],input=raw,capture_output=True,timeout=10)
        self.assertEqual(result.returncode,0)
        profiles=json.loads(result.stdout)['profiles']
        for p in profiles.values():
            self.assertTrue(p['cases'][0]['full'])
            self.assertEqual(base64.b64decode(p['dicBase64']),dic)

    def test_unknown_mode_cannot_open_default_fixture(self):
        r=subprocess.run([str(NATIVE),'--unknown'],capture_output=True,timeout=5)
        self.assertEqual(r.returncode,1)
        self.assertEqual(r.stdout,b'')

if __name__=='__main__':unittest.main()
