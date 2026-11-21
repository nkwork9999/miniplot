"""A time-series HTML chart rendered on a host-provisioned Miniplot connection."""
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from enum import Enum
from graphlib import CycleError, TopologicalSorter
from itertools import chain
from json import dumps
from math import exp, isfinite, log, prod
from sys import float_info

from duckdb import DuckDBPyConnection


_SERIES_SQL = """
    WITH points AS ({point_source}), ticks AS (
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

_ARRAY_SOURCE = """
    SELECT t.ts, v.y, t.ord
    FROM unnest(?::TIMESTAMP[]) WITH ORDINALITY AS t(ts, ord)
    JOIN unnest(?::DOUBLE[]) WITH ORDINALITY AS v(y, ord) USING (ord)
"""

_LINE_SQL = """
    SELECT line_chart_html(list(ts ORDER BY ts, ord), list(y ORDER BY ts, ord), ?)
    FROM points SEMI JOIN kept USING (ord)
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
        sql = _SERIES_SQL.format(point_source=_ARRAY_SOURCE, segment_expression="0")
        return self._db.execute(sql + _LINE_SQL, [times, values, width, title]).fetchone()[0]

    def segments(self, times: list[datetime], values: list[float | None], *,
                 width: int) -> list[list[tuple[datetime, float]]]:
        """Reduce each uninterrupted section independently. None splits sections."""
        self._validate_series(times, values, width, allow_gaps=True)
        segment_expression = (
            "count(*) FILTER (WHERE y IS NULL) OVER "
            "(ORDER BY ts, ord ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)"
        )
        sql = _SERIES_SQL.format(point_source=_ARRAY_SOURCE, segment_expression=segment_expression)
        rows = self._db.execute(sql + """
            SELECT list(struct_pack(ts := ts, y := y) ORDER BY ts, ord)
            FROM ticks SEMI JOIN kept USING (ord)
            GROUP BY segment ORDER BY segment
        """, [times, values, width]).fetchall()
        return [[(point["ts"], point["y"]) for point in segment] for (segment,) in rows]


def _quote_identifier(identifier):
    if type(identifier) is not str or not identifier or "\0" in identifier:
        raise ValueError("expected a nonempty identifier without NUL")
    return '"' + identifier.replace('"', '""') + '"'


@dataclass(frozen=True)
class TimeSeriesDataset:
    """Trusted main-schema relation, timestamp, unique tie-breaker and numeric series columns."""

    relation: str
    time_column: str
    order_column: str
    measures: Mapping[str, str]


@dataclass(frozen=True)
class LineChartSpec:
    dataset_id: str
    measure_id: str
    start: datetime
    end: datetime
    width: int
    title: str

    def __post_init__(self):
        for value in (self.dataset_id, self.measure_id, self.title):
            if type(value) is not str:
                raise TypeError("dataset, measure and title must be strings")
        for value in (self.start, self.end):
            if type(value) is not datetime or value.tzinfo is not None:
                raise ValueError("expected naive datetimes on one time scale")
        if self.end <= self.start or type(self.width) is not int or not 1 <= self.width <= 65535:
            raise ValueError("expected a nonempty time range and width in [1, 65535]")


class BucketSize(Enum):
    MINUTE = "minute"
    HOUR = "hour"
    DAY = "day"
    MONTH = "month"


@dataclass(frozen=True)
class BucketObservation:
    bucket: datetime
    series_id: str
    mean: float | None
    observations: int


class ChartBuilder:
    """Resolve caller chart specifications against the host's dataset registry."""

    def __init__(self, connection: DuckDBPyConnection, datasets: Mapping[str, TimeSeriesDataset]):
        self._db = connection
        self._datasets = {}
        for key, dataset in datasets.items():
            if type(key) is not str or not key or type(dataset) is not TimeSeriesDataset:
                raise TypeError("expected a registry of TimeSeriesDataset values")
            measures = {key: _quote_identifier(column) for key, column in dataset.measures.items()}
            if any(type(key) is not str or not key for key in measures):
                raise TypeError("measure IDs must be nonempty strings")
            self._datasets[key] = (_quote_identifier(dataset.relation), _quote_identifier(dataset.time_column),
                                   _quote_identifier(dataset.order_column), measures)

    def line(self, spec: LineChartSpec) -> str | None:
        """Project a half-open time window and reduce it before rendering."""
        if type(spec) is not LineChartSpec:
            raise TypeError("expected a LineChartSpec")
        relation, time, order, measures = self._datasets[spec.dataset_id]
        value = measures[spec.measure_id]
        source = f"""
            SELECT {time} AS ts, {value}::DOUBLE AS y, {order} AS ord
            FROM main.{relation}
            WHERE {time} >= ?::TIMESTAMP AND {time} < ?::TIMESTAMP
              AND {order} IS NOT NULL AND isfinite({value}::DOUBLE)
        """
        sql = _SERIES_SQL.format(point_source=source, segment_expression="0")
        return self._db.execute(sql + _LINE_SQL, [spec.start, spec.end, spec.width, spec.title]).fetchone()[0]

    def bucketed_series(self, dataset_id: str, series: tuple[str, ...], start: datetime,
                        end: datetime, *, size: BucketSize) -> list[BucketObservation]:
        """Aggregate requested wide columns in one scan, then emit long rows."""
        if type(dataset_id) is not str or type(series) is not tuple or not 1 <= len(series) <= 128:
            raise TypeError("expected a dataset ID and one to 128 registered series IDs")
        if any(type(name) is not str for name in series) or len(set(series)) != len(series):
            raise ValueError("series IDs must be distinct strings")
        if type(size) is not BucketSize:
            raise TypeError("expected a BucketSize")
        if any(type(value) is not datetime or value.tzinfo is not None for value in (start, end)) or end <= start:
            raise ValueError("expected a nonempty range of naive datetimes")
        relation, time, _, measures = self._datasets[dataset_id]
        aggregates, grouped = [], []
        for index, name in enumerate(series):
            column = f"{measures[name]}::DOUBLE"
            aggregates += [
                f"avg({column}) FILTER (WHERE isfinite({column})) AS mean_{index}",
                f"count(*) FILTER (WHERE isfinite({column})) AS count_{index}",
            ]
            grouped.append(f"struct_pack(mean := mean_{index}, n := count_{index})")
        rows = self._db.execute(f"""
            WITH buckets AS (
                SELECT date_trunc(?, {time})::TIMESTAMP AS bucket_ts, {", ".join(aggregates)}
                FROM main.{relation} WHERE {time} >= ?::TIMESTAMP AND {time} < ?::TIMESTAMP
                GROUP BY bucket_ts
            )
            SELECT bucket_ts, ordinal, summary.mean, summary.n FROM buckets
            CROSS JOIN UNNEST([{", ".join(grouped)}]) WITH ORDINALITY AS t(summary, ordinal)
            ORDER BY bucket_ts, ordinal
        """, [size.value, start, end]).fetchall()
        return [BucketObservation(bucket, series[ordinal - 1], mean, observations)
                for bucket, ordinal, mean, observations in rows]


