import importlib.util
import unittest
from pathlib import Path
p=Path(__file__).with_name('qa12_ge_fault_model.py')
spec=importlib.util.spec_from_file_location('qa12_ge',p)
g=importlib.util.module_from_spec(spec)
import sys
sys.modules[spec.name]=g
spec.loader.exec_module(g)

class BaselineRegression(unittest.TestCase):
    def test_01_normal_present(self):
        e=g.Engine();self.assertEqual(e.submit(),1);e.ge_complete(1);self.assertTrue(e.display(1,True));self.assertEqual(e.accepted_seq,1)
    def test_02_false_display_ack_baseline(self):
        e=g.Engine();e.submit();e.ge_complete(1);e.display(1,False)
        self.assertEqual(e.shown_seq,1);self.assertEqual(e.accepted_seq,0)
    def test_03_strict_display_rejected(self):
        e=g.Engine(strict=True);e.submit();e.ge_complete(1)
        self.assertFalse(e.display(1,False));self.assertEqual(e.shown_seq,0);self.assertEqual(e.fault,'DISPLAY_REJECT')
    def test_04_watchdog_allows_unretired_baseline(self):
        e=g.Engine();e.submit();e.watchdog(1000);e.submit();e.watchdog(1000);e.submit()
        self.assertIn('UNRETIRED_BANK_OVERWRITE',e.events)
    def test_05_strict_watchdog_stops_without_reuse(self):
        e=g.Engine(strict=True);e.submit();self.assertFalse(e.watchdog(1000));self.assertFalse(e.submit());self.assertEqual(len(e.jobs),1)
    def test_06_watchdog_threshold_under_1s(self):
        e=g.Engine(strict=True);e.submit();self.assertFalse(e.watchdog(250));self.assertEqual(e.fault,'');self.assertEqual(e.events[-1],'TRACE_ONLY')
    def test_07_finished_but_display_late_remains_distinct(self):
        e=g.Engine(strict=True);e.submit();e.ge_complete(1)
        self.assertTrue(e.jobs[1].ge_finished);self.assertFalse(e.jobs[1].display_accepted)
    def test_08_gu_start_failure_baseline_ignored(self):
        e=g.Engine();self.assertTrue(e.start(False));self.assertIn('UNACKNOWLEDGED_GU_START_FAILURE',e.events)
    def test_09_gu_start_failure_strict_no_submit(self):
        e=g.Engine(strict=True);self.assertFalse(e.start(False));self.assertFalse(e.submit());self.assertEqual(e.next_seq,0)
    def test_10_serial_sync_error_baseline_ignored(self):
        e=g.Engine();self.assertTrue(e.serial_finish('ERROR'));self.assertIn('UNCONFIRMED_SERIAL_REUSE',e.events)
    def test_11_serial_sync_error_strict_blocks(self):
        e=g.Engine(strict=True);self.assertFalse(e.serial_finish('ERROR'));self.assertFalse(e.submit())
    def test_12_serial_success_keeps_throughput_candidate(self):
        e=g.Engine(strict=True);self.assertTrue(e.serial_finish('SUCCESS'));self.assertTrue(e.start(True));self.assertEqual(e.submit(),1)
    def test_13_exact_capacity(self):
        self.assertTrue(g.budget(4,65531));self.assertFalse(g.budget(4,65532))
    def test_14_negative_request(self):self.assertFalse(g.budget(-1,0))
    def test_15_integer_overflow_boundary(self):
        self.assertFalse(g.budget(2**31-1,0));self.assertFalse(g.budget(2**31,0))
    def test_16_zero_size_still_reserves_commands(self):
        self.assertTrue(g.budget(0,65532));self.assertFalse(g.budget(0,65533))
    def test_17_finish_end_reserved(self):
        self.assertFalse(g.budget(0,65534));self.assertFalse(g.budget(16,65531))
    def test_18_unavailable_list_space(self):self.assertFalse(g.budget(4,-1))
    def test_19_pipeline_acceptance_snapshot(self):
        e=g.Engine(strict=True);self.assertEqual(e.submit(),1);e.ge_complete(1)
        self.assertTrue(e.display(1,True));self.assertEqual(e.shown_seq,e.accepted_seq)
    def test_20_per_bank_finished_reuse(self):
        e=g.Engine(strict=True);e.submit();e.ge_complete(1);e.display(1,True)
        self.assertEqual(e.submit(),2);e.ge_complete(2);e.display(2,True)
        self.assertEqual(e.submit(),3)

if __name__=='__main__':unittest.main(verbosity=2)
