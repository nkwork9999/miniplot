#define DUCKDB_EXTENSION_MAIN

#include "miniplot_extension.hpp"
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

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace duckdb {

// Helper: Escape strings for JavaScript
static string EscapeString(const string &input) {
	string output;
	output.reserve(input.size() * 1.2);
	for (char c : input) {
		switch (c) {
		case '\'':
			output += "\\'";
			break;
		case '"':
			output += "\\\"";
			break;
		case '\\':
			output += "\\\\";
			break;
		case '\n':
			output += "\\n";
			break;
		case '\r':
			output += "\\r";
			break;
		case '\t':
			output += "\\t";
			break;
		case '<':
			output += "\\x3C";
			break;
		case '>':
			output += "\\x3E";
			break;
		default:
			output += c;
		}
	}
	return output;
}

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
			if (!children[i].IsNull()) {
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
			if (!children[i].IsNull()) {
				result.push_back(children[i].GetValue<double>());
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

// Generate HTML
static string GenerateHTML(const std::vector<string> &x_data, const std::vector<double> &y_data, const string &title,
                          const string &chart_type) {
	std::ostringstream x_json;
	x_json << "[";
	for (size_t i = 0; i < x_data.size(); i++) {
		if (i > 0)
			x_json << ", ";
		x_json << "'" << EscapeString(x_data[i]) << "'";
	}
	x_json << "]";

	std::ostringstream y_json;
	y_json << "[";
	for (size_t i = 0; i < y_data.size(); i++) {
		if (i > 0)
			y_json << ", ";
		y_json << y_data[i];
	}
	y_json << "]";

	string plotly_type = "bar";
	string mode = "";
	string fill = "";

	if (chart_type == "line") {
		plotly_type = "scatter";
		mode = "mode: 'lines+markers',";
	} else if (chart_type == "scatter") {
		plotly_type = "scatter";
		mode = "mode: 'markers',";
	} else if (chart_type == "area") {
		plotly_type = "scatter";
		mode = "mode: 'lines',";
		fill = "fill: 'tozeroy',";
	}

	std::ostringstream html;
	html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>)"
	     << EscapeString(title) << R"(</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif; background: #f5f5f5; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        h1 { margin: 0 0 20px; color: #333; font-size: 28px; text-align: center; }
        #chart { width: 100%; height: 600px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>)"
	     << EscapeString(title) << R"(</h1>
        <div id="chart"></div>
    </div>
    <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
    <script>
    var data = [{
        x: )"
	     << x_json.str() << R"(,
        y: )"
	     << y_json.str() << R"(,
        type: ')"
	     << plotly_type << R"(',
        )"
	     << mode << fill << R"(
        marker: { color: 'rgb(59, 130, 246)', size: 10 },
        line: { color: 'rgb(59, 130, 246)', width: 3 }
    }];
    var layout = {
        xaxis: { showgrid: true, gridcolor: '#e5e5e5' },
        yaxis: { title: 'Value', showgrid: true, gridcolor: '#e5e5e5' },
        plot_bgcolor: '#fff',
        paper_bgcolor: '#fff',
        margin: { t: 40, r: 40, b: 60, l: 70 },
        autosize: true
    };
    Plotly.newPlot('chart', data, layout, { responsive: true, displayModeBar: true });
    window.addEventListener('resize', function() { Plotly.Plots.resize('chart'); });
    </script>
</body>
</html>)";

	return html.str();
}