@dataclass(frozen=True)
class StateInterval:
    start: datetime
    end: datetime
    state: str


class StateTimeline:
    """Compress consecutive equal events to half-open, bounded validity intervals."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def reduce(self, times: list[datetime], states: list[str | None], *,
               start: datetime, end: datetime, max_gap: timedelta) -> list[StateInterval]:
        if type(times) is not list or type(states) is not list or len(times) != len(states):
            raise ValueError("times and states must be lists of equal length")
        if any(type(t) is not datetime or t.tzinfo is not None for t in chain((start, end), times)):
            raise ValueError("use naive datetimes on one time scale")
        if end <= start or type(max_gap) is not timedelta or max_gap <= timedelta(0):
            raise ValueError("expected a nonempty extent and positive max_gap")
        if any(state is not None and type(state) is not str for state in states):
            raise TypeError("state values must be strings or None")
        rows = self._db.execute("""
            WITH bounds AS (
                SELECT ?::TIMESTAMP AS lo, ?::TIMESTAMP AS hi, ?::INTERVAL AS gap
            ), input AS (SELECT ?::TIMESTAMP[] AS times, ?::VARCHAR[] AS states),
            events AS (
                SELECT unnest(times) AS ts, unnest(states) AS state,
                       generate_subscripts(times, 1) AS ord FROM input
            ), deduplicated AS (
                SELECT ts, state FROM events, bounds WHERE ts >= lo - gap AND ts < hi
                QUALIFY row_number() OVER (PARTITION BY ts ORDER BY ord DESC) = 1
            ), adjacent AS (
                SELECT *, lag(ts) OVER w AS previous_time, lag(state) OVER w AS previous_state
                FROM deduplicated WINDOW w AS (ORDER BY ts)
            ), numbered AS (
                SELECT ts, state, count(*) FILTER (
                    WHERE previous_time IS NULL OR ts > previous_time + gap OR state IS DISTINCT FROM previous_state
                ) OVER (ORDER BY ts ROWS UNBOUNDED PRECEDING) AS run
                FROM adjacent, bounds
            ), runs AS (
                SELECT min(ts) AS first_sample, max(ts) AS last_sample, any_value(state) AS state
                FROM numbered GROUP BY run
            ), stops AS (
                SELECT *, lead(first_sample) OVER (ORDER BY first_sample) AS next_state FROM runs
            ), clipped AS (
                SELECT greatest(first_sample, lo) AS start_time,
                       least(coalesce(next_state, hi), last_sample + gap, hi) AS end_time, state
                FROM stops, bounds
            )
            SELECT start_time, end_time, state FROM clipped
            WHERE start_time < end_time AND state IS NOT NULL ORDER BY start_time
        """, [start, end, max_gap, times, states]).fetchall()
        return [StateInterval(*row) for row in rows]


@dataclass(frozen=True)
class CalendarRange:
    start: date
    end: date

    def __post_init__(self):
        if type(self.start) is not date or type(self.end) is not date or self.end <= self.start:
            raise ValueError("expected a nonempty half-open date range")


@dataclass(frozen=True)
class CalendarDay:
    day: date
    state: str
    value: float | None
    observations: int


class CalendarData:
    """Fill a date grid with separate observed, missing and out-of-coverage states."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def daily(self, times: list[datetime], values: list[float | None], *,
              displayed: CalendarRange, covered: CalendarRange, timezone_name: str) -> list[CalendarDay]:
        if type(displayed) is not CalendarRange or type(covered) is not CalendarRange:
            raise TypeError("expected CalendarRange bounds")
        if type(timezone_name) is not str or not timezone_name:
            raise ValueError("timezone_name must be a nonempty string")
        if type(times) is not list or type(values) is not list or len(times) != len(values):
            raise ValueError("times and values must be lists of equal length")
        if any(type(t) is not datetime or t.utcoffset() is None for t in times):
            raise ValueError("calendar observations require timezone-aware datetimes")
        if any(v is not None and (type(v) not in (int, float) or not -float_info.max <= v <= float_info.max)
               for v in values):
            raise ValueError("calendar values must be finite numbers or None")
        rows = self._db.execute("""
            WITH parameters AS (
                SELECT ?::DATE AS display_lo, ?::DATE AS display_hi,
                    ?::DATE AS covered_lo, ?::DATE AS covered_hi, ?::VARCHAR AS zone
            ), samples AS (
                SELECT unnest(?::TIMESTAMPTZ[]) AS observed_at, unnest(?::DOUBLE[]) AS value
            ), observed AS (
                SELECT timezone(zone, observed_at)::DATE AS d, value FROM samples, parameters
            ), totals AS (
                SELECT d, fsum(value) AS total, count(value) AS measurements
                FROM observed, parameters
                WHERE d >= greatest(display_lo, covered_lo) AND d < least(display_hi, covered_hi)
                GROUP BY d
            ), grid AS (
                SELECT dt::DATE AS d, dt >= covered_lo AND dt < covered_hi AS covered
                FROM parameters, generate_series(display_lo, display_hi - 1, INTERVAL 1 DAY) g(dt)
            )
            SELECT d, CASE WHEN NOT covered THEN 'outside'
                 WHEN coalesce(measurements, 0) = 0 THEN 'missing' ELSE 'observed' END,
                 total, coalesce(measurements, 0)
            FROM grid LEFT JOIN totals USING (d) ORDER BY d
        """, [displayed.start, displayed.end, covered.start, covered.end, timezone_name, times, values]).fetchall()
        return [CalendarDay(*row) for row in rows]


