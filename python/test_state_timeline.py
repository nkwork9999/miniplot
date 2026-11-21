import unittest
from datetime import datetime, timedelta

import duckdb

from miniplot_embedded import StateInterval, StateTimeline


class StateTimelineTests(unittest.TestCase):
    def test_compresses_equal_runs_and_preserves_gaps_and_boundaries(self):
        start = datetime(2026, 1, 1)
        def moment(minute):
            return start + timedelta(minutes=minute)
        with duckdb.connect() as db:
            timeline = StateTimeline(db)
            rows = timeline.reduce(
                [moment(i) for i in [-1, 0, 1, 1, 2, 3, 9, 10, 11]],
                ["ready", "ready", "discard by stable tie", "busy", "busy", None, "ready", "ready", "other"],
                start=start, end=moment(11), max_gap=timedelta(minutes=2),
            )
            self.assertEqual(rows, [
                StateInterval(start, moment(1), "ready"),
                StateInterval(moment(1), moment(3), "busy"),
                StateInterval(moment(9), moment(11), "ready"),
            ])
            gaps = timeline.reduce([moment(0), moment(8)], ["ready", "ready"],
                                   start=start, end=moment(12), max_gap=timedelta(minutes=2))
            self.assertEqual(gaps, [StateInterval(start, moment(2), "ready"),
                                    StateInterval(moment(8), moment(10), "ready")])
