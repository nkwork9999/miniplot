#define DUCKDB_EXTENSION_MAIN

#include "miniplot_extension.hpp"
#include "chart_html.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "yyjson.hpp"

#include <openssl/opensslv.h>

#include <fstream>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <random>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace duckdb {
using namespace miniplot_html;

// Helper: Extract string list
static std::vector<string> ExtractStringList(const Vector &list_vector, idx_t row_idx) {
	std::vector<string> result;
	try {
		auto val = list_vector.GetValue(row_idx);
		if (val.IsNull() || val.type().id() != LogicalTypeId::LIST) {
			return result;
		}
		auto &children = ListValue::GetChildren(val);
		for (idx_t i = 0; i < children.size(); i++) {
			if (children[i].IsNull()) {
				throw InvalidInputException("Chart arrays cannot contain NULL elements");
			}
			{
				result.push_back(children[i].ToString());
			}
		}
	} catch (const std::exception &e) {
		throw InvalidInputException("Failed to extract string list: %s", e.what());
	}
	return result;
}

// Helper: Extract double list
static std::vector<double> ExtractDoubleList(const Vector &list_vector, idx_t row_idx) {
	std::vector<double> result;
	try {
		auto val = list_vector.GetValue(row_idx);
		if (val.IsNull() || val.type().id() != LogicalTypeId::LIST) {
			return result;
		}
		auto &children = ListValue::GetChildren(val);
		for (idx_t i = 0; i < children.size(); i++) {
			if (children[i].IsNull()) {
				throw InvalidInputException("Chart arrays cannot contain NULL elements");
			}
			{
				auto number = children[i].GetValue<double>();
				if (!std::isfinite(number)) {
					throw InvalidInputException("Chart values must be finite");
				}
				result.push_back(number);
			}
		}
	} catch (const std::exception &e) {
		throw InvalidInputException("Failed to extract double list: %s", e.what());
	}
	return result;
}

// Generate safe filename with random hex and process ID
static string GenerateSafeFilename() {
	std::random_device rd;
	std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dis;

	std::ostringstream ss;
#ifdef _WIN32
	const char *temp_dir = getenv("TEMP");
	if (!temp_dir)
		temp_dir = "C:\\Windows\\Temp";
	ss << temp_dir << "\\duckdb_chart_";
#else
	ss << "/tmp/duckdb_chart_";
#endif

	// 128-bit random hex
	ss << std::hex << dis(gen) << dis(gen);

	// Add process ID to avoid collisions
	ss << "_" << std::dec << getpid();

	ss << ".html";
	return ss.str();
}

static void OpenInBrowser(const string &html_path) {
#ifdef _WIN32
	system(("cmd /C start \"\" \"" + html_path + "\"").c_str());
#elif __APPLE__
	system(("open \"" + html_path + "\"").c_str());
#else
	system(("xdg-open \"" + html_path + "\" || sensible-browser \"" + html_path +
	        "\" || "
	        "x-www-browser \"" +
	        html_path + "\" &")
	           .c_str());
#endif
}