// Generate 3D Scatter HTML (without timestamp)
static string Generate3DScatterHTML(const std::vector<double> &x_data, const std::vector<double> &y_data, 
                                   const std::vector<double> &z_data, const string &title) {
	std::ostringstream x_json, y_json, z_json;
	
	x_json << "[";
	for (size_t i = 0; i < x_data.size(); i++) {
		if (i > 0) x_json << ", ";
		x_json << x_data[i];
	}
	x_json << "]";

	y_json << "[";
	for (size_t i = 0; i < y_data.size(); i++) {
		if (i > 0) y_json << ", ";
		y_json << y_data[i];
	}
	y_json << "]";

	z_json << "[";
	for (size_t i = 0; i < z_data.size(); i++) {
		if (i > 0) z_json << ", ";
		z_json << z_data[i];
	}
	z_json << "]";

	std::ostringstream html;
	html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>)" << EscapeString(title) << R"(</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif; background: #f5f5f5; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        h1 { margin: 0 0 20px; color: #333; font-size: 28px; text-align: center; }
        #chart { width: 100%; height: 700px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>)" << EscapeString(title) << R"(</h1>
        <div id="chart"></div>
    </div>
    <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
    <script>
    var data = [{
        x: )" << x_json.str() << R"(,
        y: )" << y_json.str() << R"(,
        z: )" << z_json.str() << R"(,
        mode: 'markers',
        type: 'scatter3d',
        marker: {
            size: 8,
            color: )" << z_json.str() << R"(,
            colorscale: 'Viridis',
            showscale: true,
            colorbar: {
                title: 'Z Value',
                thickness: 20,
                len: 0.7
            }
        }
    }];
    var layout = {
        scene: {
            xaxis: { title: 'X Axis', showgrid: true, gridcolor: '#e5e5e5' },
            yaxis: { title: 'Y Axis', showgrid: true, gridcolor: '#e5e5e5' },
            zaxis: { title: 'Z Axis', showgrid: true, gridcolor: '#e5e5e5' },
            bgcolor: '#fff'
        },
        paper_bgcolor: '#fff',
        margin: { t: 40, r: 40, b: 40, l: 40 },
        autosize: true
    };
    Plotly.newPlot('chart', data, layout, { responsive: true, displayModeBar: true });
    window.addEventListener('resize', function() { Plotly.Plots.resize('chart'); });
    </script>
</body>
</html>)";

	return html.str();
}

// Generate 3D Scatter HTML with timestamps
static string Generate3DScatterHTMLWithTimestamp(const std::vector<double> &x_data, 
                                                  const std::vector<double> &y_data, 
                                                  const std::vector<double> &z_data,
                                                  const std::vector<string> &timestamps,
                                                  const string &title) {
	std::ostringstream x_json, y_json, z_json, timestamp_json;
	
	x_json << "[";
	for (size_t i = 0; i < x_data.size(); i++) {
		if (i > 0) x_json << ", ";
		x_json << x_data[i];
	}
	x_json << "]";

	y_json << "[";
	for (size_t i = 0; i < y_data.size(); i++) {
		if (i > 0) y_json << ", ";
		y_json << y_data[i];
	}
	y_json << "]";

	z_json << "[";
	for (size_t i = 0; i < z_data.size(); i++) {
		if (i > 0) z_json << ", ";
		z_json << z_data[i];
	}
	z_json << "]";

	timestamp_json << "[";
	for (size_t i = 0; i < timestamps.size(); i++) {
		if (i > 0) timestamp_json << ", ";
		timestamp_json << "'" << EscapeString(timestamps[i]) << "'";
	}
	timestamp_json << "]";

	std::ostringstream html;
	html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>)" << EscapeString(title) << R"(</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif; background: #f5f5f5; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        h1 { margin: 0 0 20px; color: #333; font-size: 28px; text-align: center; }
        #chart { width: 100%; height: 700px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>)" << EscapeString(title) << R"(</h1>
        <div id="chart"></div>
    </div>
    <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
    <script>
    var data = [{
        x: )" << x_json.str() << R"(,
        y: )" << y_json.str() << R"(,
        z: )" << z_json.str() << R"(,
        text: )" << timestamp_json.str() << R"(,
        mode: 'markers',
        type: 'scatter3d',
        marker: {
            size: 8,
            color: )" << z_json.str() << R"(,
            colorscale: 'Viridis',
            showscale: true,
            colorbar: {
                title: 'Z Value',
                thickness: 20,
                len: 0.7
            }
        },
        hovertemplate: '<b>Timestamp:</b> %{text}<br>' +
                       '<b>X:</b> %{x:.2f}<br>' +
                       '<b>Y:</b> %{y:.2f}<br>' +
                       '<b>Z:</b> %{z:.2f}<br>' +
                       '<extra></extra>'
    }];
    var layout = {
        scene: {
            xaxis: { title: 'X Axis', showgrid: true, gridcolor: '#e5e5e5' },
            yaxis: { title: 'Y Axis', showgrid: true, gridcolor: '#e5e5e5' },
            zaxis: { title: 'Z Axis', showgrid: true, gridcolor: '#e5e5e5' },
            bgcolor: '#fff'
        },
        paper_bgcolor: '#fff',
        margin: { t: 40, r: 40, b: 40, l: 40 },
        autosize: true
    };
    Plotly.newPlot('chart', data, layout, { responsive: true, displayModeBar: true });
    window.addEventListener('resize', function() { Plotly.Plots.resize('chart'); });
    </script>
</body>
</html>)";

	return html.str();
}

