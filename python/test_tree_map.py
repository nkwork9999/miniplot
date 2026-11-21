import json
import os
import re
import unittest

import duckdb

from miniplot_embedded import HierarchyNode, TreeMap


class TreeMapTests(unittest.TestCase):
    def test_rolls_up_all_leaves_below_the_visible_depth(self):
        nodes = [
            HierarchyNode("all", None, "All"),
            HierarchyNode("north", "all", "North"), HierarchyNode("south", "all", "South"),
            HierarchyNode("a", "north", "One", 3), HierarchyNode("b", "north", "Two", 5),
            HierarchyNode("c", "south", "One", 7),
        ]
        with duckdb.connect() as db:
            tree = TreeMap(db)
            rows = tree.summarize(nodes, root="all", max_depth=1)
            self.assertEqual([(row.key, row.parent, row.depth, row.total) for row in rows], [
                ("all", None, 0, 15), ("north", "all", 1, 8), ("south", "all", 1, 7),
            ])
            branch = tree.summarize(nodes, root="north", max_depth=0)
            self.assertEqual([(row.key, row.parent, row.total) for row in branch], [("north", None, 8)])
            with self.assertRaises(ValueError):
                tree.summarize(nodes + [HierarchyNode("a", "south", "Child", 4)], root="all", max_depth=3)
            cycle = [HierarchyNode("first", "second", "First"), HierarchyNode("second", "first", "Second")]
            with self.assertRaises(ValueError):
                tree.summarize(cycle, root="first", max_depth=3)

    def test_report_contains_only_visible_totals(self):
        extension = os.getenv("MINIPLOT_TEST_EXTENSION")
        nodes = [HierarchyNode("root", None, "Root"), HierarchyNode("child", "root", "Child", 3)]
        with duckdb.connect(config={"allow_unsigned_extensions": bool(extension)}) as db:
            db.load_extension(extension or "miniplot")
            html = TreeMap(db).chart(nodes, root="root", max_depth=0, title="Department totals")
        match = re.search(r'<script[^>]*id="miniplot-report"[^>]*>(.*?)</script>', html, re.DOTALL)
        self.assertIsNotNone(match)
        trace, = json.loads(match[1])["sections"][0]["data"]
        self.assertEqual((trace["ids"], trace["values"]), (["root"], [3.0]))
