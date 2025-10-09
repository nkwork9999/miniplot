#define DUCKDB_EXTENSION_MAIN

#include "miniplot_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_entries.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/catalog/catalog.hpp"

#include <fstream>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <openssl/opensslv.h>

namespace duckdb {

// Test function
inline void MiniplotTestFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &name_vector = args.data[0];
    UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
        return StringVector::AddString(result, "Miniplot " + name.GetString() + " 🐥"); 
    });
}

// OpenSSL version function
inline void MiniplotOpenSSLVersionFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &name_vector = args.data[0];
    UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
        string version_info = "Miniplot " + name.GetString() + ", my linked OpenSSL version is " + 
                              string(OPENSSL_VERSION_TEXT);
        return StringVector::AddString(result, version_info);
    });
}

// Helper: Escape strings for JavaScript
static string EscapeString(const string &input) {
    string output;
    output.reserve(input.size() * 1.2);
    for (char c : input) {
        switch (c) {
            case '\'': output += "\\'"; break;
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            case '<': output += "\\x3C"; break;
            case '>': output += "\\x3E"; break;
            default: output += c;
        }
    }
    return output;
}

// Helper: Extract string list from DuckDB List
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

// Helper: Extract double list from DuckDB List
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

// Generate HTML with Plotly.js CDN
static string GenerateHTML(
    const std::vector<string> &x_data,
    const std::vector<double> &y_data,
    const string &title,
    const string &chart_type
) {
    // Convert X data to JSON array
    std::ostringstream x_json;
    x_json << "[";
    for (size_t i = 0; i < x_data.size(); i++) {
        if (i > 0) x_json << ", ";
        x_json << "'" << EscapeString(x_data[i]) << "'";
    }
    x_json << "]";
    
    // Convert Y data to JSON array
    std::ostringstream y_json;
    y_json << "[";
    for (size_t i = 0; i < y_data.size(); i++) {
        if (i > 0) y_json << ", ";
        y_json << y_data[i];
    }
    y_json << "]";
    
    // Map chart type to Plotly configuration
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
    
    // Generate HTML
    std::ostringstream html;
    html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" << EscapeString(title) << R"( - DuckDB Chart</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Helvetica Neue", Arial, sans-serif;
            background: #f5f5f5;
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            background: #ffffff;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 1px 3px rgba(0, 0, 0, 0.1);
        }
        h1 {
            margin: 0 0 20px 0;
            color: #333333;
            font-size: 28px;
            font-weight: 700;
            text-align: center;
        }
        #chart {
            width: 100%;
            height: 600px;
        }
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
        type: ')" << plotly_type << R"(',
        )" << mode << R"(
        )" << fill << R"(
        marker: {
            color: 'rgb(59, 130, 246)',
            size: 10
        },
        line: {
            color: 'rgb(59, 130, 246)',
            width: 3
        }
    }];
    
    var layout = {
        xaxis: {
            showgrid: true,
            gridcolor: '#e5e5e5',
            zeroline: false
        },
        yaxis: {
            title: 'Value',
            showgrid: true,
            gridcolor: '#e5e5e5',
            zeroline: false
        },
        plot_bgcolor: '#ffffff',
        paper_bgcolor: '#ffffff',
        font: {
            family: '-apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif',
            size: 13,
            color: '#333333'
        },
        margin: { t: 40, r: 40, b: 60, l: 70 },
        autosize: true,
        hovermode: 'closest'
    };
    
    var config = {
        responsive: true,
        displayModeBar: true,
        displaylogo: false,
        modeBarButtonsToRemove: ['lasso2d', 'select2d'],
        toImageButtonOptions: {
            format: 'png',
            filename: 'duckdb_chart',
            height: 800,
            width: 1200,
            scale: 2
        }
    };
    
    Plotly.newPlot('chart', data, layout, config);
    
    window.addEventListener('resize', function() {
        Plotly.Plots.resize('chart');
    });
    </script>
</body>
</html>)";
    
    return html.str();
}

// Open HTML in default browser
static void OpenInBrowser(const string &html_path) {
#ifdef _WIN32
    string command = "cmd /C start \"\" \"" + html_path + "\"";
    system(command.c_str());
#elif __APPLE__
    string command = "open \"" + html_path + "\"";
    system(command.c_str());
#else
    string command = "xdg-open \"" + html_path + "\" 2>/dev/null &";
    system(command.c_str());
#endif
}

