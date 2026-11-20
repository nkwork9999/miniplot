"""A time-series HTML chart rendered on a host-provisioned Miniplot connection."""
from dataclasses import dataclass
from datetime import datetime
from math import isfinite
from sys import float_info

from duckdb import DuckDBPyConnection


_SERIES_SQL = """
    WITH points AS (
        SELECT t.ts, v.y, t.ord
        FROM unnest(?::TIMESTAMP[]) WITH ORDINALITY AS t(ts, ord)
        JOIN unnest(?::DOUBLE[]) WITH ORDINALITY AS v(y, ord) USING (ord)
    ), ticks AS (
        SELECT *, epoch_us(ts)::HUGEINT AS t, {segment_expression} AS segment
        FROM points
    ), bounds AS (
        SELECT min(t) AS lo, max(t) AS hi FROM ticks
    ), buckets AS (
        SELECT *, ((t-lo) * ?::INTEGER) // (hi-lo+1) AS bin
        FROM ticks, bounds WHERE y IS NOT NULL
    ), kept AS (
        SELECT unnest([
            first(ord ORDER BY ts, ord), first(ord ORDER BY y, ts, ord),
            first(ord ORDER BY y DESC, ts, ord), last(ord ORDER BY ts, ord)
        ]) AS ord FROM buckets GROUP BY segment, bin
    )
"""


class TimeSeriesChart:
    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    @staticmethod
    def _validate_series(times, values, width, *, allow_gaps):
        if type(width) is not int or not 1 <= width <= 65535:
            raise ValueError("width must be an integer from 1 through 65535")
        if not isinstance(times, list) or not isinstance(values, list):
            raise TypeError("times and values must be lists")
        if not times or len(times) != len(values):
            raise ValueError("times and values must have the same nonzero length")
        if any(type(t) is not datetime or t.tzinfo is not None for t in times):
            raise ValueError("use naive datetimes on one time scale")
        for value in values:
            if value is None and allow_gaps:
                continue
            if type(value) not in (int, float) or not isfinite(value):
                raise ValueError("values must be finite numbers")

    def line(self, times: list[datetime], values: list[float], *, title: str, width: int) -> str:
        """Render finite measurements with up to four chronological points per time bin."""
        if type(title) is not str:
            raise TypeError("title must be a string")
        self._validate_series(times, values, width, allow_gaps=False)
        return self._db.execute(_SERIES_SQL.format(segment_expression="0") + """
            SELECT line_chart_html(list(ts ORDER BY ts, ord), list(y ORDER BY ts, ord), ?)
            FROM points SEMI JOIN kept USING (ord)
        """, [times, values, width, title]).fetchone()[0]

    def segments(self, times: list[datetime], values: list[float | None], *,
                 width: int) -> list[list[tuple[datetime, float]]]:
        """Reduce each uninterrupted section independently. None splits sections."""
        self._validate_series(times, values, width, allow_gaps=True)
        segment_expression = (
            "count(*) FILTER (WHERE y IS NULL) OVER "
            "(ORDER BY ts, ord ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)"
        )
        rows = self._db.execute(_SERIES_SQL.format(segment_expression=segment_expression) + """
            SELECT list(struct_pack(ts := ts, y := y) ORDER BY ts, ord)
            FROM ticks SEMI JOIN kept USING (ord)
            GROUP BY segment ORDER BY segment
        """, [times, values, width]).fetchall()
        return [[(point["ts"], point["y"]) for point in segment] for (segment,) in rows]


@dataclass(frozen=True)
class HistogramResult:
    counts: tuple[int, ...]
    missing: int
    outside: int


class Histogram:
    """Compute numeric bin counts on a host-provisioned DuckDB connection."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def count(self, values: list[float | None], edges: list[float]) -> HistogramResult:
        """Use left-closed bins, including the right edge of the final bin."""
        if type(edges) is not list or len(edges) < 2:
            raise ValueError("provide at least two edges")
        if any(type(edge) not in (int, float) or not -float_info.max <= edge <= float_info.max for edge in edges):
            raise ValueError("edges must be finite numbers")
        bounds = [float(edge) for edge in edges]
        if any(left >= right for left, right in zip(bounds, bounds[1:])):
            raise ValueError("edges must be strictly increasing as double precision values")
        if type(values) is not list or any(
            value is not None and (type(value) not in (int, float) or not -float_info.max <= value <= float_info.max)
            for value in values
        ):
            raise ValueError("values must be a list of finite numbers or None")
        rows = self._db.execute("""
            WITH bins AS (
                SELECT lower, ord FROM unnest(?::DOUBLE[]) WITH ORDINALITY AS t(lower, ord)
            ), samples AS (
                SELECT x FROM unnest(?::DOUBLE[]) AS t(x)
            ), in_range AS (
                SELECT x FROM samples WHERE x BETWEEN ? AND ?
            ), counts AS (
                SELECT ord, count(*) AS n FROM in_range
                ASOF JOIN bins ON x >= lower GROUP BY ord
            )
            SELECT coalesce(list(coalesce(n, 0) ORDER BY ord), []) AS counts,
                   (SELECT count(*) FROM samples WHERE x IS NULL),
                   (SELECT count(*) FROM samples WHERE x < ? OR x > ?)
            FROM bins LEFT JOIN counts USING (ord)
        """, [bounds[:-1], values, bounds[0], bounds[-1], bounds[0], bounds[-1]]).fetchone()
        return HistogramResult(tuple(rows[0]), rows[1], rows[2])
