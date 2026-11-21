import json
import os
import re
import unittest

import duckdb

from miniplot_embedded import SankeyFlow


class SankeyFlowTests(unittest.TestCase):
    def test_flow_sums_and_drilldown_keep_small_links_and_distinct_stages(self):
        artifact = os.getenv("MINIPLOT_TEST_EXTENSION")
        with duckdb.connect(config={"allow_unsigned_extensions": bool(artifact)}) as db:
            db.load_extension(artifact or "miniplot")
            db.execute("""
                CREATE TABLE movements(stage INTEGER, source VARCHAR, destination VARCHAR, amount DOUBLE);
                INSERT INTO movements VALUES
                    (0, 'A', 'A', 3), (0, 'A', 'A', 5), (0, 'A', 'B', 2), (0, 'A', 'Other', 1),
                    (1, 'A', 'C', 7), (1, 'B', 'C', 2), (0, 'A', 'B', -4),
                    (1, 'B', 'C', NULL);
            """)
            river = SankeyFlow(db, "movements")
            links = river.summarize(minimum=3)
            self.assertEqual(links.invalid_rows, 2)
            self.assertEqual(sum(link.weight for link in links.links), 20)
            self.assertEqual(links.stage_totals, ((0, 11), (1, 9)))
            low = next(link for link in links.links if link.collapsed and link.source == "A")
            expanded = river.expand(low.stage, low.source, minimum=3)
            self.assertEqual([(r.destination, r.weight) for r in expanded], [("B", 2), ("Other", 1)])
            self.assertEqual(sum(r.weight for r in expanded), low.weight)
            html = river.chart(title="Flow over time", minimum=3)
        match = re.search(r'<script[^>]*id="miniplot-report"[^>]*>(.*?)</script>', html, re.DOTALL)
        self.assertIsNotNone(match)
        trace, = json.loads(match[1])["sections"][0]["data"]
        self.assertEqual(trace["type"], "sankey")
        self.assertTrue(all(a != b for a, b in zip(trace["link"]["source"], trace["link"]["target"])))
