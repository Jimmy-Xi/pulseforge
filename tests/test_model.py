import unittest

from pulseforge_sim import Config, Simulator, fingerprint
from pulseforge_sim.protocol import STATUS_FREEZE, STATUS_SPIKE


class SimulatorTests(unittest.TestCase):
    def test_same_seed_produces_same_incident(self) -> None:
        config = Config(seed=1234, spike_every=7, drop_every=11, freeze_every=13)
        first = list(Simulator(config).run(100))
        second = list(Simulator(config).run(100))
        self.assertEqual(first, second)
        self.assertEqual(fingerprint(first), fingerprint(second))

    def test_fault_counters_are_exact(self) -> None:
        simulator = Simulator(Config(spike_every=5, drop_every=4, freeze_every=6, noise_milli=0))
        samples = list(simulator.run(60))
        self.assertEqual(simulator.stats.generated, 60)
        self.assertEqual(simulator.stats.dropped, 15)
        self.assertEqual(simulator.stats.delivered, 45)
        self.assertEqual(simulator.stats.spikes, 9)
        self.assertEqual(simulator.stats.freezes, 5)
        self.assertEqual(sum(bool(s.status & STATUS_SPIKE) for s in samples), 9)
        self.assertEqual(sum(bool(s.status & STATUS_FREEZE) for s in samples), 5)

    def test_sequence_gaps_make_drops_observable(self) -> None:
        samples = list(Simulator(Config(drop_every=3)).run(10))
        self.assertEqual([sample.sequence for sample in samples], [0, 1, 3, 4, 6, 7, 9])

    def test_invalid_configuration_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            Simulator(Config(period_us=1))

    def test_zero_seed_uses_nonzero_fallback(self) -> None:
        samples = list(Simulator(Config(seed=0)).run(3))
        self.assertEqual(len({sample.signal_milli for sample in samples}), 3)

    def test_golden_incident_fingerprint(self) -> None:
        config = Config(drop_every=5, spike_every=7, freeze_every=11)
        samples = list(Simulator(config).run(20))
        self.assertEqual(
            fingerprint(samples),
            "23a1c1b6229a59f70d4b69c8a544f3e744586e8c4ca613119f1098c96f106f64",
        )


if __name__ == "__main__":
    unittest.main()
