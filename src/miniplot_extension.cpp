#define DUCKDB_EXTENSION_MAIN

#include "miniplot_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

#include <fstream>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <ctime>

namespace duckdb {

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

// Generate HTML
static string GenerateHTML(
    const std::vector<string> &x_data,
    const std::vector<double> &y_data,
    const string &title,
    const string &chart_type
) {
    std::ostringstream x_json;
    x_json << "[";
    for (size_t i = 0; i < x_data.size(); i++) {
        if (i > 0) x_json << ", ";
        x_json << "'" << EscapeString(x_data[i]) << "'";
    }
    x_json << "]";
    
    std::ostringstream y_json;
    y_json << "[";
    for (size_t i = 0; i < y_data.size(); i++) {
        if (i > 0) y_json << ", ";
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
    <title>)" << EscapeString(title) << R"(</title>
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
        <h1>)" << EscapeString(title) << R"(</h1>
        <div id="chart"></div>
    </div>
    <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
    <script>
    var data = [{
        x: )" << x_json.str() << R"(,
        y: )" << y_json.str() << R"(,
        type: ')" << plotly_type << R"(',
        )" << mode << fill << R"(
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

static void OpenInBrowser(const string &html_path) {
#ifdef _WIN32
    system(("cmd /C start \"\" \"" + html_path + "\"").c_str());
#elif __APPLE__
    system(("open \"" + html_path + "\"").c_str());
#else
    system(("xdg-open \"" + html_path + "\" 2>/dev/null &").c_str());
#endif
}

static void CreateChart(
    const std::vector<string> &x_data,
    const std::vector<double> &y_data,
    const string &title,
    const string &chart_type,
    Vector &result
) {
    if (x_data.empty() && y_data.empty()) {
        throw InvalidInputException("Chart data cannot be empty");
    }
    
    if (!x_data.empty() && !y_data.empty() && x_data.size() != y_data.size()) {
        throw InvalidInputException("X and Y data length mismatch");
    }
    
    string html = GenerateHTML(x_data, y_data, title, chart_type);
    
    std::ostringstream temp_path_stream;
#ifdef _WIN32
    const char* temp_dir = getenv("TEMP");
    if (!temp_dir) temp_dir = "C:\\Windows\\Temp";
    temp_path_stream << temp_dir << "\\duckdb_chart_" << time(nullptr) << ".html";
#else
    temp_path_stream << "/tmp/duckdb_chart_" << time(nullptr) << ".html";
#endif
    
    string temp_path = temp_path_stream.str();
    std::ofstream file(temp_path);
    file << html;
    file.close();
    
    OpenInBrowser(temp_path);
    result.SetValue(0, Value("Chart opened: " + temp_path));
}

// Scalar functions
inline void MiniplotScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &name_vector = args.data[0];
    UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
        return StringVector::AddString(result, "Miniplot " + name.GetString() + " 🐥"); 
    });
}

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

// Extension Load
void MiniplotExtension::Load(ExtensionLoader &loader) {
    loader.RegisterFunction(ScalarFunction("miniplot", 
        {LogicalType::VARCHAR}, 
        LogicalType::VARCHAR, 
        MiniplotScalarFun));
    
    loader.RegisterFunction(ScalarFunction("bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR, 
        BarChartFunction));
    
    loader.RegisterFunction(ScalarFunction("line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR, 
        LineChartFunction));
    
    loader.RegisterFunction(ScalarFunction("scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR, 
        ScatterChartFunction));
    
    loader.RegisterFunction(ScalarFunction("area_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR, 
        AreaChartFunction));
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
    // Connection経由で関数を登録
    
    duckdb::Connection con(db);
    con.BeginTransaction();
    
    auto &catalog = duckdb::Catalog::GetSystemCatalog(*con.context);
    
    // CreateScalarFunctionInfoを使って各関数を登録
    duckdb::CreateScalarFunctionInfo miniplot_func(
        duckdb::ScalarFunction("miniplot", 
            {duckdb::LogicalType::VARCHAR}, 
            duckdb::LogicalType::VARCHAR, 
            duckdb::MiniplotScalarFun));
    catalog.CreateFunction(*con.context, miniplot_func);
    
    duckdb::CreateScalarFunctionInfo bar_chart_func(
        duckdb::ScalarFunction("bar_chart",
            {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR), 
             duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
             duckdb::LogicalType::VARCHAR},
            duckdb::LogicalType::VARCHAR, 
            duckdb::BarChartFunction));
    catalog.CreateFunction(*con.context, bar_chart_func);
    
    duckdb::CreateScalarFunctionInfo line_chart_func(
        duckdb::ScalarFunction("line_chart",
            {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR), 
             duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
             duckdb::LogicalType::VARCHAR},
            duckdb::LogicalType::VARCHAR, 
            duckdb::LineChartFunction));
    catalog.CreateFunction(*con.context, line_chart_func);
    
    duckdb::CreateScalarFunctionInfo scatter_chart_func(
        duckdb::ScalarFunction("scatter_chart",
            {duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
             duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
             duckdb::LogicalType::VARCHAR},
            duckdb::LogicalType::VARCHAR, 
            duckdb::ScatterChartFunction));
    catalog.CreateFunction(*con.context, scatter_chart_func);
    
    duckdb::CreateScalarFunctionInfo area_chart_func(
        duckdb::ScalarFunction("area_chart",
            {duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR), 
             duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE), 
             duckdb::LogicalType::VARCHAR},
            duckdb::LogicalType::VARCHAR, 
            duckdb::AreaChartFunction));
    catalog.CreateFunction(*con.context, area_chart_func);
    
    con.Commit();
}

DUCKDB_EXTENSION_API const char *miniplot_version() {
    return duckdb::DuckDB::LibraryVersion();
}

}