static void OpenInBrowser(const string &html_path) {
#ifdef _WIN32
	system(("cmd /C start \"\" \"" + html_path + "\"").c_str());
#elif __APPLE__
	system(("open \"" + html_path + "\"").c_str());
#else
	system(("xdg-open \"" + html_path + "\" || sensible-browser \"" + html_path + "\" || "
	        "x-www-browser \"" + html_path + "\" &").c_str());
#endif
}

// Helper: Convert string to lowercase
static string ToLowerCase(const string &str) {
	string lower = str;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	return lower;
}

// Generic chart creation with path control
static void CreateChartWithPath(const std::vector<string> &x_data, const std::vector<double> &y_data, 
                               const string &title, const string &chart_type, const string &output_path_arg,
                               Vector &result) {
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

	if (open_browser) {
		OpenInBrowser(html_path);
	}

	result.SetValue(0, Value(html_path));
}

// Generic 3D chart creation with path control
static void Create3DChartWithPath(const std::vector<double> &x_data, const std::vector<double> &y_data,
                                 const std::vector<double> &z_data, const string &title,
                                 const string &output_path_arg, Vector &result) {
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

	if (open_browser) {
		OpenInBrowser(html_path);
	}

	result.SetValue(0, Value(html_path));
}

// Generic 3D chart with timestamp creation with path control
static void Create3DChartWithTimestampAndPath(const std::vector<double> &x_data, 
                                              const std::vector<double> &y_data,
                                              const std::vector<double> &z_data,
                                              const std::vector<string> &timestamps,
                                              const string &title,
                                              const string &output_path_arg,
                                              Vector &result) {
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

	if (open_browser) {
		OpenInBrowser(html_path);
	}

	result.SetValue(0, Value(html_path));
}

// ============================================
// 2D Chart Functions (3 args - browser mode)
// ============================================

inline void BarChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_strings = ExtractStringList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();
	CreateChartWithPath(x_strings, y_values, title_str, "bar", "", result);
}

inline void LineChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_strings = ExtractStringList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();
	CreateChartWithPath(x_strings, y_values, title_str, "line", "", result);
}

inline void ScatterChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_values = ExtractDoubleList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();

	std::vector<string> x_strings;
	for (double v : x_values) {
		x_strings.push_back(std::to_string(v));
	}
	CreateChartWithPath(x_strings, y_values, title_str, "scatter", "", result);
}

inline void AreaChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_strings = ExtractStringList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();
	CreateChartWithPath(x_strings, y_values, title_str, "area", "", result);
}

// ============================================
// 2D Chart Functions (4 args - with output path)
// ============================================

inline void BarChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_strings = ExtractStringList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();
	string output_path = args.data[3].GetValue(0).ToString();
	CreateChartWithPath(x_strings, y_values, title_str, "bar", output_path, result);
}

