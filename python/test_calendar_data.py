import unittest
from datetime import date, datetime, timezone

import duckdb

from miniplot_embedded import CalendarData, CalendarDay, CalendarRange


class CalendarDataTests(unittest.TestCase):
    def test_leap_day_midmonth_and_missing_zero_are_distinct(self):
        def day(n):
            return date(2024, 2, n)

        with duckdb.connect() as db:
            calendar = CalendarData(db)
            rows = calendar.daily(
                [datetime(2024, 2, 29, 7, 45, tzinfo=timezone.utc),
                 datetime(2024, 2, 29, 8, 10, tzinfo=timezone.utc),
                 datetime(2024, 3, 2, 5, 0, tzinfo=timezone.utc)],
                [3, 0, 100],
                displayed=CalendarRange(day(27), date(2024, 3, 3)),
                covered=CalendarRange(day(28), date(2024, 3, 1)),
                timezone_name="America/Los_Angeles",
            )
            self.assertEqual(rows, [
                CalendarDay(day(27), "outside", None, 0),
                CalendarDay(day(28), "observed", 3, 1),
                CalendarDay(day(29), "observed", 0, 1),
                CalendarDay(date(2024, 3, 1), "outside", None, 0),
                CalendarDay(date(2024, 3, 2), "outside", None, 0),
            ])

    def test_daylight_saving_dates_use_business_timezone(self):
        with duckdb.connect() as db:
            calendar = CalendarData(db)
            window = CalendarRange(date(2024, 3, 9), date(2024, 3, 12))
            rows = calendar.daily(
                [datetime(2024, 3, 10, 8, tzinfo=timezone.utc),
                 datetime(2024, 3, 11, 6, 59, tzinfo=timezone.utc),
                 datetime(2024, 3, 11, 7, 0, tzinfo=timezone.utc)], [1, 2, 4],
                displayed=window, covered=window, timezone_name="America/Los_Angeles",
            )
            self.assertEqual([(row.state, row.value, row.observations) for row in rows],
                             [("missing", None, 0), ("observed", 3, 2), ("observed", 4, 1)])
