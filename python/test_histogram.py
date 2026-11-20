import unittest

import duckdb
from miniplot_embedded import Histogram


class HistogramTests(unittest.TestCase):
    def test_boundaries_empty_bins_missing_and_outside(self):
        with duckdb.connect() as db:
            histogram = Histogram(db)
            result = histogram.count([-3, -2, -2, 0, 2, 4, None], [-3, -2, 0, 1, 2])
            self.assertEqual(result.counts, (1, 2, 1, 1))
            self.assertEqual((result.missing, result.outside), (1, 1))
            self.assertEqual(histogram.count([2, 2], [0, 1, 2]).counts, (0, 2))
            self.assertEqual(histogram.count([], [0, 1, 2]).counts, (0, 0))

    def test_invalid_edges_and_nonfinite_samples(self):
        with duckdb.connect() as db:
            histogram = Histogram(db)
            with self.assertRaises(ValueError):
                histogram.count([2], [0, 0, 2])
            with self.assertRaises(ValueError):
                histogram.count([float("inf")], [0, 2])