// Helper: Convert string to lowercase
static string ToLowerCase(const string &str) {
	string lower = str;
	std::transform(lower.begin(), lower.end(), lower.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower;
}

// All file-producing functions share the existing browser/tmp/custom-path contract.
static void SaveChartHTML(const string &html_content, const string &output_path_arg, Vector &result, idx_t row_idx) {
	const bool open_browser = output_path_arg.empty();
	const string html_path =
	    open_browser || ToLowerCase(output_path_arg) == "tmp" ? GenerateSafeFilename() : output_path_arg;
	if (html_path.find('\0') != string::npos) {
		throw InvalidInputException("Chart output path cannot contain NUL bytes");
	}
	std::ofstream file(html_path);
	if (!file.is_open()) {
		throw IOException("Failed to create chart file: %s", html_path);
	}
	file << html_content;
	file.close();
	if (!file) {
		throw IOException("Failed to write chart file: %s", html_path);
	}
	if (open_browser) {
		OpenInBrowser(html_path);
	}
	result.SetValue(row_idx, Value(html_path));
}

static bool RowHasNull(DataChunk &args, idx_t row) {
	for (idx_t col = 0; col < args.ColumnCount(); col++) {
		if (args.data[col].GetValue(row).IsNull()) {
			return true;
		}
	}
	return false;
}

static void ValidatePlotlyJSON(const string &json, const char *argument, bool is_data) {
	using namespace duckdb_yyjson;
	std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(json.data(), json.size(), 0),
	                                                            yyjson_doc_free);
	if (!doc) {
		throw InvalidInputException("plotly_new: %s must be valid JSON", argument);
	}
	auto root = yyjson_doc_get_root(doc.get());
	if (is_data) {
		if (!yyjson_is_arr(root)) {
			throw InvalidInputException("plotly_new: data must be a JSON array of trace objects");
		}
		size_t idx, max;
		yyjson_val *trace;
		yyjson_arr_foreach(root, idx, max, trace) {
			if (!yyjson_is_obj(trace)) {
				throw InvalidInputException("plotly_new: each data entry must be a JSON trace object");
			}
		}
	} else if (!yyjson_is_obj(root)) {
		throw InvalidInputException("plotly_new: %s must be a JSON object", argument);
	}
}

template <bool RETURN_HTML>
static void PlotlyNewFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		if (RowHasNull(args, row)) {
			result.SetValue(row, Value());
			continue;
		}
		auto data = args.data[0].GetValue(row).ToString();
		auto layout = args.data[1].GetValue(row).ToString();
		auto config = args.ColumnCount() >= 3 ? args.data[2].GetValue(row).ToString() : "{}";
		ValidatePlotlyJSON(data, "data", true);
		ValidatePlotlyJSON(layout, "layout", false);
		ValidatePlotlyJSON(config, "config", false);
		auto html = GeneratePlotlyHTML(data, layout, config);
		if (RETURN_HTML) {
			result.SetValue(row, Value(html));
		} else {
			auto path = args.ColumnCount() == 4 ? args.data[3].GetValue(row).ToString() : "";
			SaveChartHTML(html, path, result, row);
		}
	}
}

static string ScatterAxisJSON(const Value &values, bool &is_date) {
	const auto &children = ListValue::GetChildren(values);
	auto type = ListType::GetChildType(values.type()).id();
	is_date = type != LogicalTypeId::DOUBLE;
	string json = "[";
	for (idx_t i = 0; i < children.size(); i++) {
		const auto &value = children[i];
		if (value.IsNull()) {
			throw InvalidInputException("Chart arrays cannot contain NULL elements");
		}
		if (i) {
			json += ',';
		}
		if (type == LogicalTypeId::DOUBLE) {
			auto number = value.GetValue<double>();
			if (!std::isfinite(number)) {
				throw InvalidInputException("Chart values must be finite");
			}
			json += NumberLabel(number);
		} else {
			string label;
			if (type == LogicalTypeId::DATE) {
				auto date = DateValue::Get(value);
				if (!Date::IsFinite(date)) {
					throw InvalidInputException("Chart dates must be finite");
				}
				label = Date::ToString(date);
			} else {
				auto timestamp = type == LogicalTypeId::TIMESTAMP_TZ ? timestamp_t(TimestampTZValue::Get(value).value)
				                                                     : TimestampValue::Get(value);
				if (!Timestamp::IsFinite(timestamp)) {
					throw InvalidInputException("Chart timestamps must be finite");
				}
				// TIMESTAMPTZ is an instant: present it in UTC on Plotly's date axis.
				// TIMESTAMP is a wall-clock value and keeps its original fields.
				label = Timestamp::ToString(timestamp);
			}
			json += QuoteJSON(label);
		}
	}
	return json + ']';
}

enum class TypedChartKind { SCATTER, LINE, AREA };

