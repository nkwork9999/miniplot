import json
import os
import re
import unittest
from datetime import datetime, timedelta

import duckdb
from miniplot_embedded import TimeSeriesChart


class ChartTests(unittest.TestCase):
    def setUp(self):
        artifact = os.getenv("MINIPLOT_TEST_EXTENSION")
        config = {"allow_unsigned_extensions": bool(artifact)}
        if directory := os.getenv("DUCKDB_EXTENSION_DIRECTORY"):
            config["extension_directory"] = directory
        self.db = duckdb.connect(config=config)
        self.addCleanup(self.db.close)
        self.db.load_extension(artifact or "miniplot")
        self.chart = TimeSeriesChart(self.db)
        self.start = datetime(2026, 1, 1)

    def draw(self, seconds, values, width):
        html = self.chart.line([self.start + timedelta(seconds=i) for i in seconds], values,
                               title="Bob's <chart>", width=width)
        self.assertIn("Bob&#39;s &lt;chart&gt;", html)
        encoded = re.search(r"var data = JSON.parse\('(.*?)'\);", html).group(1)
        return json.loads(encoded.replace(r'\"', '"'))[0]

    def test_keeps_peaks_in_time_order_and_respects_display_width(self):
        trace = self.draw([0, 1, 2, 3, 4, 5], [0, 80, 1, -4, 1, 0], 1)
        self.assertEqual(trace["y"], [0, 80, -4, 0])
        self.assertEqual(len(trace["x"]), 4)

    def test_two_buckets_and_equal_timestamps(self):
        self.assertEqual(self.draw([0, 0, 0, 5, 8, 9], [5, 7, 3, 4, 2, 3], 2)["y"],
                         [5, 7, 3, 4, 2, 3])
        self.assertEqual(self.draw([2, 1], [2, 1], 20)["y"], [1, 2])

    def test_rejects_mismatched_nonfinite_and_empty_series(self):
        for times, values, width in [([], [], 1), ([0], [1, 2], 1), ([0], [float("nan")], 1), ([0], [1], 0)]:
            with self.subTest(values=values, width=width), self.assertRaises(ValueError):
                self.draw(times, values, width)
