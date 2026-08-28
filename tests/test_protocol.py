import binascii
import unittest

from pulseforge_sim.protocol import Sample, StreamCorruptionError, fingerprint, frame_size


class ProtocolTests(unittest.TestCase):
    def test_frame_is_fixed_width_and_round_trips(self) -> None:
        original = Sample(10_000, 7, -123, 24_993, 1).with_crc()
        frame = original.pack()
        self.assertEqual(frame_size(), 32)
        self.assertEqual(len(frame), 32)
        self.assertEqual(Sample.unpack(frame), original)

    def test_corruption_is_detected(self) -> None:
        frame = bytearray(Sample(1, 2, 3, 4, 0).pack())
        frame[12] ^= 0x40
        with self.assertRaises(StreamCorruptionError):
            Sample.unpack(bytes(frame))

    def test_crc_matches_standard_ieee_crc32(self) -> None:
        sample = Sample(1, 2, 3, 4, 5).with_crc()
        self.assertEqual(sample.crc32, binascii.crc32(sample.payload()) & 0xFFFFFFFF)

    def test_fingerprint_depends_on_order(self) -> None:
        first = Sample(0, 0, 1, 2, 0)
        second = Sample(1, 1, 2, 3, 0)
        self.assertNotEqual(fingerprint([first, second]), fingerprint([second, first]))


if __name__ == "__main__":
    unittest.main()