template <bool RETURN_HTML, TypedChartKind KIND = TypedChartKind::SCATTER>
static void TypedChartExecute(DataChunk &args, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		if (RowHasNull(args, row)) {
			result.SetValue(row, Value());
			continue;
		}
		auto x = args.data[0].GetValue(row);
		auto y = args.data[1].GetValue(row);
		const auto count = ListValue::GetChildren(x).size();
		if (count != ListValue::GetChildren(y).size()) {
			throw InvalidInputException("x and y arrays must have the same length");
		}
		if (!count) {
			throw InvalidInputException("Data arrays cannot be empty");
		}
		bool x_date, y_date;
		auto x_json = ScatterAxisJSON(x, x_date);
		auto y_json = ScatterAxisJSON(y, y_date);
		auto title = args.data[2].GetValue(row).ToString();
		const char *mode = KIND == TypedChartKind::SCATTER ? "markers"
		                   : KIND == TypedChartKind::LINE  ? "lines+markers"
		                                                   : "lines";
		auto data = "[{\"type\":\"scatter\",\"mode\":\"" + string(mode) + "\",\"x\":" + x_json + ",\"y\":" + y_json;
		if (KIND == TypedChartKind::AREA) {
			data += ",\"fill\":\"tozeroy\"";
		}
		data += "}]";
		auto layout = "{\"title\":{\"text\":" + QuoteJSON(title) + "},\"xaxis\":{\"type\":\"" +
		              (x_date ? "date" : "linear") + "\"},\"yaxis\":{\"type\":\"" + (y_date ? "date" : "linear") +
		              "\"}}";
		auto html = GeneratePlotlyHTML(data, layout, "{\"responsive\":true}", title);
		if (RETURN_HTML) {
			result.SetValue(row, Value(html));
		} else {
			auto path = args.ColumnCount() == 4 ? args.data[3].GetValue(row).ToString() : "";
			SaveChartHTML(html, path, result, row);
		}
	}
}

template <bool RETURN_HTML, TypedChartKind KIND>
static void TypedChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	TypedChartExecute<RETURN_HTML, KIND>(args, result);
}

// Distribution charts keep raw samples; Plotly performs binning/quartile calculation.
template <bool RETURN_HTML, bool HISTOGRAM>
static void DistributionChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		if (RowHasNull(args, row)) {
			result.SetValue(row, Value());
			continue;
		}
		auto samples = ExtractDoubleList(args.data[0], row);
		if (samples.empty()) {
			throw InvalidInputException("Data arrays cannot be empty");
		}
		string values = "[";
		for (idx_t i = 0; i < samples.size(); i++) {
			if (i) {
				values += ',';
			}
			values += NumberLabel(samples[i]);
		}
		values += ']';
		auto title = args.data[1].GetValue(row).ToString();
		auto data =
		    HISTOGRAM ? "[{\"type\":\"histogram\",\"x\":" + values + "}]" : "[{\"type\":\"box\",\"y\":" + values + "}]";
		auto layout = "{\"title\":{\"text\":" + QuoteJSON(title) + "}}";
		auto html = GeneratePlotlyHTML(data, layout, "{\"responsive\":true}", title);
		if (RETURN_HTML) {
			result.SetValue(row, Value(html));
		} else {
			auto path = args.ColumnCount() == 3 ? args.data[2].GetValue(row).ToString() : "";
			SaveChartHTML(html, path, result, row);
		}
	}
}

// Generic chart creation with path control
static void CreateChartWithPath(const std::vector<string> &x_data, const std::vector<double> &y_data,
                                const string &title, const string &chart_type, const string &output_path_arg,
                                Vector &result, idx_t row_idx) {
	if (x_data.size() != y_data.size()) {
		throw InvalidInputException("x and y arrays must have the same length");
	}
	if (x_data.empty()) {
		throw InvalidInputException("Data arrays cannot be empty");
	}

	string html_content = GenerateHTML(x_data, y_data, title, chart_type);
	SaveChartHTML(html_content, output_path_arg, result, row_idx);
}