inline void LineChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_strings = ExtractStringList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();
	string output_path = args.data[3].GetValue(0).ToString();
	CreateChartWithPath(x_strings, y_values, title_str, "line", output_path, result);
}

inline void ScatterChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_values = ExtractDoubleList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();
	string output_path = args.data[3].GetValue(0).ToString();

	std::vector<string> x_strings;
	for (double v : x_values) {
		x_strings.push_back(std::to_string(v));
	}
	CreateChartWithPath(x_strings, y_values, title_str, "scatter", output_path, result);
}

inline void AreaChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_strings = ExtractStringList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	string title_str = args.data[2].GetValue(0).ToString();
	string output_path = args.data[3].GetValue(0).ToString();
	CreateChartWithPath(x_strings, y_values, title_str, "area", output_path, result);
}

// ============================================
// 3D Chart Functions (4 args - browser mode, no timestamp)
// ============================================

inline void Scatter3DChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_values = ExtractDoubleList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	auto z_values = ExtractDoubleList(args.data[2], 0);
	string title_str = args.data[3].GetValue(0).ToString();
	Create3DChartWithPath(x_values, y_values, z_values, title_str, "", result);
}

// ============================================
// 3D Chart Functions (5 args - with output path, no timestamp)
// ============================================

inline void Scatter3DChartWithPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_values = ExtractDoubleList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	auto z_values = ExtractDoubleList(args.data[2], 0);
	string title_str = args.data[3].GetValue(0).ToString();
	string output_path = args.data[4].GetValue(0).ToString();
	Create3DChartWithPath(x_values, y_values, z_values, title_str, output_path, result);
}

// ============================================
// 3D Chart Functions (5 args - browser mode, with timestamp)
// ============================================

inline void Scatter3DChartWithTimestampFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_values = ExtractDoubleList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	auto z_values = ExtractDoubleList(args.data[2], 0);
	auto timestamps = ExtractStringList(args.data[3], 0);
	string title_str = args.data[4].GetValue(0).ToString();
	Create3DChartWithTimestampAndPath(x_values, y_values, z_values, timestamps, title_str, "", result);
}

// ============================================
// 3D Chart Functions (6 args - with output path and timestamp)
// ============================================

inline void Scatter3DChartWithTimestampAndPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto x_values = ExtractDoubleList(args.data[0], 0);
	auto y_values = ExtractDoubleList(args.data[1], 0);
	auto z_values = ExtractDoubleList(args.data[2], 0);
	auto timestamps = ExtractStringList(args.data[3], 0);
	string title_str = args.data[4].GetValue(0).ToString();
	string output_path = args.data[5].GetValue(0).ToString();
	Create3DChartWithTimestampAndPath(x_values, y_values, z_values, timestamps, title_str, output_path, result);
}


// ============================================
// Version functions
// ============================================

inline void MiniplotScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name, result, args.size(), [&](string_t name) {
		std::string version_string = "Miniplot " + name.GetString();
		return StringVector::AddString(result, version_string);
	});
}

inline void MiniplotOpenSSLVersionFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name, result, args.size(), [&](string_t name) {
		std::string version_string = "Miniplot " + name.GetString() + 
		                            ", my linked OpenSSL version is " + 
		                            OPENSSL_VERSION_TEXT;
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

	loader.RegisterFunction(
	    ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, LogicalType::VARCHAR, 
	                  MiniplotOpenSSLVersionFunction));

	// ============================================
	// 2D Charts with Overloading
	// ============================================
	
	// Bar Chart
	loader.RegisterFunction(ScalarFunction(
	    "bar_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, BarChartFunction));
	
	loader.RegisterFunction(ScalarFunction(
	    "bar_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, BarChartWithPathFunction));

	// Line Chart
	loader.RegisterFunction(ScalarFunction(
	    "line_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, LineChartFunction));
	
	loader.RegisterFunction(ScalarFunction(
	    "line_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, LineChartWithPathFunction));

	// Scatter Chart
	loader.RegisterFunction(ScalarFunction(
	    "scatter_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, ScatterChartFunction));
	
	loader.RegisterFunction(ScalarFunction(
	    "scatter_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, ScatterChartWithPathFunction));

	// Area Chart
	loader.RegisterFunction(ScalarFunction(
	    "area_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, AreaChartFunction));
	
	loader.RegisterFunction(ScalarFunction(
	    "area_chart",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, AreaChartWithPathFunction));

	// ============================================
	// 3D Charts with Overloading
	// ============================================
	
	// 3D Scatter (no timestamp)
	loader.RegisterFunction(ScalarFunction(
	    "scatter_3d_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
	    LogicalType::VARCHAR, Scatter3DChartFunction));
	
	loader.RegisterFunction(ScalarFunction(
	    "scatter_3d_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, Scatter3DChartWithPathFunction));

	// 3D Scatter (with timestamp)
	loader.RegisterFunction(ScalarFunction(
	    "scatter_3d_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::VARCHAR),
	     LogicalType::VARCHAR},
	    LogicalType::VARCHAR, Scatter3DChartWithTimestampFunction));
	
	loader.RegisterFunction(ScalarFunction(
	    "scatter_3d_chart",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), 
	     LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::VARCHAR),
	     LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, Scatter3DChartWithTimestampAndPathFunction));
		}
	// ============================================

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

	duckdb::CreateScalarFunctionInfo miniplot_openssl_func(duckdb::ScalarFunction(
	    "miniplot_openssl_version", {duckdb::LogicalType::VARCHAR}, duckdb::LogicalType::VARCHAR, 
	    duckdb::MiniplotOpenSSLVersionFunction));
	catalog.CreateFunction(*con.context, miniplot_openssl_func);

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
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
	                            duckdb::LogicalType::VARCHAR, duckdb::LogicalType::VARCHAR},
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
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
	                            duckdb::LogicalType::VARCHAR, duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::LineChartWithPathFunction));
	catalog.CreateFunction(*con.context, line_chart_func_4);

	duckdb::CreateScalarFunctionInfo scatter_chart_func_3(
	    duckdb::ScalarFunction("scatter_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::ScatterChartFunction));
	catalog.CreateFunction(*con.context, scatter_chart_func_3);

	duckdb::CreateScalarFunctionInfo scatter_chart_func_4(
	    duckdb::ScalarFunction("scatter_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
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
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
	                            duckdb::LogicalType::VARCHAR, duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::AreaChartWithPathFunction));
	catalog.CreateFunction(*con.context, area_chart_func_4);

	// 3D Scatter Chart - 4 args (browser, no timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_func_4(
	    duckdb::ScalarFunction("scatter_3d_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_func_4);

	// 3D Scatter Chart - 5 args (with path, no timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_func_5_path(
	    duckdb::ScalarFunction("scatter_3d_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::VARCHAR, duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartWithPathFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_func_5_path);

	// 3D Scatter Chart - 5 args (browser, with timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_timestamp_func_5(
	    duckdb::ScalarFunction("scatter_3d_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartWithTimestampFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_timestamp_func_5);

	// 3D Scatter Chart - 6 args (with path and timestamp)
	duckdb::CreateScalarFunctionInfo scatter_3d_chart_timestamp_func_6(
	    duckdb::ScalarFunction("scatter_3d_chart",
	                           {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE),
	                            duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                            duckdb::LogicalType::VARCHAR, duckdb::LogicalType::VARCHAR},
	                           duckdb::LogicalType::VARCHAR, duckdb::Scatter3DChartWithTimestampAndPathFunction));
	catalog.CreateFunction(*con.context, scatter_3d_chart_timestamp_func_6);

	con.Commit();
}

DUCKDB_EXTENSION_API const char *miniplot_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}