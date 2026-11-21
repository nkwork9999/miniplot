import unittest

import duckdb

from miniplot_embedded import DensityCell, ScatterDensity, ScatterExtent


class ScatterDensityTests(unittest.TestCase):
    def test_counts_conserve_boundary_points_and_drop_null_pairs(self):
        with duckdb.connect() as db:
            density = ScatterDensity(db)
            x = [0, 5, 10, 3, None, 3, -1, 11]
            y = [0, 5, 10, 7, 3, None, 0, 0]
            result = density.fetch(x, y, ScatterExtent(0, 0, 10, 10), columns=2, rows=2, point_limit=2)
            self.assertEqual(result.visible_count, 4)
            self.assertEqual(result.cells, (DensityCell(0, 0, 1), DensityCell(0, 1, 1), DensityCell(1, 1, 2)))
            self.assertEqual(result.points, ())
            zoom = density.fetch(x, y, ScatterExtent(2, 4, 6, 8), columns=2, rows=2, point_limit=2)
            self.assertEqual(zoom.visible_count, 2)
            self.assertEqual(zoom.points, ((5.0, 5.0), (3.0, 7.0)))
            self.assertEqual(zoom.cells, ())
            with self.assertRaises(ValueError):
                density.fetch([0, 1], [0], ScatterExtent(0, 0, 10, 10), columns=2, rows=2, point_limit=2)