// Generic 3D chart creation with path control
static void Create3DChartWithPath(const std::vector<double> &x_data, const std::vector<double> &y_data,
                                  const std::vector<double> &z_data, const string &title, const string &output_path_arg,
                                  Vector &result, idx_t row_idx) {
	if (x_data.size() != y_data.size() || x_data.size() != z_data.size()) {
		throw InvalidInputException("x, y, and z arrays must have the same length");
	}
	if (x_data.empty()) {
		throw InvalidInputException("Data arrays cannot be empty");
	}

	string html_content = Generate3DScatterHTML(x_data, y_data, z_data, title);
	SaveChartHTML(html_content, output_path_arg, result, row_idx);
}

// Generic 3D chart with timestamp creation with path control
static void Create3DChartWithTimestampAndPath(const std::vector<double> &x_data, const std::vector<double> &y_data,
                                              const std::vector<double> &z_data, const std::vector<string> &timestamps,
                                              const string &title, const string &output_path_arg, Vector &result,
                                              idx_t row_idx) {
	if (x_data.size() != y_data.size() || x_data.size() != z_data.size() || x_data.size() != timestamps.size()) {
		throw InvalidInputException("x, y, z, and timestamp arrays must have the same length");
	}
	if (x_data.empty()) {
		throw InvalidInputException("Data arrays cannot be empty");
	}

	string html_content = Generate3DScatterHTMLWithTimestamp(x_data, y_data, z_data, timestamps, title);
	SaveChartHTML(html_content, output_path_arg, result, row_idx);
}

// ============================================
// 2D Chart Functions (3 args - browser mode)
// ============================================

inline void BarChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();
		CreateChartWithPath(x_strings, y_values, title_str, "bar", "", result, row);
	}
}

inline void LineChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();
		CreateChartWithPath(x_strings, y_values, title_str, "line", "", result, row);
	}
}

inline void ScatterChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	TypedChartExecute<false>(args, result);
}

inline void AreaChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();
		CreateChartWithPath(x_strings, y_values, title_str, "area", "", result, row);
	}
}

// ============================================
// 2D Chart Functions (4 args - with output path)
// ============================================

inline void BarChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();
		string output_path = args.data[3].GetValue(row).ToString();
		CreateChartWithPath(x_strings, y_values, title_str, "bar", output_path, result, row);
	}
}

inline void LineChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();
		string output_path = args.data[3].GetValue(row).ToString();
		CreateChartWithPath(x_strings, y_values, title_str, "line", output_path, result, row);
	}
}

inline void ScatterChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	TypedChartExecute<false>(args, result);
}

inline void AreaChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();
		string output_path = args.data[3].GetValue(row).ToString();
		CreateChartWithPath(x_strings, y_values, title_str, "area", output_path, result, row);
	}
}

// ============================================
// 3D Chart Functions (4 args - browser mode, no timestamp)
// ============================================

inline void Scatter3DChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_values = ExtractDoubleList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		auto z_values = ExtractDoubleList(args.data[2], row);
		string title_str = args.data[3].GetValue(row).ToString();
		Create3DChartWithPath(x_values, y_values, z_values, title_str, "", result, row);
	}
}

// ============================================
// 3D Chart Functions (5 args - with output path, no timestamp)
// ============================================

inline void Scatter3DChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_values = ExtractDoubleList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		auto z_values = ExtractDoubleList(args.data[2], row);
		string title_str = args.data[3].GetValue(row).ToString();
		string output_path = args.data[4].GetValue(row).ToString();
		Create3DChartWithPath(x_values, y_values, z_values, title_str, output_path, result, row);
	}
}

// ============================================
// 3D Chart Functions (5 args - browser mode, with timestamp)
// ============================================

inline void Scatter3DChartWithTimestampFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_values = ExtractDoubleList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		auto z_values = ExtractDoubleList(args.data[2], row);
		auto timestamps = ExtractStringList(args.data[3], row);
		string title_str = args.data[4].GetValue(row).ToString();
		Create3DChartWithTimestampAndPath(x_values, y_values, z_values, timestamps, title_str, "", result, row);
	}
}

// ============================================
// 3D Chart Functions (6 args - with output path and timestamp)
// ============================================