// Main chart creation function
static void CreateChart(
    const std::vector<string> &x_data,
    const std::vector<double> &y_data,
    const string &title,
    const string &chart_type,
    Vector &result
) {
    // Validate input
    if (x_data.empty() && y_data.empty()) {
        throw InvalidInputException("Chart data cannot be empty");
    }
    
    if (!x_data.empty() && !y_data.empty() && x_data.size() != y_data.size()) {
        throw InvalidInputException(
            "X and Y data must have the same length (X: %llu, Y: %llu)",
            (unsigned long long)x_data.size(),
            (unsigned long long)y_data.size()
        );
    }
    
    try {
        // Generate HTML
        string html = GenerateHTML(x_data, y_data, title, chart_type);
        
        // Create temp file path
        std::ostringstream temp_path_stream;
#ifdef _WIN32
        const char* temp_dir = getenv("TEMP");
        if (!temp_dir) temp_dir = getenv("TMP");
        if (!temp_dir) temp_dir = "C:\\Windows\\Temp";
        temp_path_stream << temp_dir << "\\duckdb_chart_" 
                        << time(nullptr) << "_" << rand() << ".html";
#else
        temp_path_stream << "/tmp/duckdb_chart_" 
                        << time(nullptr) << "_" << rand() << ".html";
#endif
        
        string temp_path = temp_path_stream.str();
        
        // Write HTML to file
        std::ofstream file(temp_path, std::ios::binary);
        if (!file) {
            throw IOException("Failed to create temporary HTML file: %s", temp_path.c_str());
        }
        file << html;
        file.close();
        
        // Open in browser
        OpenInBrowser(temp_path);
        
        result.SetValue(0, Value("Chart opened in browser: " + temp_path));
    } catch (const Exception &e) {
        throw;
    } catch (const std::exception &e) {
        throw InternalException("Failed to create chart: %s", e.what());
    }
}

// Chart functions
inline void BarChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto x_strings = ExtractStringList(args.data[0], 0);
    auto y_values = ExtractDoubleList(args.data[1], 0);
    string title_str = args.data[2].GetValue(0).ToString();
    CreateChart(x_strings, y_values, title_str, "bar", result);
}

inline void LineChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto x_strings = ExtractStringList(args.data[0], 0);
    auto y_values = ExtractDoubleList(args.data[1], 0);
    string title_str = args.data[2].GetValue(0).ToString();
    CreateChart(x_strings, y_values, title_str, "line", result);
}

inline void ScatterChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto x_values = ExtractDoubleList(args.data[0], 0);
    auto y_values = ExtractDoubleList(args.data[1], 0);
    string title_str = args.data[2].GetValue(0).ToString();
    
    std::vector<string> x_strings;
    for (double v : x_values) {
        x_strings.push_back(std::to_string(v));
    }
    
    CreateChart(x_strings, y_values, title_str, "scatter", result);
}

inline void AreaChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto x_strings = ExtractStringList(args.data[0], 0);
    auto y_values = ExtractDoubleList(args.data[1], 0);
    string title_str = args.data[2].GetValue(0).ToString();
    CreateChart(x_strings, y_values, title_str, "area", result);
}

// Registration functions
static void LoadInternal(ExtensionLoader &loader) {
    loader.RegisterFunction(ScalarFunction("miniplot", {LogicalType::VARCHAR}, 
                                          LogicalType::VARCHAR, MiniplotTestFunction));
    
    loader.RegisterFunction(ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, 
                                          LogicalType::VARCHAR, MiniplotOpenSSLVersionFunction));
    
    loader.RegisterFunction(ScalarFunction("bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, BarChartFunction));
    
    loader.RegisterFunction(ScalarFunction("line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, LineChartFunction));
    
    loader.RegisterFunction(ScalarFunction("scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, ScatterChartFunction));
    
    loader.RegisterFunction(ScalarFunction("area_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, AreaChartFunction));
}

static void LoadInternal(DatabaseInstance &instance) {
    Connection con(instance);
    auto &context = *con.context;
    auto &catalog = Catalog::GetSystemCatalog(instance);
    
    CreateScalarFunctionInfo test_info(ScalarFunction("miniplot", {LogicalType::VARCHAR}, 
                                                      LogicalType::VARCHAR, MiniplotTestFunction));
    catalog.CreateFunction(context, test_info);
    
    CreateScalarFunctionInfo openssl_info(ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, 
                                                         LogicalType::VARCHAR, MiniplotOpenSSLVersionFunction));
    catalog.CreateFunction(context, openssl_info);
    
    CreateScalarFunctionInfo bar_info(ScalarFunction("bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, BarChartFunction));
    catalog.CreateFunction(context, bar_info);
    
    CreateScalarFunctionInfo line_info(ScalarFunction("line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, LineChartFunction));
    catalog.CreateFunction(context, line_info);
    
    CreateScalarFunctionInfo scatter_info(ScalarFunction("scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, ScatterChartFunction));
    catalog.CreateFunction(context, scatter_info);
    
    CreateScalarFunctionInfo area_info(ScalarFunction("area_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, AreaChartFunction));
    catalog.CreateFunction(context, area_info);
}

void MiniplotExtension::Load(ExtensionLoader &loader) {
    LoadInternal(loader);
}

std::string MiniplotExtension::Name() {
    return "miniplot";
}

std::string MiniplotExtension::Version() const {
#ifdef EXT_VERSION_MINIPLOT
    return EXT_VERSION_MINIPLOT;
#else
    return "0.0.2";
#endif
}

} // namespace duckdb

extern "C" {
DUCKDB_EXTENSION_API void miniplot_duckdb_cpp_init(duckdb::ExtensionLoader &loader) {
    duckdb::LoadInternal(loader);
}

DUCKDB_EXTENSION_API void miniplot_init(duckdb::DatabaseInstance &db) {
    duckdb::LoadInternal(db);
}

DUCKDB_EXTENSION_API const char *miniplot_version() {
    return duckdb::DuckDB::LibraryVersion();
}
}