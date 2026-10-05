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
	string html_path;
	bool open_browser = false;

	// Check if output_path_arg is "tmp" (case-insensitive)
	if (ToLowerCase(output_path_arg) == "tmp") {
		html_path = GenerateSafeFilename();
		open_browser = false;
	} else if (output_path_arg.empty()) {
		// Empty string means browser mode
		html_path = GenerateSafeFilename();
		open_browser = true;
	} else {
		// Custom path specified
		html_path = output_path_arg;
		open_browser = false;
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
	string html_path;
	bool open_browser = false;

	if (ToLowerCase(output_path_arg) == "tmp") {
		html_path = GenerateSafeFilename();
		open_browser = false;
	} else if (output_path_arg.empty()) {
		html_path = GenerateSafeFilename();
		open_browser = true;
	} else {
		html_path = output_path_arg;
		open_browser = false;
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
	string html_path;
	bool open_browser = false;

	if (ToLowerCase(output_path_arg) == "tmp") {
		html_path = GenerateSafeFilename();
		open_browser = false;
	} else if (output_path_arg.empty()) {
		html_path = GenerateSafeFilename();
		open_browser = true;
	} else {
		html_path = output_path_arg;
		open_browser = false;
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
		string title_str = args.data[2].GetValue(row).ToString();

		std::vector<string> x_strings;
		for (double v : x_values) {
			x_strings.push_back(NumberLabel(v));
		}
		CreateChartWithPath(x_strings, y_values, title_str, "scatter", "", result, row);
	}
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
		string title_str = args.data[2].GetValue(row).ToString();
		string output_path = args.data[3].GetValue(row).ToString();

		std::vector<string> x_strings;
		for (double v : x_values) {
			x_strings.push_back(NumberLabel(v));
		}
		CreateChartWithPath(x_strings, y_values, title_str, "scatter", output_path, result, row);
	}
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
		string title_str = args.data[2].GetValue(row).ToString();

		if (x_values.size() != y_values.size()) {
			throw InvalidInputException("x and y arrays must have the same length");
		}
		if (x_values.empty()) {
			throw InvalidInputException("Data arrays cannot be empty");
		}

		std::vector<string> x_strings;
		for (double v : x_values) {
			x_strings.push_back(NumberLabel(v));
		}

		string html_content = GenerateHTML(x_strings, y_values, title_str, "scatter");
		result.SetValue(row, Value(html_content));
	}
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