@dataclass(frozen=True)
class MarketSession:
    session_id: str
    start: datetime
    end: datetime

    def __post_init__(self):
        if type(self.session_id) is not str or not self.session_id:
            raise ValueError("session ID must be a nonempty string")
        if any(type(t) is not datetime or t.utcoffset() is None for t in (self.start, self.end)):
            raise ValueError("session boundaries must be timezone-aware")
        start, end = self.start.astimezone(timezone.utc), self.end.astimezone(timezone.utc)
        if start >= end:
            raise ValueError("the session must be nonempty")
        object.__setattr__(self, "start", start)
        object.__setattr__(self, "end", end)


@dataclass(frozen=True)
class QuoteTrade:
    time: datetime
    sequence: int
    price: float
    volume: float

    def __post_init__(self):
        if type(self.time) is not datetime or self.time.utcoffset() is None:
            raise ValueError("trade time must be timezone-aware")
        object.__setattr__(self, "time", self.time.astimezone(timezone.utc))
        if type(self.sequence) is not int or not -(1 << 63) <= self.sequence < (1 << 63):
            raise ValueError("sequence must be a signed 64-bit integer")
        if any(type(n) not in (float, int) or not 0 <= n <= float_info.max for n in (self.price, self.volume)):
            raise ValueError("price and volume must be finite nonnegative numbers")


@dataclass(frozen=True)
class MarketBar:
    session_id: str
    open: float | None
    high: float | None
    low: float | None
    close: float | None
    volume: float | None
    trades: int


class OHLC:
    """Aggregate trades by explicit market sessions and tie-break on exchange sequence."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def bars(self, sessions: list[MarketSession], trades: list[QuoteTrade]) -> list[MarketBar]:
        if type(sessions) is not list or any(type(s) is not MarketSession for s in sessions):
            raise TypeError("expected a list of MarketSession values")
        if type(trades) is not list or any(type(t) is not QuoteTrade for t in trades):
            raise TypeError("expected a list of QuoteTrade values")
        ordered = sorted(sessions, key=lambda session: session.start)
        if len({s.session_id for s in sessions}) != len(sessions) or any(
            left.end > right.start for left, right in zip(ordered, ordered[1:])
        ):
            raise ValueError("sessions must have unique IDs and nonoverlapping intervals")
        if len({(t.time, t.sequence) for t in trades}) != len(trades):
            raise ValueError("the trade time and sequence pair must be unique")
        rows = self._db.execute("""
            WITH sessions AS MATERIALIZED (
                SELECT unnest(?::VARCHAR[]) AS session_id, unnest(?::TIMESTAMPTZ[]) AS opens_at,
                       unnest(?::TIMESTAMPTZ[]) AS closes_at
            ), trades AS (
                SELECT unnest(?::TIMESTAMPTZ[]) AS traded_at, unnest(?::BIGINT[]) AS seq,
                       unnest(?::DOUBLE[]) AS price, unnest(?::DOUBLE[]) AS volume
            ), assigned AS (
                SELECT session_id, traded_at, seq, price, volume FROM trades
                ASOF JOIN sessions ON traded_at >= opens_at WHERE traded_at < closes_at
            ), bars AS (
                SELECT session_id, first(price ORDER BY traded_at, seq) AS opening,
                       max(price) AS high, min(price) AS low, last(price ORDER BY traded_at, seq) AS closing,
                       fsum(volume) AS volume, count(*) AS number_trades
                FROM assigned GROUP BY session_id
            )
            SELECT session_id, opening, high, low, closing, volume, coalesce(number_trades, 0)
            FROM sessions LEFT JOIN bars USING (session_id) ORDER BY opens_at
        """, [[s.session_id for s in ordered], [s.start for s in ordered], [s.end for s in ordered],
              [t.time for t in trades], [t.sequence for t in trades],
              [t.price for t in trades], [t.volume for t in trades]]).fetchall()
        return [MarketBar(*row) for row in rows]


@dataclass(frozen=True)
class SeriesPoint:
    time: datetime
    key: int
    value: float | None

    def __post_init__(self):
        if type(self.time) is not datetime or self.time.tzinfo is not None:
            raise ValueError("use naive timestamps on the same time scale")
        if type(self.key) is not int or not -(1 << 63) <= self.key < (1 << 63):
            raise ValueError("series key must be a signed 64-bit integer")
        if self.value is not None and (type(self.value) not in (float, int)
                                       or not -float_info.max <= self.value <= float_info.max):
            raise ValueError("observations must be finite numbers or None")


@dataclass(frozen=True)
class SeriesDifference:
    time: datetime
    key: int
    old: float | None
    new: float | None
    delta: float | None
    ratio: float | None
    state: str


class TimeSeriesDiff:
    """Align two chart revisions by timestamp and stable row identity."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    @staticmethod
    def _bind(series):
        if type(series) is not list or any(type(point) is not SeriesPoint for point in series):
            raise TypeError("expected a list of SeriesPoint values")
        if len({(point.time, point.key) for point in series}) != len(series):
            raise ValueError("timestamp and key pairs must be unique per series")
        return [[point.time for point in series], [point.key for point in series], [point.value for point in series]]

    def compare(self, old: list[SeriesPoint], new: list[SeriesPoint]) -> list[SeriesDifference]:
        bindings = self._bind(old) + self._bind(new)
        rows = self._db.execute("""
            WITH baseline AS (
                SELECT unnest(?::TIMESTAMP[]) AS ts, unnest(?::BIGINT[]) AS id, unnest(?::DOUBLE[]) AS old
            ), candidate AS (
                SELECT unnest(?::TIMESTAMP[]) AS ts, unnest(?::BIGINT[]) AS id, unnest(?::DOUBLE[]) AS revised
            )
            SELECT coalesce(baseline.ts,candidate.ts) AS ts, coalesce(baseline.id,candidate.id) AS id,
                   old, revised, revised - old AS delta, (revised - old) / nullif(old, 0) AS ratio,
                   CASE WHEN baseline.id IS NULL THEN 'added'
                        WHEN candidate.id IS NULL THEN 'removed'
                        WHEN old IS NULL OR revised IS NULL THEN 'missing_value'
                        WHEN old = 0 THEN 'zero_baseline' ELSE 'matched' END
            FROM baseline FULL OUTER JOIN candidate
                ON baseline.ts = candidate.ts AND baseline.id = candidate.id
            ORDER BY ts, id
        """, bindings).fetchall()
        return [SeriesDifference(*row) for row in rows]

    def chart(self, old: list[SeriesPoint], new: list[SeriesPoint], *, title: str) -> str:
        """Render measurements, differences and relative changes on separate charts."""
        if type(title) is not str:
            raise TypeError("title must be a string")
        rows = self.compare(old, new)
        times = [r.time.isoformat() for r in rows]
        def trace(name, attr, scale=1):
            values = [getattr(r, attr) for r in rows]
            return {"type": "scatter", "mode": "lines+markers", "connectgaps": False, "name": name,
                    "x": times, "y": [v * scale if v is not None and isfinite(v * scale) else None for v in values]}
        sections = [
            {"heading": "Measurements", "data": [trace("Previous", "old"), trace("Current", "new")]},
            {"heading": "Absolute change", "data": [trace("Difference", "delta")]},
            {"heading": "Relative change (%)", "data": [trace("Percent change", "ratio", 100)]},
        ]
        html, = self._db.execute("SELECT chart_report_html(?::VARCHAR)", [dumps({
            "title": title, "sections": sections,
        }, allow_nan=False)]).fetchone()
        return html


