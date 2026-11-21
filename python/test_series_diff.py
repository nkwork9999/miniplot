import json
import os
import re
import unittest
from datetime import datetime

import duckdb

from miniplot_embedded import SeriesPoint, TimeSeriesDiff


class SeriesDiffTests(unittest.TestCase):
    def test_missing_rows_and_zero_baselines_have_distinct_contracts(self):
        t = datetime(2026, 1, 1)
        with duckdb.connect() as db:
            comparator = TimeSeriesDiff(db)
            old = [SeriesPoint(t, 4, None), SeriesPoint(t, 1, 0), SeriesPoint(t, 2, 10), SeriesPoint(t, 3, 20)]
            new = [SeriesPoint(t, 1, 4), SeriesPoint(t, 2, 15), SeriesPoint(t, 4, 3), SeriesPoint(t, 5, 8)]
            rows = comparator.compare(old, new)
            self.assertEqual([(r.key, r.state, r.delta, r.ratio) for r in rows], [
                (1, "zero_baseline", 4, None),
                (2, "matched", 5, 0.5),
                (3, "removed", None, None),
                (4, "missing_value", None, None),
                (5, "added", None, None),
            ])
            self.assertEqual(comparator.compare([], []), [])
            with self.assertRaises(ValueError):
                comparator.compare(old + [old[0]], new)

    def test_report_keeps_undefined_relative_changes_empty(self):
        extension = os.getenv("MINIPLOT_TEST_EXTENSION")
        with duckdb.connect(config={"allow_unsigned_extensions": bool(extension)}) as db:
            db.load_extension(extension or "miniplot")
            time = datetime(2026, 1, 1)
            old = [SeriesPoint(time, 1, 0), SeriesPoint(time, 2, 5)]
            new = [SeriesPoint(time, 1, 7), SeriesPoint(time, 2, 10)]
            html = TimeSeriesDiff(db).chart(old, new, title="Revision comparison")
        match = re.search(r'<script[^>]*id="miniplot-report"[^>]*>(.*?)</script>', html, re.DOTALL)
        self.assertIsNotNone(match)
        sections = json.loads(match[1])["sections"]
        self.assertEqual(sections[1]["data"][0]["y"], [7.0, 5.0])
        self.assertEqual(sections[2]["data"][0]["y"], [None, 100.0])
