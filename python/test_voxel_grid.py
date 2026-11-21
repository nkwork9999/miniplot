import json
import os
import re
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


    def test_3d_chart_uses_occupied_cells_and_stable_selection_keys(self):
        artifact = os.getenv("MINIPLOT_TEST_EXTENSION")
        with duckdb.connect(config={"allow_unsigned_extensions": bool(artifact)}) as db:
            db.load_extension(artifact or "miniplot")
            db.execute("CREATE TABLE cloud AS SELECT 1::BIGINT AS id, 4e307 AS x, 0.5 AS y, 0.5 AS z, NULL::DOUBLE AS color")
            db.execute("INSERT INTO cloud SELECT i, 4e307, 0.5, 0.5, NULL FROM range(2,12) t(i)")
            grid = VoxelGrid(db, "cloud")
            spec = VoxelSpec((0, 0, 0), (1e308, 1, 1), (2, 2, 2))
            html = grid.chart(spec, title="3D observations")
        match = re.search(r'<script[^>]*id="miniplot-report"[^>]*>(.*?)</script>', html, re.DOTALL)
        self.assertIsNotNone(match)
        trace, = json.loads(match[1])["sections"][0]["data"]
        self.assertEqual(trace["type"], "scatter3d")
        self.assertEqual(trace["customdata"], [[0, 1, 1, 11]])
        self.assertAlmostEqual(trace["x"][0] / 1e307, 4.0)
        self.assertEqual(trace["marker"]["size"], [11])