@dataclass(frozen=True)
class VoxelSpec:
    lower: tuple[float, float, float]
    upper: tuple[float, float, float]
    bins: tuple[int, int, int]

    def __post_init__(self):
        if any(type(values) is not tuple or len(values) != 3 for values in (self.lower, self.upper, self.bins)):
            raise ValueError("provide three bounds and cell counts")
        for lo, hi in zip(self.lower, self.upper):
            if not all(type(n) in (int, float) and isfinite(n) for n in (lo, hi)) or not 0 < hi - lo < float("inf"):
                raise ValueError("every axis must have an increasing finite span")
        if any(type(n) is not int or not 1 <= n <= 256 for n in self.bins) or prod(self.bins) > 262_144:
            raise ValueError("the grid may have up to 256 cells on each axis and 262144 cells overall")


@dataclass(frozen=True)
class Voxel:
    cell: tuple[int, int, int]
    count: int
    center: tuple[float, float, float]
    mean_color: float | None


@dataclass(frozen=True)
class VoxelPoint:
    point_id: int
    position: tuple[float, float, float]
    color: float | None


class VoxelGrid:
    """Aggregate three-dimensional points in a registered DuckDB table."""

    def __init__(self, connection: DuckDBPyConnection, relation: str):
        self._db = connection
        source = _quote_identifier(relation)
        axes = ("x", "y", "z")
        bounds = ", ".join(f"?::DOUBLE AS {kind}_{axis}" for kind in ("lo", "hi") for axis in axes)
        sizes = ", ".join(f"?::INTEGER AS bins_{axis}" for axis in axes)
        units = ", ".join(f"({axis} - lo_{axis}) / (hi_{axis} - lo_{axis}) AS unit_{axis}" for axis in axes)
        cells = ", ".join(
            f"least(bins_{axis} - 1, floor(unit_{axis} * bins_{axis}))::INT AS cell_{axis}" for axis in axes
        )
        filters = " AND ".join(f"{axis} BETWEEN lo_{axis} AND hi_{axis} AND isfinite({axis})" for axis in axes)
        self._centers = ", ".join(
            f"min(lo_{axis}) + favg(unit_{axis}) * (max(hi_{axis}) - min(lo_{axis}))" for axis in axes
        )
        self._buckets = f"""
            WITH bounds AS (SELECT {bounds}, {sizes}), scaled AS (
                SELECT id, x, y, z, CASE WHEN isfinite(color) THEN color END AS color, bounds.*, {units}
                FROM main.{source} CROSS JOIN bounds WHERE {filters}
            ), cells AS (SELECT *, {cells} FROM scaled)
        """

    @staticmethod
    def _bounds(spec):
        if type(spec) is not VoxelSpec:
            raise TypeError("expected a VoxelSpec")
        return [*spec.lower, *spec.upper, *spec.bins]

    def summarize(self, spec: VoxelSpec) -> list[Voxel]:
        rows = self._db.execute(self._buckets + f"""
            SELECT cell_x, cell_y, cell_z, count(*), {self._centers}, favg(color)
            FROM cells GROUP BY cell_x, cell_y, cell_z ORDER BY cell_x, cell_y, cell_z
        """, self._bounds(spec)).fetchall()
        return [Voxel((i, j, k), count, (x, y, z), color) for i, j, k, count, x, y, z, color in rows]

    def points(self, spec: VoxelSpec, cell: tuple[int, int, int], *, limit: int,
               after_id: int | None = None) -> list[VoxelPoint]:
        parameters = self._bounds(spec)
        if (type(cell) is not tuple or len(cell) != 3 or
                any(type(i) is not int or not 0 <= i < n for i, n in zip(cell, spec.bins))):
            raise ValueError("cell coordinates must be inside the grid")
        if type(limit) is not int or not 1 <= limit <= 10_000:
            raise ValueError("limit must be an integer between 1 and 10000")
        if after_id is not None and (type(after_id) is not int or not -(1 << 63) <= after_id < 1 << 63):
            raise ValueError("after_id must be a signed 64-bit integer or None")
        rows = self._db.execute(self._buckets + """
            SELECT id, x, y, z, color FROM cells WHERE (cell_x, cell_y, cell_z) = (?, ?, ?)
                AND (?::BIGINT IS NULL OR id > ?::BIGINT)
            ORDER BY id LIMIT ?
        """, [*parameters, *cell, after_id, after_id, limit]).fetchall()
        return [VoxelPoint(key, (x, y, z), color) for key, x, y, z, color in rows]

    def chart(self, spec: VoxelSpec, *, title: str) -> str:
        if type(title) is not str:
            raise TypeError("title must be a string")
        cells = self.summarize(spec)
        markers = [cell.count for cell in cells]
        trace = {
            "type": "scatter3d", "mode": "markers",
            "x": [cell.center[0] for cell in cells], "y": [cell.center[1] for cell in cells],
            "z": [cell.center[2] for cell in cells],
            "customdata": [[*cell.cell, cell.count] for cell in cells],
            "hovertemplate": "Cell %{customdata[0]}, %{customdata[1]}, %{customdata[2]}<br>Count %{customdata[3]}",
            "marker": {"size": markers, "sizemode": "area", "sizemin": 2,
                       "sizeref": max(markers, default=1) / 80,
                       "color": [cell.mean_color if cell.mean_color is None or isfinite(cell.mean_color) else None
                                 for cell in cells]},
        }
        report = dumps({"title": title, "sections": [{"data": [trace]}]}, allow_nan=False)
        return self._db.execute("SELECT chart_report_html(?::VARCHAR)", [report]).fetchone()[0]


