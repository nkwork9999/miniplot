import unittest
import json
import os
import re

import duckdb

from miniplot_embedded import GroupedStatistics


class GroupedStatisticsTests(unittest.TestCase):
    def setUp(self):
        self.db = duckdb.connect()
        self.addCleanup(self.db.close)
        self.stats = GroupedStatistics(self.db)

    def test_boxplot_reports_quartiles_whiskers_and_outliers(self):
        rows = self.stats.boxplot([1] * 7 + [2, 2] + [3, 4],
                                 [1, 2, 3, 4, 5, 100, 200, 8, 8, 42, None],
                                 outlier_limit=1)
        self.assertEqual([(r.group_id, r.count) for r in rows], [(1, 7), (2, 2), (3, 1), (4, 0)])
        self.assertEqual((rows[0].q1, rows[0].median, rows[0].q3), (2.5, 4.0, 52.5))
        self.assertEqual((rows[0].lower_whisker, rows[0].upper_whisker), (1.0, 100.0))
        self.assertEqual((rows[0].outlier_count, rows[0].outlier_examples), (1, (200.0,)))
        self.assertEqual((rows[1].lower_whisker, rows[1].upper_whisker), (8.0, 8.0))
        self.assertEqual((rows[2].q1, rows[2].q3), (42.0, 42.0))
        self.assertEqual((rows[3].q1, rows[3].outlier_count), (None, 0))
        self.assertEqual(self.stats.boxplot([], []), [])
        self.assertEqual(self.stats.boxplot([1, 1, 1, 1, 1], [0, 0, 0, 0, 5],
                                            outlier_limit=0)[0].outlier_examples, ())

    def test_boxplot_html_contains_precomputed_boxes_and_bounded_outliers(self):
        extension = os.getenv("MINIPLOT_TEST_EXTENSION")
        with duckdb.connect(config={"allow_unsigned_extensions": bool(extension)}) as db:
            db.load_extension(extension or "miniplot")
            chart = GroupedStatistics(db).boxplot_html([1] * 9 + [2], [0] * 7 + [9, 10, None],
                                                       title="Observed distribution", outlier_limit=1)
        match = re.search(r'<script[^>]*id="miniplot-report"[^>]*>(.*?)</script>', chart, re.DOTALL)
        self.assertIsNotNone(match)
        report = json.loads(match[1])
        box, = report["sections"][0]["data"]
        self.assertEqual(box["x"], ["1"])
        self.assertEqual(box["median"], [0])
        self.assertEqual(box["customdata"], [[2, [9]]])
        self.assertNotIn("y", box)

    def test_top_categories_keep_label_collisions_and_preserve_totals(self):
        rows = self.stats.top_categories([9, 1, 2, 1, 8, None], [1, 4, 5, 1, 2, 3],
                                         {1: "same", 2: "same", 8: "other", 9: "minor"}, limit=2)
        self.assertEqual([(r.kind, r.category_id, r.label, r.total) for r in rows], [
            ("category", 1, "same", 5), ("category", 2, "same", 5),
            ("other", None, "Other", 3), ("missing", None, "(Missing)", 3),
        ])
        self.assertEqual(sum(r.total for r in rows), 16)

    def test_degree_distribution_preserves_isolated_vertices_and_self_loops(self):
        rows = self.stats.degree_distribution([1, 2, 3, 4],
                                              [(1, 2, 2), (1, 2, 1), (2, 2, 1), (3, 1, 1)])
        self.assertEqual([(r.direction, r.degree, r.vertices) for r in rows],
                         [("in", 0, 2), ("in", 1, 1), ("in", 4, 1),
                          ("out", 0, 1), ("out", 1, 2), ("out", 3, 1)])
        with self.assertRaises(ValueError):
            self.stats.degree_distribution([1, 2], [(1, 3, 1)])

    def test_weighted_rollup_matches_direct_aggregation(self):
        groups = [1, 2, 2, 2, 3, 3, 4, 4]
        values = [10, 20, 30, None, None, None, 999, 0]
        weights = [1, 2, 1, 100, 1, 1, 0, 0]
        fine = self.stats.weighted_means(groups, values, weights)
        self.assertEqual([(r.group_id, r.weight, r.mean) for r in fine],
                         [(1, 1, 10), (2, 3, 70 / 3), (3, 0, None), (4, 0, None)])
        parent = {1: 100, 2: 100, 3: 200, 4: 200}
        coarse = self.stats.combine_weighted_means(fine, parent)
        direct = self.stats.weighted_means([parent[group] for group in groups], values, weights)
        self.assertEqual(coarse, direct)
        self.assertEqual([(r.group_id, r.mean) for r in coarse], [(100, 20), (200, None)])
        with self.assertRaises(ValueError):
            self.stats.weighted_means([1], [5], [-1])
