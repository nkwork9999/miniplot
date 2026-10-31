// Included inside namespace duckdb after the shared chart helpers.
enum class ExtraChartKind { PIE, DONUT, HORIZONTAL_BAR, VIOLIN, BUBBLE, HEATMAP, CONTOUR, STEP, RADAR, SCATTER_GL };

static string StringArrayJSON(const std::vector<string> &values) {
	string out = "[";
	for (idx_t i = 0; i < values.size(); i++) {
		if (i)
			out += ',';
		out += QuoteJSON(values[i]);
	}
	return out + ']';
}

static void RequireSamples(const std::vector<double> &values) {
	if (values.empty())
		throw InvalidInputException("Data arrays cannot be empty");
}
static void RequireNonnegative(const std::vector<double> &values) {
	for (auto value : values) {
		if (value < 0)
			throw InvalidInputException("Values must be nonnegative");
	}
}
static void RequireIncreasing(const std::vector<double> &values) {
	for (idx_t i = 1; i < values.size(); i++) {
		if (values[i] <= values[i - 1])
			throw InvalidInputException("Grid axes must be strictly increasing");
	}
}

template <bool RETURN_HTML, ExtraChartKind KIND>
static void ExtraChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	const bool labels = KIND == ExtraChartKind::PIE || KIND == ExtraChartKind::DONUT ||
	                    KIND == ExtraChartKind::HORIZONTAL_BAR || KIND == ExtraChartKind::RADAR;
	const bool matrix = KIND == ExtraChartKind::HEATMAP || KIND == ExtraChartKind::CONTOUR;
	const idx_t title_col = KIND == ExtraChartKind::VIOLIN ? 1 : matrix || KIND == ExtraChartKind::BUBBLE ? 3 : 2;
	for (idx_t row = 0; row < args.size(); row++) {
		if (RowHasNull(args, row)) {
			result.SetValue(row, Value());
			continue;
		}
		string trace;
		string axes;
		if (labels) {
			auto x = ExtractStringList(args.data[0], row);
			auto y = ExtractDoubleList(args.data[1], row);
			RequireSamples(y);
			if (x.size() != y.size())
				throw InvalidInputException("Labels and values must have the same length");
			auto x_json = StringArrayJSON(x);
			auto y_json = NumberArrayJSON(y);
			if (KIND == ExtraChartKind::HORIZONTAL_BAR) {
				trace = "{\"type\":\"bar\",\"y\":" + x_json + ",\"x\":" + y_json + ",\"orientation\":\"h\"}";
			} else if (KIND == ExtraChartKind::RADAR) {
				RequireNonnegative(y);
				trace = "{\"type\":\"scatterpolar\",\"theta\":" + x_json + ",\"r\":" + y_json + ",\"fill\":\"toself\"}";
			} else {
				RequireNonnegative(y);
				if (std::none_of(y.begin(), y.end(), [](double v) { return v > 0; }))
					throw InvalidInputException("Pie values must contain a positive value");
				trace = "{\"type\":\"pie\",\"labels\":" + x_json + ",\"values\":" + y_json;
				if (KIND == ExtraChartKind::DONUT)
					trace += ",\"hole\":0.5";
				trace += '}';
			}
		} else {
			auto x = ExtractDoubleList(args.data[0], row);
			RequireSamples(x);
			if (KIND == ExtraChartKind::VIOLIN) {
				trace = "{\"type\":\"violin\",\"y\":" + NumberArrayJSON(x) + '}';
			} else {
				auto y = ExtractDoubleList(args.data[1], row);
				RequireSamples(y);
				if (!matrix && x.size() != y.size())
					throw InvalidInputException("x and y arrays must have the same length");
				string fields = "\"x\":" + NumberArrayJSON(x) + ",\"y\":" + NumberArrayJSON(y);
				if (matrix) {
					RequireIncreasing(x);
					RequireIncreasing(y);
					auto value = args.data[2].GetValue(row);
					auto &rows = ListValue::GetChildren(value);
					if (rows.size() != y.size())
						throw InvalidInputException("Grid row count must match y length");
					string z = "[";
					for (idx_t j = 0; j < rows.size(); j++) {
						if (rows[j].IsNull())
							throw InvalidInputException("Grid rows cannot be NULL");
						auto &cells = ListValue::GetChildren(rows[j]);
						if (cells.size() != x.size())
							throw InvalidInputException("Grid column count must match x length");
						std::vector<double> values;
						values.reserve(cells.size());
						for (auto &cell : cells) {
							if (cell.IsNull())
								throw InvalidInputException("Grid values cannot be NULL");
							auto number = cell.GetValue<double>();
							if (!std::isfinite(number))
								throw InvalidInputException("Grid values must be finite");
							values.push_back(number);
						}
						if (j)
							z += ',';
						z += NumberArrayJSON(values);
					}
					z += ']';
					trace = "{\"type\":\"" + string(KIND == ExtraChartKind::HEATMAP ? "heatmap" : "contour") + "\"," +
					        fields + ",\"z\":" + z + '}';
				} else if (KIND == ExtraChartKind::BUBBLE) {
					auto sizes = ExtractDoubleList(args.data[2], row);
					if (sizes.size() != x.size())
						throw InvalidInputException("Bubble sizes must match x and y length");
					RequireNonnegative(sizes);
					trace = "{\"type\":\"scatter\",\"mode\":\"markers\"," + fields +
					        ",\"marker\":{\"size\":" + NumberArrayJSON(sizes) + "}}";
				} else {
					trace = "{\"type\":\"" + string(KIND == ExtraChartKind::SCATTER_GL ? "scattergl" : "scatter") +
					        "\",\"mode\":\"" + (KIND == ExtraChartKind::STEP ? "lines" : "markers") + "\"," + fields;
					if (KIND == ExtraChartKind::STEP)
						trace += ",\"line\":{\"shape\":\"hv\"}";
					trace += '}';
					axes = ",\"xaxis\":{\"type\":\"linear\"},\"yaxis\":{\"type\":\"linear\"}";
				}
			}
		}
		auto title = args.data[title_col].GetValue(row).ToString();
		auto html = GeneratePlotlyHTML("[" + trace + "]", "{\"title\":{\"text\":" + QuoteJSON(title) + "}" + axes + "}",
		                               "{\"responsive\":true}", title);
		if (RETURN_HTML)
			result.SetValue(row, Value(html));
		else
			SaveChartHTML(html,
			              args.ColumnCount() > title_col + 1 ? args.data[title_col + 1].GetValue(row).ToString() : "",
			              result, row);
	}
}