@dataclass(frozen=True)
class HierarchyNode:
    key: str
    parent: str | None
    label: str
    weight: float | None = None

    def __post_init__(self):
        if type(self.key) is not str or not self.key or type(self.label) is not str:
            raise ValueError("hierarchy nodes require a nonempty key and a label")
        if self.parent is not None and (type(self.parent) is not str or not self.parent):
            raise ValueError("parent must be a nonempty key or None")
        if self.weight is not None and (type(self.weight) not in (float, int)
                                        or not 0 <= self.weight <= float_info.max):
            raise ValueError("leaf weight must be finite and nonnegative")


@dataclass(frozen=True)
class TreeMapArea:
    key: str
    parent: str | None
    label: str
    depth: int
    total: float


class TreeMap:
    """Build a leaf-weight hierarchy and render only the requested subtree depth."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def summarize(self, nodes: list[HierarchyNode], *, root: str, max_depth: int) -> list[TreeMapArea]:
        if type(nodes) is not list or any(type(node) is not HierarchyNode for node in nodes):
            raise TypeError("expected a list of HierarchyNode values")
        if type(max_depth) is not int or max_depth < 0:
            raise ValueError("max_depth must be a nonnegative integer")
        known = {node.key for node in nodes}
        if len(known) != len(nodes):
            raise ValueError("each ID must have exactly one parent declaration")
        if type(root) is not str or root not in known:
            raise KeyError(root)
        parents = {node.parent for node in nodes if node.parent is not None}
        if not parents <= known:
            raise ValueError("every parent must be declared")
        try:
            TopologicalSorter({n.key: () if n.parent is None else (n.parent,) for n in nodes}).prepare()
        except CycleError as error:
            raise ValueError("tree contains a cycle") from error
        if any((node.key in parents) == (node.weight is not None) for node in nodes):
            raise ValueError("provide weights for leaves only")
        records = [{"id": n.key, "parent": n.parent, "label": n.label, "weight": n.weight} for n in nodes]
        rows = self._db.execute("""
            WITH RECURSIVE nodes AS MATERIALIZED (
                SELECT n.id AS id, n.parent AS parent, n.label AS label, n.weight AS weight
                FROM unnest(?::STRUCT(id VARCHAR, parent VARCHAR, label VARCHAR, weight DOUBLE)[]) t(n)
            ), subtree(id, parent, label, weight, depth, ancestors) AS (
                SELECT id, NULL::VARCHAR, label, weight, 0, [id] FROM nodes WHERE id = ?
                UNION ALL
                SELECT n.id, n.parent, n.label, n.weight, s.depth + 1, list_append(s.ancestors, n.id)
                FROM nodes n JOIN subtree s ON n.parent = s.id
            ), projections AS (
                SELECT unnest(ancestors) AS id, weight FROM subtree WHERE weight IS NOT NULL
            ), areas AS (
                SELECT id, fsum(weight) AS total FROM projections GROUP BY id
            )
            SELECT id, parent, label, depth, total FROM subtree JOIN areas USING (id)
            WHERE depth <= ? ORDER BY depth, id
        """, [records, root, max_depth]).fetchall()
        return [TreeMapArea(*row) for row in rows]

    def chart(self, nodes: list[HierarchyNode], *, root: str, max_depth: int, title: str) -> str:
        if type(title) is not str:
            raise TypeError("title must be a string")
        summary = self.summarize(nodes, root=root, max_depth=max_depth)
        trace = {"type": "treemap", "ids": [n.key for n in summary], "labels": [n.label for n in summary],
                 "parents": [n.parent or "" for n in summary], "values": [n.total for n in summary],
                 "branchvalues": "total"}
        spec = dumps({"title": title, "sections": [{"data": [trace]}]}, allow_nan=False)
        return self._db.execute("SELECT chart_report_html(?::VARCHAR)", [spec]).fetchone()[0]


@dataclass(frozen=True)
class HistogramResult:
    counts: tuple[int, ...]
    missing: int
    outside: int


@dataclass(frozen=True)
class LogHistogramResult:
    edges: tuple[float, ...]
    counts: tuple[int, ...]
    missing: int
    outside: int
    nonpositive: int


@dataclass(frozen=True)
class ScatterExtent:
    xmin: float
    ymin: float
    xmax: float
    ymax: float

    def __post_init__(self):
        for low, high in ((self.xmin, self.xmax), (self.ymin, self.ymax)):
            if (type(low) not in (int, float) or type(high) not in (int, float)
                or not -float_info.max <= low < high <= float_info.max
                or not 0 < float(high) - float(low) <= float_info.max):
                raise ValueError("scatter bounds must have a finite, positive double-precision span")


@dataclass(frozen=True)
class DensityCell:
    column: int
    row: int
    count: int


@dataclass(frozen=True)
class ScatterSelection:
    visible_count: int
    cells: tuple[DensityCell, ...]
    points: tuple[tuple[float, float], ...]


class ScatterDensity:
    """Use a visible point budget to choose exact points or a compact count grid."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def fetch(self, x: list[float | None], y: list[float | None], extent: ScatterExtent, *,
              columns: int, rows: int, point_limit: int) -> ScatterSelection:
        if type(x) is not list or type(y) is not list or len(x) != len(y):
            raise ValueError("x and y must be lists of equal length")
        if any(v is not None and (type(v) not in (int, float) or not -float_info.max <= v <= float_info.max)
               for axis in (x, y) for v in axis):
            raise ValueError("coordinates must be finite numbers or None")
        if type(extent) is not ScatterExtent:
            raise TypeError("expected a ScatterExtent")
        if any(type(n) is not int or not 1 <= n <= 1024 for n in (columns, rows)):
            raise ValueError("grid dimensions must be in [1, 1024]")
        if type(point_limit) is not int or not 0 <= point_limit <= 1_000_000:
            raise ValueError("point_limit must be in [0, 1000000]")
        visible, cells, points = self._db.execute("""
            WITH parameters AS (
                SELECT ?::DOUBLE[] as xs, ?::DOUBLE[] as ys, ?::DOUBLE as xmin, ?::DOUBLE as ymin,
                       ?::DOUBLE as xmax, ?::DOUBLE as ymax, ?::INTEGER as nx, ?::INTEGER as ny,
                       ?::INTEGER as budget
            ), input AS (
                SELECT unnest(xs) AS x, unnest(ys) AS y, generate_subscripts(xs, 1) AS ord FROM parameters
            ), visible AS MATERIALIZED (
                SELECT x, y, ord FROM input, parameters WHERE x BETWEEN xmin AND xmax AND y BETWEEN ymin AND ymax
            ), size AS (SELECT count(*) AS n FROM visible),
            bins AS (
                SELECT least(nx - 1, floor((x - xmin) / (xmax - xmin) * nx))::INT AS gx,
                       least(ny - 1, floor((y - ymin) / (ymax - ymin) * ny))::INT AS gy,
                       count(*) AS hits
                FROM visible, parameters, size WHERE n > budget GROUP BY gx, gy
            )
            SELECT n,
                (SELECT list(struct_pack(gx := gx, gy := gy, hits := hits) ORDER BY gx, gy) FROM bins),
                (SELECT list(struct_pack(x := x, y := y) ORDER BY ord) FROM visible WHERE n <= budget)
            FROM size, parameters
        """, [x, y, extent.xmin, extent.ymin, extent.xmax, extent.ymax, columns, rows, point_limit]).fetchone()
        return ScatterSelection(visible, tuple(DensityCell(c["gx"], c["gy"], c["hits"]) for c in cells or ()),
                                tuple((p["x"], p["y"]) for p in points or ()))


