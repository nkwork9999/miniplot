import json
import os
import re
import unittest
from datetime import datetime, timedelta

import duckdb
from miniplot_embedded import BucketSize, ChartBuilder, LineChartSpec, TimeSeriesChart, TimeSeriesDataset


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

    def test_registered_relation_matches_array_chart_with_extra_columns_hidden(self):
        self.db.execute("""
            CREATE TABLE "daily points"(sequence BIGINT PRIMARY KEY, observed TIMESTAMP, sales DOUBLE, private VARCHAR);
            INSERT INTO "daily points"
            SELECT i, TIMESTAMP '2026-01-01' + i * INTERVAL 1 SECOND,
                   [0, 80, 1, -4, 1, 0, 90][i+1], 'internal'
            FROM range(7) AS t(i);
        """)
        builder = ChartBuilder(self.db, {"daily": TimeSeriesDataset("daily points", "observed", "sequence",
                                                                      {"sales": "sales"})})
        start = datetime(2026, 1, 1)
        end = start + timedelta(seconds=6)
        spec = LineChartSpec("daily", "sales", start, end, width=1, title="Bob's <chart>")
        html = builder.line(spec)
        self.assertIn("Bob&#39;s &lt;chart&gt;", html)
        self.assertNotIn("internal", html)
        encoded = re.search(r"var data = JSON.parse\('(.*?)'\);", html).group(1)
        self.assertEqual(json.loads(encoded.replace(r'\"', '"'))[0],
                         self.draw([0, 1, 2, 3, 4, 5], [0, 80, 1, -4, 1, 0], 1))
        with self.assertRaises(KeyError):
            builder.line(LineChartSpec("daily", "private", start, end, width=1, title="Hidden"))

    def test_multiple_series_share_buckets_and_keep_missing_values_per_series(self):
        self.db.execute("""
            CREATE TABLE wide(sequence BIGINT, observed TIMESTAMP, arrivals DOUBLE, departures DOUBLE);
            INSERT INTO wide VALUES
              (1, TIMESTAMP '2026-01-01 01:00:00', 3, NULL),
              (2, TIMESTAMP '2026-01-01 01:12:00', 7, NULL),
              (3, TIMESTAMP '2026-01-01 02:00:00', NULL, 20),
              (4, TIMESTAMP '2026-01-01 02:49:00', 8, 40);
        """)
        builder = ChartBuilder(self.db, {"counts": TimeSeriesDataset("wide", "observed", "sequence",
                                                                       {"a": "arrivals", "d": "departures"})})
        output = builder.bucketed_series("counts", ("a", "d"), self.start, self.start + timedelta(days=1),
                                         size=BucketSize.HOUR)
        self.assertEqual([(row.bucket.hour, row.series_id, row.mean, row.observations) for row in output],
                         [(1, "a", 5, 2), (1, "d", None, 0), (2, "a", 8, 1), (2, "d", 30, 2)])
        with self.assertRaises(KeyError):
            builder.bucketed_series("counts", ("hidden",), self.start, self.start + timedelta(days=1),
                                    size=BucketSize.HOUR)
