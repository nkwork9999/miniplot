import unittest

import duckdb

from miniplot_embedded import VoxelGrid, VoxelSpec


class VoxelGridTests(unittest.TestCase):
    def test_centroids_conserve_points_and_selected_cell_can_be_paged(self):
        with duckdb.connect() as db:
            db.execute("""
                CREATE TABLE points(id BIGINT PRIMARY KEY, x DOUBLE, y DOUBLE, z DOUBLE, color DOUBLE);
                INSERT INTO points VALUES
                    (1, 0, 0, 0, 4), (2, 0.2, 0.4, 0.8, NULL),
                    (3, 0.5, 0.5, 1, 10), (4, 1, 1, 1, 20),
                    (5, 1.1, 0, 0, 2), (6, NULL, 0, 0, 2),
                    (7, 'NaN'::DOUBLE, 0, 0, 2);
            """)
            grid = VoxelGrid(db, "points")
            spec = VoxelSpec((0, 0, 0), (1, 1, 1), (2, 2, 1))
            low, high = grid.summarize(spec)
            self.assertEqual((low.cell, low.count, low.center, low.mean_color),
                             ((0, 0, 0), 2, (0.1, 0.2, 0.4), 4))
            self.assertEqual((high.cell, high.count, high.center, high.mean_color),
                             ((1, 1, 0), 2, (0.75, 0.75, 1), 15))
            self.assertEqual([p.point_id for p in grid.points(spec, high.cell, limit=1)], [3])
            point, = grid.points(spec, high.cell, after_id=3, limit=1)
            self.assertEqual((point.point_id, point.position, point.color), (4, (1, 1, 1), 20))
            self.assertEqual(grid.points(spec, (1, 0, 0), limit=1), [])
            with self.assertRaises(ValueError):
                grid.points(spec, (1, 2, 0), limit=1)
