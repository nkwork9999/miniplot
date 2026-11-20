"""A time-series HTML chart rendered on a host-provisioned Miniplot connection."""
from datetime import datetime
from math import isfinite

from duckdb import DuckDBPyConnection


class TimeSeriesChart:
    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def line(self, times: list[datetime], values: list[float], *, title: str, width: int) -> str:
        """Render finite measurements with up to four chronological points per time bin."""
        if type(width) is not int or not 1 <= width <= 65535:
            raise ValueError("width must be an integer from 1 through 65535")
        if type(title) is not str:
            raise TypeError("title must be a string")
        if not isinstance(times, list) or not isinstance(values, list):
            raise TypeError("times and values must be lists")
        if not times or len(times) != len(values):
            raise ValueError("times and values must have the same nonzero length")
        if any(type(t) is not datetime or t.tzinfo is not None for t in times):
            raise ValueError("use naive datetimes on one time scale")
        if any(type(v) not in (int, float) or not isfinite(v) for v in values):
            raise ValueError("values must be finite numbers")
        return self._db.execute("""
            WITH points AS (
                SELECT t.ts, v.y, t.ord
                FROM unnest(?::TIMESTAMP[]) WITH ORDINALITY AS t(ts, ord)
                JOIN unnest(?::DOUBLE[]) WITH ORDINALITY AS v(y, ord) USING (ord)
            ), ticks AS (
                SELECT *, epoch_us(ts)::HUGEINT AS t FROM points
            ), bounds AS (
                SELECT min(t) AS lo, max(t) AS hi FROM ticks
            ), buckets AS (
                SELECT *, ((t-lo) * ?::INTEGER) // (hi-lo+1) AS bin FROM ticks, bounds
            ), kept AS (
                SELECT unnest([
                    first(ord ORDER BY ts, ord), first(ord ORDER BY y, ts, ord),
                    first(ord ORDER BY y DESC, ts, ord), last(ord ORDER BY ts, ord)
                ]) AS ord FROM buckets GROUP BY bin
            )
            SELECT line_chart_html(list(ts ORDER BY ts, ord), list(y ORDER BY ts, ord), ?)
            FROM points SEMI JOIN kept USING (ord)
        """, [times, values, width, title]).fetchone()[0]