class Histogram:
    """Compute numeric bin counts on a host-provisioned DuckDB connection."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    def count(self, values: list[float | None], edges: list[float]) -> HistogramResult:
        """Use left-closed bins, including the right edge of the final bin."""
        counts, missing, outside, _ = self._count(values, edges, positive_only=False)
        return HistogramResult(tuple(counts), missing, outside)

    def count_log(self, values: list[float | None], *, lower: float,
                  upper: float, bins: int) -> LogHistogramResult:
        """Count exponentially spaced bins with returned edges in original units."""
        if (type(lower) not in (int, float) or type(upper) not in (int, float)
            or not 0 < lower < upper <= float_info.max):
            raise ValueError("expected finite positive bounds in increasing order")
        if type(bins) is not int or not 1 <= bins <= 65_535:
            raise ValueError("bins must be an integer in [1, 65535]")
        start, distance = log(lower), log(upper) - log(lower)
        edges = [float(lower)]
        edges.extend(exp(start + distance * index / bins) for index in range(1, bins))
        edges.append(float(upper))
        counts, missing, outside, nonpositive = self._count(values, edges, positive_only=True)
        return LogHistogramResult(tuple(edges), tuple(counts), missing, outside, nonpositive)

    def _count(self, values, edges, *, positive_only):
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
                   (SELECT count(*) FROM samples WHERE (x < ? OR x > ?) AND (NOT ? OR x > 0)),
                   (SELECT count(*) FROM samples WHERE ? AND x <= 0)
            FROM bins LEFT JOIN counts USING (ord)
        """, [bounds[:-1], values, bounds[0], bounds[-1], bounds[0], bounds[-1],
              positive_only, positive_only]).fetchone()
        return rows


@dataclass(frozen=True)
class BoxSummary:
    group_id: int
    count: int
    q1: float | None
    median: float | None
    q3: float | None
    lower_whisker: float | None
    upper_whisker: float | None
    outlier_count: int
    outlier_examples: tuple[float, ...]


@dataclass(frozen=True)
class CategoryTotal:
    kind: str
    category_id: int | None
    label: str
    total: float | None


@dataclass(frozen=True)
class DegreeCount:
    direction: str
    degree: int
    vertices: int


