import unittest

import duckdb

from miniplot_embedded import GridWindow, SparseHeatmap


class SparseHeatmapTests(unittest.TestCase):
    def test_sparse_and_dense_windows_distinguish_actual_zero_missing_and_outside(self):
        with duckdb.connect() as db:
            db.execute("""
                CREATE TABLE bins(xbin BIGINT, ybin BIGINT, value DOUBLE, PRIMARY KEY (xbin, ybin));
                INSERT INTO bins VALUES (0,0,0), (0,1,NULL), (1,0,5), (10,0,200)
            """)
            heatmap = SparseHeatmap(db, "bins", columns=2, rows=2)
            window = GridWindow(-1, 2, 0, 2)
            sparse = heatmap.sparse(window)
            self.assertEqual([(c.x, c.y, c.state, c.value) for c in sparse], [
                (0, 0, "observed", 0), (1, 0, "observed", 5), (0, 1, "missing_value", None),
            ])
            dense = heatmap.dense(window)
            self.assertEqual([(c.x, c.y, c.state, c.value) for c in dense], [
                (-1, 0, "outside", None), (0, 0, "observed", 0), (1, 0, "observed", 5),
                (-1, 1, "outside", None), (0, 1, "missing_value", None), (1, 1, "unobserved", None),
            ])
            self.assertEqual(heatmap.sparse(GridWindow(5, 8, 9, 10)), [])
            with self.assertRaises(ValueError):
                heatmap.dense(GridWindow(0, 1024, 0, 1024))