inline void Scatter3DChartWithTimestampAndPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_values = ExtractDoubleList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		auto z_values = ExtractDoubleList(args.data[2], row);
		auto timestamps = ExtractStringList(args.data[3], row);
		string title_str = args.data[4].GetValue(row).ToString();
		string output_path = args.data[5].GetValue(row).ToString();
		Create3DChartWithTimestampAndPath(x_values, y_values, z_values, timestamps, title_str, output_path, result,
		                                  row);
	}
}

// ============================================
// HTML返却関数 (WASM対応) - 2D Charts
// ============================================

inline void BarChartHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();

		if (x_strings.size() != y_values.size()) {
			throw InvalidInputException("x and y arrays must have the same length");
		}
		if (x_strings.empty()) {
			throw InvalidInputException("Data arrays cannot be empty");
		}

		string html_content = GenerateHTML(x_strings, y_values, title_str, "bar");
		result.SetValue(row, Value(html_content));
	}
}

inline void LineChartHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();

		if (x_strings.size() != y_values.size()) {
			throw InvalidInputException("x and y arrays must have the same length");
		}
		if (x_strings.empty()) {
			throw InvalidInputException("Data arrays cannot be empty");
		}

		string html_content = GenerateHTML(x_strings, y_values, title_str, "line");
		result.SetValue(row, Value(html_content));
	}
}

inline void ScatterChartHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	TypedChartExecute<true>(args, result);
}

inline void AreaChartHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_strings = ExtractStringList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		string title_str = args.data[2].GetValue(row).ToString();

		if (x_strings.size() != y_values.size()) {
			throw InvalidInputException("x and y arrays must have the same length");
		}
		if (x_strings.empty()) {
			throw InvalidInputException("Data arrays cannot be empty");
		}

		string html_content = GenerateHTML(x_strings, y_values, title_str, "area");
		result.SetValue(row, Value(html_content));
	}
}

// ============================================
// HTML返却関数 (WASM対応) - 3D Charts
// ============================================

inline void Scatter3DChartHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_values = ExtractDoubleList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		auto z_values = ExtractDoubleList(args.data[2], row);
		string title_str = args.data[3].GetValue(row).ToString();

		if (x_values.size() != y_values.size() || x_values.size() != z_values.size()) {
			throw InvalidInputException("x, y, and z arrays must have the same length");
		}
		if (x_values.empty()) {
			throw InvalidInputException("Data arrays cannot be empty");
		}

		string html_content = Generate3DScatterHTML(x_values, y_values, z_values, title_str);
		result.SetValue(row, Value(html_content));
	}
}

inline void Scatter3DChartHTMLWithTimestampFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		bool any_null = false;
		for (idx_t col = 0; col < args.ColumnCount(); col++) {
			if (args.data[col].GetValue(row).IsNull()) {
				any_null = true;
				break;
			}
		}
		if (any_null) {
			result.SetValue(row, Value());
			continue;
		}

		auto x_values = ExtractDoubleList(args.data[0], row);
		auto y_values = ExtractDoubleList(args.data[1], row);
		auto z_values = ExtractDoubleList(args.data[2], row);
		auto timestamps = ExtractStringList(args.data[3], row);
		string title_str = args.data[4].GetValue(row).ToString();

		if (x_values.size() != y_values.size() || x_values.size() != z_values.size() ||
		    x_values.size() != timestamps.size()) {
			throw InvalidInputException("x, y, z, and timestamp arrays must have the same length");
		}
		if (x_values.empty()) {
			throw InvalidInputException("Data arrays cannot be empty");
		}

		string html_content = Generate3DScatterHTMLWithTimestamp(x_values, y_values, z_values, timestamps, title_str);
		result.SetValue(row, Value(html_content));
	}
}

// ============================================
// Version functions
// ============================================

inline void MiniplotScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name, result, args.size(), [&](string_t name) {
		std::string version_string = "Miniplot " + name.GetString() + " 🐥";
		return StringVector::AddString(result, version_string);
	});
}

inline void MiniplotOpenSSLVersionFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name, result, args.size(), [&](string_t name) {
		std::string version_string =
		    "Miniplot " + name.GetString() + ", my linked OpenSSL version is " + OPENSSL_VERSION_TEXT;
		return StringVector::AddString(result, version_string);
	});
}

// ============================================
// Extension Load
// ============================================

// Use the same documented registrations for both extension entry points.
static vector<CreateScalarFunctionInfo> MiniplotFunctions() {
	vector<CreateScalarFunctionInfo> functions;
	auto add = [&](const string &name, vector<LogicalType> types, scalar_function_t callback, vector<string> parameters,
	               const string &description, const string &example, bool writes_file, bool can_throw = true) {
		ScalarFunction function(name, types, LogicalType::VARCHAR, callback);
		if (writes_file) {
			function.SetVolatile();
		}
		if (can_throw) {
			function.SetFallible();
		}
		CreateScalarFunctionInfo info(std::move(function));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription doc;
		doc.parameter_types = std::move(types);
		doc.parameter_names = std::move(parameters);
		doc.description = description;
		doc.examples = {example};
		doc.categories = {"visualization"};
		info.descriptions.push_back(std::move(doc));
		functions.push_back(std::move(info));
	};
	const auto text = LogicalType::VARCHAR;
	const auto labels = LogicalType::LIST(text);
	const auto numbers = LogicalType::LIST(LogicalType::DOUBLE);
	add("miniplot", {text}, MiniplotScalarFun, {"name"}, "Return a Miniplot greeting for name.", "miniplot('Sam')",
	    false, false);
	add("miniplot_openssl_version", {text}, MiniplotOpenSSLVersionFunction, {"name"},
	    "Return a greeting and the OpenSSL version used to compile Miniplot.", "miniplot_openssl_version('Sam')", false,
	    false);

	const string browser = " Save an HTML chart, open the browser, and return the file path.";
	const string file = " Save an HTML chart and return its path; output_path='tmp' creates a temporary file without "
	                    "opening the browser, a custom path saves there, and an empty path opens the browser.";
	const string html = " Return the HTML document without writing a file or opening the browser.";
	struct Chart2D {
		const char *name;
		const char *kind;
		scalar_function_t browser;
		scalar_function_t file;
		scalar_function_t html;
	};
	for (const auto &chart :
	     {Chart2D {"bar_chart", "bar", BarChartFunction, BarChartWithPathFunction, BarChartHTMLFunction},
	      Chart2D {"line_chart", "line", LineChartFunction, LineChartWithPathFunction, LineChartHTMLFunction},
	      Chart2D {"area_chart", "area", AreaChartFunction, AreaChartWithPathFunction, AreaChartHTMLFunction}}) {
		const string name = chart.name;
		const string description =
		    "Plot a " + string(chart.kind) + " chart from equal-length nonempty labels and values.";
		const string example = "(['A', 'B'], [1.0, 2.0], 'Example'";
		add(name, {labels, numbers, text}, chart.browser, {"x", "y", "title"}, description + browser,
		    name + example + ")", true);
		add(name, {labels, numbers, text, text}, chart.file, {"x", "y", "title", "output_path"}, description + file,
		    name + example + ", 'tmp')", true);
		add(name + "_html", {labels, numbers, text}, chart.html, {"x", "y", "title"}, description + html,
		    name + "_html" + example + ")", false);
	}

	// Explicit overloads preserve the numeric API while allowing either date axis.
	const vector<LogicalType> axis_types = {LogicalType::DOUBLE, LogicalType::DATE, LogicalType::TIMESTAMP,
	                                        LogicalType::TIMESTAMP_TZ};
	const vector<string> axis_examples = {
	    "[1.0, 2.0]", "[DATE '2026-01-01', DATE '2026-01-02']",
	    "[TIMESTAMP '2026-01-01 10:00:00', TIMESTAMP '2026-01-01 11:00:00']",
	    "[TIMESTAMPTZ '2026-01-01 10:00:00+00', TIMESTAMPTZ '2026-01-01 11:00:00+00']"};
	for (idx_t x = 0; x < axis_types.size(); x++) {
		for (idx_t y = 0; y < axis_types.size(); y++) {
			auto types = vector<LogicalType> {LogicalType::LIST(axis_types[x]), LogicalType::LIST(axis_types[y]), text};
			const string example = "(" + axis_examples[x] + ", " + axis_examples[y] + ", 'Example'";
			const string description =
			    "Plot a 2D scatter chart from equal-length nonempty numeric or date/time arrays; "
			    "TIMESTAMP keeps wall-clock fields and TIMESTAMPTZ is shown in UTC.";
			add("scatter_chart", types, ScatterChartFunction, {"x", "y", "title"}, description + browser,
			    "scatter_chart" + example + ")", true);
			add("scatter_chart_html", types, ScatterChartHTMLFunction, {"x", "y", "title"}, description + html,
			    "scatter_chart_html" + example + ")", false);
			types.push_back(text);
			add("scatter_chart", types, ScatterChartWithPathFunction, {"x", "y", "title", "output_path"},
			    description + file, "scatter_chart" + example + ", 'tmp')", true);
		}
	}

	// Numeric and temporal x axes share scatter's precise serialization and validation.
	struct TypedChart {
		const char *name;
		scalar_function_t file;
		scalar_function_t html;
	};
	for (const auto &chart : {TypedChart {"line_chart", TypedChartFunction<false, TypedChartKind::LINE>,
	                                      TypedChartFunction<true, TypedChartKind::LINE>},
	                          TypedChart {"area_chart", TypedChartFunction<false, TypedChartKind::AREA>,
	                                      TypedChartFunction<true, TypedChartKind::AREA>}}) {
		const string name = chart.name;
		for (idx_t x = 0; x < axis_types.size(); x++) {
			auto types = vector<LogicalType> {LogicalType::LIST(axis_types[x]), numbers, text};
			const string example = "(" + axis_examples[x] + ", [1.0, 2.0], 'Example'";
			const string description =
			    "Plot equal-length nonempty numeric or date/time x values and numeric y values; "
			    "numeric spacing is preserved, TIMESTAMP keeps wall-clock fields and TIMESTAMPTZ is shown in UTC.";
			add(name, types, chart.file, {"x", "y", "title"}, description + browser, name + example + ")", true);
			add(name + "_html", types, chart.html, {"x", "y", "title"}, description + html,
			    name + "_html" + example + ")", false);
			types.push_back(text);
			add(name, types, chart.file, {"x", "y", "title", "output_path"}, description + file,
			    name + example + ", 'tmp')", true);
		}
	}

	const string d3 = "Plot a 3D scatter chart from equal-length nonempty x, y and z arrays.";
	const string e3 = "([1.0, 2.0], [3.0, 4.0], [5.0, 6.0], 'Example'";
	add("scatter_3d_chart", {numbers, numbers, numbers, text}, Scatter3DChartFunction, {"x", "y", "z", "title"},
	    d3 + browser, "scatter_3d_chart" + e3 + ")", true);
	add("scatter_3d_chart", {numbers, numbers, numbers, text, text}, Scatter3DChartWithPathFunction,
	    {"x", "y", "z", "title", "output_path"}, d3 + file, "scatter_3d_chart" + e3 + ", 'tmp')", true);
	add("scatter_3d_chart_html", {numbers, numbers, numbers, text}, Scatter3DChartHTMLFunction,
	    {"x", "y", "z", "title"}, d3 + html, "scatter_3d_chart_html" + e3 + ")", false);
	const string dt = "Plot a 3D scatter chart with a timestamp/hover label for each point.";
	const string et = "([1.0], [2.0], [3.0], ['2026-01-01 10:00'], 'Example'";
	add("scatter_3d_chart", {numbers, numbers, numbers, labels, text}, Scatter3DChartWithTimestampFunction,
	    {"x", "y", "z", "timestamps", "title"}, dt + browser, "scatter_3d_chart" + et + ")", true);
	add("scatter_3d_chart", {numbers, numbers, numbers, labels, text, text}, Scatter3DChartWithTimestampAndPathFunction,
	    {"x", "y", "z", "timestamps", "title", "output_path"}, dt + file, "scatter_3d_chart" + et + ", 'tmp')", true);
	add("scatter_3d_chart_html", {numbers, numbers, numbers, labels, text}, Scatter3DChartHTMLWithTimestampFunction,
	    {"x", "y", "z", "timestamps", "title"}, dt + html, "scatter_3d_chart_html" + et + ")", false);

	struct DistributionChart {
		const char *name;
		const char *description;
		scalar_function_t file;
		scalar_function_t html;
	};
	for (const auto &chart :
	     {DistributionChart {
	          "histogram_chart",
	          "Plot a histogram from nonempty finite numeric samples; Plotly automatically chooses bins.",
	          DistributionChartFunction<false, true>, DistributionChartFunction<true, true>},
	      DistributionChart {"box_chart",
	                         "Plot a box-and-whisker chart from nonempty finite numeric samples; Plotly computes "
	                         "quartiles and outliers.",
	                         DistributionChartFunction<false, false>, DistributionChartFunction<true, false>}}) {
		const string name = chart.name;
		const string description = chart.description;
		const string example = "([1.0, 2.0, 2.0, 4.0], 'Distribution'";
		add(name, {numbers, text}, chart.file, {"samples", "title"}, description + browser, name + example + ")", true);
		add(name, {numbers, text, text}, chart.file, {"samples", "title", "output_path"}, description + file,
		    name + example + ", 'tmp')", true);
		add(name + "_html", {numbers, text}, chart.html, {"samples", "title"}, description + html,
		    name + "_html" + example + ")", false);
	}

	const string plotly = "Plot arbitrary Plotly traces from a JSON array of objects and a JSON layout object; "
	                      "an optional JSON config object defaults to {}.";
	const string pe = "('[{\"x\":[1,2],\"y\":[3,4],\"type\":\"scatter\"}]', '{\"title\":{\"text\":\"Example\"}}'";
	add("plotly_new", {text, text}, PlotlyNewFunction<false>, {"data", "layout"}, plotly + browser,
	    "plotly_new" + pe + ")", true);
	add("plotly_new", {text, text, text}, PlotlyNewFunction<false>, {"data", "layout", "config"}, plotly + browser,
	    "plotly_new" + pe + ", '{\"responsive\":true}')", true);
	add("plotly_new", {text, text, text, text}, PlotlyNewFunction<false>, {"data", "layout", "config", "output_path"},
	    plotly + file, "plotly_new" + pe + ", '{}', 'tmp')", true);
	add("plotly_new_html", {text, text}, PlotlyNewFunction<true>, {"data", "layout"}, plotly + html,
	    "plotly_new_html" + pe + ")", false);
	add("plotly_new_html", {text, text, text}, PlotlyNewFunction<true>, {"data", "layout", "config"}, plotly + html,
	    "plotly_new_html" + pe + ", '{\"responsive\":true}')", false);
	return functions;
}

void MiniplotExtension::Load(ExtensionLoader &loader) {
	for (auto &info : MiniplotFunctions()) {
		loader.RegisterFunction(std::move(info));
	}
}

std::string MiniplotExtension::Name() {
	return "miniplot";
}

std::string MiniplotExtension::Version() const {
#ifdef EXT_VERSION_MINIPLOT
	return EXT_VERSION_MINIPLOT;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {
DUCKDB_EXTENSION_API void miniplot_duckdb_cpp_init(duckdb::ExtensionLoader &loader) {
	duckdb::MiniplotExtension ext;
	ext.Load(loader);
}

DUCKDB_EXTENSION_API void miniplot_init(duckdb::DatabaseInstance &db) {
	duckdb::Connection con(db);
	con.BeginTransaction();
	auto &catalog = duckdb::Catalog::GetSystemCatalog(*con.context);
	for (auto &info : duckdb::MiniplotFunctions()) {
		catalog.CreateFunction(*con.context, info);
	}
	con.Commit();
}

DUCKDB_EXTENSION_API const char *miniplot_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}