class ErrorBarMethod(Enum):
    STANDARD_ERROR = 1.0
    NORMAL_95 = 1.959963984540054


@dataclass(frozen=True)
class ErrorBar:
    group_id: int
    count: int
    mean: float | None
    variance: float | None
    lower: float | None
    upper: float | None
    method: ErrorBarMethod
    reason: str | None


@dataclass(frozen=True)
class WeightedMean:
    group_id: int
    weighted_sum: float
    weight: float

    def __post_init__(self):
        if type(self.group_id) is not int or not -(1 << 63) <= self.group_id < (1 << 63):
            raise ValueError("group ID must fit a signed 64-bit integer")
        if (type(self.weighted_sum) not in (int, float)
            or not -float_info.max <= self.weighted_sum <= float_info.max
            or type(self.weight) not in (int, float)
            or not 0 <= self.weight <= float_info.max):
            raise ValueError("weighted sum and nonnegative weight must be finite")
        if self.weight == 0 and self.weighted_sum != 0:
            raise ValueError("a zero weight must have zero weighted sum")

    @property
    def mean(self) -> float | None:
        return self.weighted_sum / self.weight if self.weight else None


class GroupedStatistics:
    """Return compact chart data computed on the host's DuckDB connection."""

    def __init__(self, connection: DuckDBPyConnection):
        self._db = connection

    @staticmethod
    def _is_id(value):
        return type(value) is int and -(1 << 63) <= value < (1 << 63)

    @classmethod
    def _validate_series(cls, groups, values, *, allow_missing_group=False):
        if type(groups) is not list or type(values) is not list or len(groups) != len(values):
            raise ValueError("groups and values must be lists of equal length")
        if any(not (cls._is_id(g) or (allow_missing_group and g is None)) for g in groups):
            raise ValueError("group IDs must be signed 64-bit integers")
        if any(value is not None and (type(value) not in (int, float)
                                     or not -float_info.max <= value <= float_info.max)
               for value in values):
            raise ValueError("values must be finite numbers or None")

    def boxplot(self, groups: list[int], values: list[float | None], *,
                outlier_limit: int = 0) -> list[BoxSummary]:
        """Use continuous quartiles, observed Tukey whiskers, and bounded outlier samples."""
        self._validate_series(groups, values)
        if type(outlier_limit) is not int or not 0 <= outlier_limit <= 10_000:
            raise ValueError("outlier_limit must be an integer in [0, 10000]")
        rows = self._db.execute("""
            WITH samples AS MATERIALIZED (SELECT unnest(?::BIGINT[]) AS id, unnest(?::DOUBLE[]) AS value),
            quartiles AS (
                SELECT id, count(value) AS n, quantile_cont(value, [0.25, 0.5, 0.75]) AS q
                FROM samples GROUP BY id
            ), classified AS (
                SELECT id, value, value BETWEEN q[1] - (q[3] - q[1]) * 1.5
                                           AND q[3] + (q[3] - q[1]) * 1.5 AS within_fences
                FROM samples JOIN quartiles USING (id)
            ), whiskers AS (
                SELECT id, min(value) FILTER (WHERE within_fences) AS low,
                           max(value) FILTER (WHERE within_fences) AS high,
                           count(*) FILTER (WHERE NOT within_fences) AS outliers,
                           min(value, ?) FILTER (WHERE NOT within_fences) AS examples
                FROM classified GROUP BY id
            )
            SELECT id, n, q[1], q[2], q[3], low, high, outliers, list_slice(examples, 1, ?)
            FROM quartiles JOIN whiskers USING (id) ORDER BY id
        """, [groups, values, max(1, outlier_limit), outlier_limit]).fetchall()
        return [BoxSummary(*row[:-1], tuple(row[-1] or ())) for row in rows]

    def boxplot_html(self, groups: list[int], values: list[float | None], *, title: str,
                     outlier_limit: int = 0) -> str:
        """Render the precomputed boxes as a Plotly trace."""
        if type(title) is not str:
            raise TypeError("title must be a string")
        boxes = [box for box in self.boxplot(groups, values, outlier_limit=outlier_limit) if box.count]
        trace = {
            "type": "box", "x": [str(box.group_id) for box in boxes],
            "q1": [box.q1 for box in boxes], "median": [box.median for box in boxes],
            "q3": [box.q3 for box in boxes],
            "lowerfence": [box.lower_whisker for box in boxes],
            "upperfence": [box.upper_whisker for box in boxes],
            "customdata": [[box.outlier_count, box.outlier_examples] for box in boxes],
            "boxpoints": False,
        }
        spec = dumps({"title": title, "sections": [{"data": [trace]}]}, allow_nan=False)
        html, = self._db.execute("SELECT chart_report_html(?::VARCHAR)", [spec]).fetchone()
        return html

    def top_categories(self, categories: list[int | None], values: list[float | None],
                       labels: dict[int, str], *, limit: int) -> list[CategoryTotal]:
        """Aggregate by stable ID and preserve distinct missing and remainder totals."""
        self._validate_series(categories, values, allow_missing_group=True)
        if type(limit) is not int or not 0 <= limit <= 1_000_000:
            raise ValueError("limit must be an integer in [0, 1000000]")
        if type(labels) is not dict or any(not self._is_id(i) or type(label) is not str for i, label in labels.items()):
            raise TypeError("labels must map category IDs to strings")
        if set(categories) - {None} - labels.keys():
            raise KeyError("unknown category ID")
        rows = self._db.execute("""
            WITH samples AS (SELECT unnest(?::BIGINT[]) AS id, unnest(?::DOUBLE[]) AS value),
            totals AS MATERIALIZED (SELECT id, sum(value) AS total FROM samples GROUP BY id),
            ranked AS (
                SELECT id, total, row_number() OVER (ORDER BY total DESC NULLS LAST, id) AS rank
                FROM totals WHERE id IS NOT NULL
            ), tagged AS (
                SELECT CASE WHEN rank <= ? THEN 'category' ELSE 'other' END AS kind,
                       CASE WHEN rank <= ? THEN id END AS category_id,
                       CASE WHEN rank <= ? THEN rank END AS display_rank, total
                FROM ranked
                UNION ALL SELECT 'missing', NULL, NULL, total FROM totals WHERE id IS NULL
            )
            SELECT kind, category_id, sum(total) FROM tagged
            GROUP BY kind, category_id, display_rank
            ORDER BY display_rank NULLS LAST, kind DESC
        """, [categories, values, limit, limit, limit]).fetchall()
        special = {"other": "Other", "missing": "(Missing)"}
        return [CategoryTotal(k, i, labels[i] if i is not None else special[k], amount)
                for k, i, amount in rows]

    def degree_distribution(self, vertices: list[int], edges: list[tuple[int, int, int]]) -> list[DegreeCount]:
        """Count weighted directed degrees with one incoming and outgoing contribution per loop."""
        if type(vertices) is not list or any(not self._is_id(v) for v in vertices):
            raise ValueError("vertices must be a list of integer IDs")
        known = set(vertices)
        if len(known) != len(vertices):
            raise ValueError("vertex IDs must be unique")
        if type(edges) is not list:
            raise TypeError("edges must be a list")
        sources, targets, weights = [], [], []
        for edge in edges:
            if type(edge) is not tuple or len(edge) != 3:
                raise TypeError("expected (source, target, weight) tuples")
            source, target, weight = edge
            if not self._is_id(source) or not self._is_id(target) or source not in known or target not in known:
                raise ValueError("edge endpoints must be registered vertices")
            if not self._is_id(weight) or weight <= 0:
                raise ValueError("edge weights must be positive signed 64-bit integers")
            sources.append(source)
            targets.append(target)
            weights.append(weight)
        rows = self._db.execute("""
            WITH vertices AS (SELECT unnest(?::BIGINT[]) AS id),
            edges AS (
                SELECT unnest(?::BIGINT[]) AS source, unnest(?::BIGINT[]) AS target,
                       unnest(?::BIGINT[]) AS weight
            ), endpoints AS (
                SELECT unnest(['out', 'in']) AS direction, unnest([source, target]) AS id, weight FROM edges
            ), degrees AS (
                SELECT direction, id, sum(weight) AS degree FROM endpoints GROUP BY direction, id
            ), completed AS (
                SELECT d.direction, coalesce(degree, 0) AS degree FROM vertices
                CROSS JOIN (VALUES ('in'), ('out')) AS d(direction)
                LEFT JOIN degrees USING (id, direction)
            )
            SELECT direction, degree, count(*) FROM completed GROUP BY direction, degree
            ORDER BY direction, degree
        """, [vertices, sources, targets, weights]).fetchall()
        return [DegreeCount(*row) for row in rows]

    def weighted_means(self, groups: list[int], values: list[float | None],
                       weights: list[float]) -> list[WeightedMean]:
        """Keep the numerator and valid weight for each group for later rollups."""
        self._validate_series(groups, values)
        self._validate_series(groups, weights)
        if any(weight is None or weight < 0 for weight in weights):
            raise ValueError("weights must be nonnegative finite numbers")
        rows = self._db.execute("""
            WITH samples AS (
                SELECT unnest(?::BIGINT[]) AS id, unnest(?::DOUBLE[]) AS value,
                       unnest(?::DOUBLE[]) AS weight
            )
            SELECT id, coalesce(fsum(value * weight), 0),
                   coalesce(fsum(weight) FILTER (WHERE value IS NOT NULL), 0)
            FROM samples GROUP BY id ORDER BY id
        """, [groups, values, weights]).fetchall()
        return [WeightedMean(*row) for row in rows]

    def combine_weighted_means(self, groups: list[WeightedMean],
                               parents: dict[int, int]) -> list[WeightedMean]:
        """Roll up summaries using only their two stored aggregate components."""
        if type(groups) is not list or any(type(group) is not WeightedMean for group in groups):
            raise TypeError("groups must be a list of WeightedMean values")
        if len({group.group_id for group in groups}) != len(groups):
            raise ValueError("each child group can appear once")
        if type(parents) is not dict or any(not self._is_id(k) or not self._is_id(v) for k, v in parents.items()):
            raise ValueError("parents must map integer child IDs to integer parent IDs")
        rows = self._db.execute("""
            WITH components AS (
                SELECT unnest(?::BIGINT[]) AS id, unnest(?::DOUBLE[]) AS numerator,
                       unnest(?::DOUBLE[]) AS weight
            )
            SELECT id, fsum(numerator), fsum(weight) FROM components GROUP BY id ORDER BY id
        """, [[parents[g.group_id] for g in groups], [g.weighted_sum for g in groups],
              [g.weight for g in groups]]).fetchall()
        return [WeightedMean(*row) for row in rows]

    def error_bars(self, groups: list[int], values: list[float | None], *,
                   method: ErrorBarMethod) -> list[ErrorBar]:
        """Calculate sample variance and an interval around each group mean."""
        self._validate_series(groups, values)
        if type(method) is not ErrorBarMethod:
            raise TypeError("expected an ErrorBarMethod")
        rows = self._db.execute("""
            WITH observations AS (
                SELECT unnest(?::BIGINT[]) AS id, unnest(?::DOUBLE[]) AS value
            ), moments AS (
                SELECT id, count(value) AS n, favg(value) AS mean, var_samp(value) AS variance
                FROM observations GROUP BY id
            ), intervals AS (
                SELECT *, ? * sqrt(variance / n) AS radius FROM moments
            )
            SELECT id, n, mean, variance, mean - radius, mean + radius,
                   CASE WHEN n = 0 THEN 'no_samples' WHEN n = 1 THEN 'insufficient_samples' END
            FROM intervals ORDER BY id
        """, [groups, values, method.value]).fetchall()
        return [ErrorBar(*row[:-1], method, row[-1]) for row in rows]
