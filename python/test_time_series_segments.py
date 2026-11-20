from datetime import datetime, timedelta
import unittest

import duckdb
from miniplot_embedded import TimeSeriesChart


class TimeSeriesSegmentsTests(unittest.TestCase):
    def test_gaps_peaks_and_equal_times(self):
        start = datetime(2026, 1, 1)
        times = [start + timedelta(seconds=i) for i in [0, 1, 2, 3, 4, 5, 5, 5, 6, 7]]
        with duckdb.connect() as db:
            chart = TimeSeriesChart(db)
            segments = chart.segments(times, [2, 70, 4, -8, None, 9, 10, 0, None, None], width=1)
            self.assertEqual([[value for _, value in segment] for segment in segments],
                             [[2, 70, -8], [9, 10, 0]])
            self.assertEqual([time for time, _ in segments[1]], [times[5]] * 3)
            self.assertEqual(chart.segments(times, [None] * len(times), width=1), [])
            with self.assertRaises(ValueError):
                chart.segments(times, [float("nan")] * len(times), width=1)
