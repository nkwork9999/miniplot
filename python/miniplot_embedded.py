"""A time-series HTML chart rendered on a host-provisioned Miniplot connection."""
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import datetime, timedelta
from enum import Enum
from itertools import chain
from json import dumps
from math import exp, isfinite, log
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