void MiniplotExtension::Load(ExtensionLoader &loader) {
	// Version functions
	loader.RegisterFunction(
	    ScalarFunction("miniplot", {LogicalType::VARCHAR}, LogicalType::VARCHAR, MiniplotScalarFun));

	loader.RegisterFunction(ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                       MiniplotOpenSSLVersionFunction));

	// ============================================
	// 2D Charts with Overloading (既存機能)
	// ============================================

	// Bar Chart
	loader.RegisterFunction(ScalarFunction(
	    "bar_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, BarChartFunction));

	loader.RegisterFunction(
	    ScalarFunction("bar_chart",
	                   {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, BarChartWithPathFunction));

	// Line Chart
	loader.RegisterFunction(ScalarFunction(
	    "line_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, LineChartFunction));

	loader.RegisterFunction(
	    ScalarFunction("line_chart",
	                   {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, LineChartWithPathFunction));

	// Scatter Chart
	loader.RegisterFunction(ScalarFunction(
	    "scatter_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, ScatterChartFunction));

	loader.RegisterFunction(
	    ScalarFunction("scatter_chart",
	                   {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, ScatterChartWithPathFunction));

	// Area Chart
	loader.RegisterFunction(ScalarFunction(
	    "area_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, AreaChartFunction));

	loader.RegisterFunction(
	    ScalarFunction("area_chart",
	                   {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, AreaChartWithPathFunction));

	// ============================================
	// 3D Charts with Overloading (既存機能)
	// ============================================

	// 3D Scatter (no timestamp)
	loader.RegisterFunction(
	    ScalarFunction("scatter_3d_chart",
	                   {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, Scatter3DChartFunction));

	loader.RegisterFunction(
	    ScalarFunction("scatter_3d_chart",
	                   {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, Scatter3DChartWithPathFunction));

	// 3D Scatter (with timestamp)
	loader.RegisterFunction(ScalarFunction(
	    "scatter_3d_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	     LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::VARCHAR), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, Scatter3DChartWithTimestampFunction));

	loader.RegisterFunction(
	    ScalarFunction("scatter_3d_chart",
	                   {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::VARCHAR),
	                    LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, Scatter3DChartWithTimestampAndPathFunction));

	// ============================================
	// HTML返却関数 (WASM対応の新機能)
	// ============================================

	// 2D Charts - HTML版
	loader.RegisterFunction(ScalarFunction(
	    "bar_chart_html",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, BarChartHTMLFunction));

	loader.RegisterFunction(ScalarFunction(
	    "line_chart_html",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, LineChartHTMLFunction));

	loader.RegisterFunction(ScalarFunction(
	    "scatter_chart_html",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, ScatterChartHTMLFunction));

	loader.RegisterFunction(ScalarFunction(
	    "area_chart_html",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, AreaChartHTMLFunction));

	// 3D Charts - HTML版
	loader.RegisterFunction(
	    ScalarFunction("scatter_3d_chart_html",
	                   {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                    LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, Scatter3DChartHTMLFunction));

	loader.RegisterFunction(ScalarFunction(
	    "scatter_3d_chart_html",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	     LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::VARCHAR), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, Scatter3DChartHTMLWithTimestampFunction));
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

	// Version functions
	duckdb::CreateScalarFunctionInfo miniplot_func(duckdb::ScalarFunction(
	    "miniplot", {duckdb::LogicalType::VARCHAR}, duckdb::LogicalType::VARCHAR, duckdb::MiniplotScalarFun));
	catalog.CreateFunction(*con.context, miniplot_func);

	duckdb::CreateScalarFunctionInfo miniplot_openssl_func(
	    duckdb::ScalarFunction("miniplot_openssl_version", {duckdb::LogicalType::VARCHAR}, duckdb::LogicalType::VARCHAR,
	                           duckdb::MiniplotOpenSSLVersionFunction));
	catalog.CreateFunction(*con.context, miniplot_openssl_func);

	// ============================================
	// 既存の2D Charts関数
	// ============================================

	// 2D Charts - 3 args (browser mode)
	duckdb::CreateScalarFunctionInfo bar_chart_func_3(
	    duckdb::ScalarFunction("bar_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::BarChartFunction));
	catalog.CreateFunction(*con.context, bar_chart_func_3);

	// 2D Charts - 4 args (with path)
	duckdb::CreateScalarFunctionInfo bar_chart_func_4(
	    duckdb::ScalarFunction("bar_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR,
	                            duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::BarChartWithPathFunction));
	catalog.CreateFunction(*con.context, bar_chart_func_4);

	duckdb::CreateScalarFunctionInfo line_chart_func_3(
	    duckdb::ScalarFunction("line_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::LineChartFunction));
	catalog.CreateFunction(*con.context, line_chart_func_3);

	duckdb::CreateScalarFunctionInfo line_chart_func_4(
	    duckdb::ScalarFunction("line_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR,
	                            duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::LineChartWithPathFunction));
	catalog.CreateFunction(*con.context, line_chart_func_4);

	duckdb::CreateScalarFunctionInfo scatter_chart_func_3(
	    duckdb::ScalarFunction("scatter_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::ScatterChartFunction));
	catalog.CreateFunction(*con.context, scatter_chart_func_3);

	duckdb::CreateScalarFunctionInfo scatter_chart_func_4(duckdb::ScalarFunction(
	    "scatter_chart",
	    {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::VARCHAR, duckdb::LogicalType::VARCHAR},
	    duckdb::LogicalType::VARCHAR, duckdb::ScatterChartWithPathFunction));
	catalog.CreateFunction(*con.context, scatter_chart_func_4);

	duckdb::CreateScalarFunctionInfo area_chart_func_3(
	    duckdb::ScalarFunction("area_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::AreaChartFunction));
	catalog.CreateFunction(*con.context, area_chart_func_3);

	duckdb::CreateScalarFunctionInfo area_chart_func_4(
	    duckdb::ScalarFunction("area_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR,
	                            duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::AreaChartWithPathFunction));
	catalog.CreateFunction(*con.context, area_chart_func_4);

	// ============================================
	// 既存の3D Charts関数
	// ============================================

	// 3D Scatter Chart - 4 args (browser, no timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_func_4(duckdb::ScalarFunction(
	    "scatter_3d_chart",
	    {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	    duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_func_4);

	// 3D Scatter Chart - 5 args (with path, no timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_func_5_path(duckdb::ScalarFunction(
	    "scatter_3d_chart",
	    {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR,
	     duckdb::LogicalType::VARCHAR},
	    duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartWithPathFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_func_5_path);

	// 3D Scatter Chart - 5 args (browser, with timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_timestamp_func_5(duckdb::ScalarFunction(
	    "scatter_3d_chart",
	    {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR), duckdb::LogicalType::VARCHAR},
	    duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartWithTimestampFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_timestamp_func_5);

	// 3D Scatter Chart - 6 args (with path and timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_timestamp_func_6(duckdb::ScalarFunction(
	    "scatter_3d_chart",
	    {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR), duckdb::LogicalType::VARCHAR,
	     duckdb::LogicalType::VARCHAR},
	    duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartWithTimestampAndPathFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_timestamp_func_6);

	// ============================================
	// 新規: HTML返却関数 (WASM対応)
	// ============================================

	// 2D Charts - HTML版
	duckdb::CreateScalarFunctionInfo bar_chart_html_func(
	    duckdb::ScalarFunction("bar_chart_html",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::BarChartHTMLFunction));
	catalog.CreateFunction(*con.context, bar_chart_html_func);

	duckdb::CreateScalarFunctionInfo line_chart_html_func(
	    duckdb::ScalarFunction("line_chart_html",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::LineChartHTMLFunction));
	catalog.CreateFunction(*con.context, line_chart_html_func);

	duckdb::CreateScalarFunctionInfo scatter_chart_html_func(
	    duckdb::ScalarFunction("scatter_chart_html",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::ScatterChartHTMLFunction));
	catalog.CreateFunction(*con.context, scatter_chart_html_func);

	duckdb::CreateScalarFunctionInfo area_chart_html_func(
	    duckdb::ScalarFunction("area_chart_html",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::AreaChartHTMLFunction));
	catalog.CreateFunction(*con.context, area_chart_html_func);

	// 3D Charts - HTML版
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_html_func(duckdb::ScalarFunction(
	    "scatter_3d_chart_html",
	    {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	    duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartHTMLFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_html_func);

	duckdb::CreateScalarFunctionInfo scatter_3d_chart_html_timestamp_func(duckdb::ScalarFunction(
	    "scatter_3d_chart_html",
	    {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	     duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR), duckdb::LogicalType::VARCHAR},
	    duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartHTMLWithTimestampFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_html_timestamp_func);

	con.Commit();
}

DUCKDB_EXTENSION_API const char *miniplot_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}