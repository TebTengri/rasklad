# SPDX-License-Identifier: MIT
import copy
import unittest
from model import ScreeningModel, Outcome, TERMINAL, QUEUE_CAP

class HandoffTests(unittest.TestCase):
    def active(self):
        m=ScreeningModel();e=m.offer('A','g1');m.ack(e,'A','g1');return m

    def test_queued_pair_waits_for_exact_ack(self):
        m=ScreeningModel();e=m.offer('A','g1')
        m.key('K','press');m.key('K','release')
        for wrong in [(e,'B','g1'),(e,'A','g2'),(e-1,'A','g1')]:
            m.ack(*wrong);self.assertEqual(m.trace,[]);self.assertFalse(m.acked)
        m.ack(e,'A','g1')
        self.assertEqual(m.trace,[('A','K','press'),('A','K','release')])
        self.assertFalse(m.delivered)

    def test_same_target_new_grab_before_queued_press_delivery(self):
        m=ScreeningModel();old=m.offer('A','g1');m.key('K','press')
        new=m.offer('A','g2');m.ack(old,'A','g1');self.assertFalse(m.trace)
        m.ack(new,'A','g2');m.key('K','release')
        self.assertEqual(len(m.trace),2);self.assertFalse(m.delivered)

    def test_persistent_same_grab_preserves_held_pair(self):
        m=self.active();m.key('K','press');old=m.epoch
        new=m.offer('A','g1');m.key('K','release')
        m.ack(old,'A','g1');self.assertEqual(len(m.trace),1)
        m.ack(new,'A','g1');self.assertEqual(len(m.trace),2)
        self.assertFalse(m.delivered)

    def test_delivered_press_cannot_silently_move_to_new_grab(self):
        m=self.active();m.key('K','press');m.offer('A','g2')
        self.assertEqual(m.outcome(),Outcome.UNSUPPORTED_TRANSFER)
        self.assertEqual(m.trace,[('A','K','press')])

    def test_queued_switch_rejects_without_replay(self):
        m=ScreeningModel();m.offer('A','g1');m.key('K','press');m.offer('B','g2')
        self.assertEqual(m.outcome(),Outcome.UNSUPPORTED_QUEUED_SWITCH)
        self.assertEqual(m.trace,[])

    def test_held_switch_rejects_before_new_target_acceptance(self):
        m=self.active();m.key('K','press');m.offer('B','g2')
        m.ack(m.epoch,'B','g2');m.key('M','press');m.key('K','release')
        self.assertEqual(m.outcome(),Outcome.UNSUPPORTED_HELD_SWITCH)
        self.assertEqual(m.trace,[('A','K','press')])

    def test_overflow_rejects_without_flushing(self):
        m=ScreeningModel();m.offer('A','g1')
        for _ in range(2):m.key('K','press');m.key('K','release')
        m.key('M','press');m.ack(m.epoch,'A','g1')
        self.assertEqual(m.outcome(),Outcome.OVERFLOW)
        self.assertEqual(len(m.queue),QUEUE_CAP);self.assertEqual(m.trace,[])

    def test_no_ack_before_offer_or_boolean_epoch(self):
        m=ScreeningModel();m.ack(0,None,None);self.assertFalse(m.acked)
        m.offer('A','g1');m.ack(True,'A','g1');self.assertFalse(m.acked)

    def test_target_roundtrip_does_not_accept_old_ack(self):
        m=ScreeningModel();first=m.offer('A','g1');m.offer('B','g2')
        current=m.offer('A','g1');m.key('K','press')
        m.ack(first,'A','g1');self.assertFalse(m.trace)
        m.ack(current,'A','g1');self.assertEqual(len(m.trace),1)

    def test_duplicate_press_and_unmatched_release_reject(self):
        for sequence in [('press','press'),('release',),('press','release','release')]:
            m=self.active()
            for state in sequence:m.key('K',state)
            self.assertEqual(m.outcome(),Outcome.REJECTED_INPUT)

    def test_exhaustive_short_traces(self):
        commands=[('offer','A','g1'),('offer','A','g2'),('offer','B','g1'),('offer','B','g2'),
                  ('ack',),('stale',),('key','K','press'),('key','K','release'),
                  ('key','M','press'),('key','M','release')]
        frontier=[ScreeningModel()];checked=0
        for _ in range(4):
            next_frontier=[]
            for before in frontier:
                for action in commands:
                    m=copy.deepcopy(before)
                    if action[0]=='ack':m.ack(m.epoch,m.target,m.grab)
                    elif action[0]=='stale':m.ack(m.epoch-1,m.target,m.grab)
                    else:getattr(m,action[0])(*action[1:])
                    checked+=1
                    self.assertLessEqual(len(m.queue),QUEUE_CAP)
                    self.assertLessEqual(len(m.held),2)
                    if before.state in TERMINAL:self.assertEqual(m.trace,before.trace)
                    if action[0] in ('offer','stale') or (action[0]=='key' and not before.acked):
                        self.assertEqual(m.trace,before.trace)
                    owners={}
                    for target,grab,key,state in m.delivery_bindings:
                        if state=='press':
                            self.assertNotIn(key,owners);owners[key]=(target,grab)
                        else:
                            self.assertEqual(owners.pop(key,None),(target,grab))
                    self.assertEqual(owners,m.delivered)
                    self.assertEqual([(t,k,s) for t,g,k,s in m.delivery_bindings],m.trace)
                    next_frontier.append(m)
            frontier=next_frontier
        self.assertEqual(checked,11110)

if __name__=='__main__':unittest.main()
