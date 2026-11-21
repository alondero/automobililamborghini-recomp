import copy
import unittest

from tools.benchmark_circuit4 import completed_workload


class CompletionTests(unittest.TestCase):
    def test_incomplete_or_wrong_workload_is_not_a_benchmark(self):
        complete = dict(outcome='passed', reason='replay_complete', loaded_circuit=4,
            warp=dict(applied=True, circuit=4, mode=2),
            replay=dict(active=True, complete=True, failed=False, frames_consumed=900,
                        total_frames=900, guest_frames_verified=900))
        self.assertTrue(completed_workload(complete, 900))
        self.assertFalse(completed_workload({}, 900))
        for field in ('frames_consumed', 'total_frames', 'guest_frames_verified'):
            early = copy.deepcopy(complete)
            early['replay'][field] = 899
            self.assertFalse(completed_workload(early, 900), field)
        for field, value in [('reason', 'window_closed'), ('loaded_circuit', 1)]:
            early = copy.deepcopy(complete)
            early[field] = value
            self.assertFalse(completed_workload(early, 900), field)
        complete['warp']['mode'] = 0  # A time trial has only one car.
        self.assertFalse(completed_workload(complete, 900))
