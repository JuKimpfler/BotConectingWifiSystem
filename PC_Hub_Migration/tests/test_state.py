import unittest

from PC_Hub_Migration.hub_core.config import HubConfig
from PC_Hub_Migration.hub_core.protocol import Heartbeat, TelemetryValue
from PC_Hub_Migration.hub_core.state import HubState


class StateTests(unittest.TestCase):
    def setUp(self):
        self.state = HubState(HubConfig())

    def test_updates_track_min_max(self):
        self.state.update_telemetry('SAT1', '127.0.0.1', 5006, TelemetryValue('Speed', 0, 100, 10))
        self.state.update_telemetry('SAT1', '127.0.0.1', 5006, TelemetryValue('Speed', 0, 80, 20))
        self.state.update_telemetry('SAT1', '127.0.0.1', 5006, TelemetryValue('Speed', 0, 140, 30))
        snapshot = self.state.snapshot()
        speed = snapshot['satellites']['SAT1']['streams']['Speed']
        self.assertEqual(speed['current'], 140)
        self.assertEqual(speed['min'], 80)
        self.assertEqual(speed['max'], 140)

    def test_heartbeat_marks_endpoint(self):
        self.state.update_heartbeat('SAT2', '10.0.0.5', 5006, Heartbeat(uptime_ms=2000, rssi=-50, queue_len=1))
        snapshot = self.state.snapshot()
        sat = snapshot['satellites']['SAT2']
        self.assertEqual(sat['host'], '10.0.0.5')
        self.assertEqual(sat['queue_len'], 1)


if __name__ == '__main__':
    unittest.main()
