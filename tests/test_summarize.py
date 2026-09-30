import csv
import io
from pathlib import Path
import unittest
from unittest.mock import patch

from summarize import summarize


def group(sample=1, mode=1, source=1, **overrides):
    base = dict(sample=sample, hand=0, mode=mode, xr_time=sample * 100_000_000,
                locate_result=0, hand_active=1, source_active=1, source=source,
                focused=1, multi_result=0, sync_result=0, grip_active=1,
                grip_flags=3, trigger_active=1, trigger=.5, squeeze_active=1,
                squeeze=0, flags=2, y=0, z=0)
    base.update(overrides)
    return [dict(base, joint=j, x=j / 100) for j in range(26)]


class SummaryTest(unittest.TestCase):
    def report(self, rows):
        stream = io.StringIO()
        writer = csv.DictWriter(stream, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)
        with patch.object(Path, 'open', return_value=io.StringIO(stream.getvalue())):
            return summarize(Path('capture.csv'))

    def test_camera_only_with_controller_auto(self):
        result = self.report(group(mode=0, source=2) + group())
        self.assertEqual(result['source_samples']['LEFT AUTO'], {'controller': 1})
        self.assertEqual(result['camera_with_active_valid_controller_pose'], {'LEFT CAMERA_ONLY': 1})
        self.assertEqual(result['camera_with_active_controller_and_button_pressed'], {'LEFT CAMERA_ONLY': 1})
        self.assertEqual(result['camera_only_while_same_hand_auto_uses_controller'], {'LEFT CAMERA_ONLY': 1})

    def test_invalid_or_unfocused_samples_are_not_overlap(self):
        for overrides in [dict(focused=0), dict(source_active=0), dict(grip_flags=2),
                          dict(sync_result=-1), dict(multi_result=-1), dict(flags=0),
                          dict(hand_active=0), dict(locate_result=-1)]:
            with self.subTest(overrides=overrides):
                self.assertEqual(self.report(group(**overrides))['camera_with_active_valid_controller_pose'], {})

    def test_incomplete_group_is_skipped(self):
        result = self.report(group() + group(sample=2)[:13])
        self.assertEqual(result['incomplete_groups_skipped'], 1)
        self.assertEqual(result['source_samples']['LEFT CAMERA_ONLY'], {'camera': 1})


if __name__ == '__main__':
    unittest.main()
