# SPDX-License-Identifier: MIT
import fcntl,importlib.util,os,tempfile,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('alerts',Path(__file__).resolve().parents[2]/'tools/owner_alert_audio.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class RotationTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.path=Path(self.temp.name)
  for number,data in ((1,b'A'),(2,b'B')):(self.path/f'owner-alert-sound-{number}.mp3').write_bytes(data)
 def tearDown(self):self.temp.cleanup()
 def test_alternates_and_persists_without_audio(self):
  heard=[]
  def player(fd):heard.append(os.read(fd,1));return True
  self.assertEqual([m.play(self.path,player) for _ in range(3)],[1,2,1])
  self.assertEqual(heard,[b'A',b'B',b'A'])
  self.assertEqual((self.path/'owner-alert-sequence').read_bytes(),b'2\n')
 def test_failed_play_does_not_advance(self):
  self.assertIsNone(m.play(self.path,lambda fd:False))
  self.assertEqual(m.play(self.path,lambda fd:True),1)
 def test_state_symlink_never_changes_target(self):
  target=self.path/'private';target.write_bytes(b'untouched')
  (self.path/'owner-alert-sequence').symlink_to(target)
  with self.assertRaises(OSError):m.play(self.path,lambda fd:self.fail('must not play'))
  self.assertEqual(target.read_bytes(),b'untouched')
 def test_concurrent_notification_skips_instead_of_overlapping(self):
  state=self.path/'owner-alert-sequence'
  with state.open('w+') as locked:
   fcntl.flock(locked,fcntl.LOCK_EX|fcntl.LOCK_NB)
   self.assertIsNone(m.play(self.path,lambda fd:self.fail('must not overlap')))
if __name__=='__main__':unittest.main()
