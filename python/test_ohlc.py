import unittest
from datetime import datetime, timezone

import duckdb

from miniplot_embedded import MarketSession, OHLC, QuoteTrade


def ts(day, hour):
    return datetime(2026, 1, day, hour, tzinfo=timezone.utc)


class MarketBarTests(unittest.TestCase):
    def test_overnight_sessions_and_tied_execution_times(self):
        with duckdb.connect() as db:
            series = OHLC(db)
            sessions = [MarketSession("A", ts(1, 20), ts(2, 4)), MarketSession("B", ts(2, 20), ts(3, 4))]
            trades = [QuoteTrade(ts(1, 23), 2, 12, 3), QuoteTrade(ts(1, 23), 1, 11, 2),
                      QuoteTrade(ts(2, 3), 3, 10, 5), QuoteTrade(ts(2, 4), 4, 912, 40)]
            first, missing = series.bars(sessions, trades)
            self.assertEqual((first.session_id, first.open, first.high, first.low, first.close, first.volume),
                             ("A", 11, 12, 10, 10, 10))
            self.assertEqual((missing.session_id, missing.open, missing.volume, missing.trades),
                             ("B", None, None, 0))
            with self.assertRaises(ValueError):
                series.bars(sessions + [MarketSession("C", ts(2, 0), ts(2, 3))], trades)
