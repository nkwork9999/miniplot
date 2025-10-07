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
#include <openssl/opensslv.h>

namespace duckdb {

// Rust FFI function declarations
extern "C" {
    void chart_viewer_show_chart(
        const char* title,
        const char* x_data_json,
        const char* y_data_json,
        const char* chart_type
    );
}

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

// Helper function to convert list to JSON string
static string ListToJson(const Vector &list_vector, idx_t row_idx) {
    auto val = list_vector.GetValue(row_idx);
    if (val.IsNull() || val.type().id() != LogicalTypeId::LIST) {
        return "[]";
    }
    
    auto &children = ListValue::GetChildren(val);
    string json = "[";
    for (idx_t i = 0; i < children.size(); i++) {
        if (i > 0) json += ",";
        auto child_str = children[i].ToString();
        // Check if it's a number
        bool is_number = !child_str.empty() && 
                        (std::isdigit(child_str[0]) || child_str[0] == '-' || child_str[0] == '.');
        if (is_number) {
            json += child_str;
        } else {
            json += "\"" + child_str + "\"";
        }
    }
    json += "]";
    return json;
}

// Bar chart function
inline void BarChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];

    string title_str = title.GetValue(0).ToString();
    string x_json = ListToJson(x_list, 0);
    string y_json = ListToJson(y_list, 0);

    chart_viewer_show_chart(
        title_str.c_str(),
        x_json.c_str(),
        y_json.c_str(),
        "bar"
    );

    result.SetValue(0, Value("Bar chart window opened"));
}

// Line chart function
inline void LineChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];

    string title_str = title.GetValue(0).ToString();
    string x_json = ListToJson(x_list, 0);
    string y_json = ListToJson(y_list, 0);

    chart_viewer_show_chart(
        title_str.c_str(),
        x_json.c_str(),
        y_json.c_str(),
        "line"
    );

    result.SetValue(0, Value("Line chart window opened"));
}

// Scatter chart function
inline void ScatterChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];

    string title_str = title.GetValue(0).ToString();
    string x_json = ListToJson(x_list, 0);
    string y_json = ListToJson(y_list, 0);

    chart_viewer_show_chart(
        title_str.c_str(),
        x_json.c_str(),
        y_json.c_str(),
        "scatter"
    );

    result.SetValue(0, Value("Scatter chart window opened"));
}

// Histogram function
inline void HistogramFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &data_list = args.data[0];
    auto &bins = args.data[1];
    auto &title = args.data[2];

    string title_str = title.GetValue(0).ToString();
    string x_json = "[]";  // Empty for histogram
    string y_json = ListToJson(data_list, 0);

    chart_viewer_show_chart(
        title_str.c_str(),
        x_json.c_str(),
        y_json.c_str(),
        "histogram"
    );

    result.SetValue(0, Value("Histogram window opened"));
}

// Area chart function
inline void AreaChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];

    string title_str = title.GetValue(0).ToString();
    string x_json = ListToJson(x_list, 0);
    string y_json = ListToJson(y_list, 0);

    chart_viewer_show_chart(
        title_str.c_str(),
        x_json.c_str(),
        y_json.c_str(),
        "area"
    );

    result.SetValue(0, Value("Area chart window opened"));
}

// LoadInternal (ExtensionLoader version)
static void LoadInternal(ExtensionLoader &loader) {
    auto miniplot_test = ScalarFunction("miniplot", {LogicalType::VARCHAR}, 
                                       LogicalType::VARCHAR, MiniplotTestFunction);
    loader.RegisterFunction(miniplot_test);

    auto openssl_version = ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, 
                                         LogicalType::VARCHAR, MiniplotOpenSSLVersionFunction);
    loader.RegisterFunction(openssl_version);

    auto bar_chart = ScalarFunction(
        "bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, BarChartFunction);
    loader.RegisterFunction(bar_chart);

    auto line_chart = ScalarFunction(
        "line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, LineChartFunction);
    loader.RegisterFunction(line_chart);

    auto scatter_chart = ScalarFunction(
        "scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, ScatterChartFunction);
    loader.RegisterFunction(scatter_chart);

    auto histogram = ScalarFunction(
        "histogram_chart", 
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::VARCHAR},
        LogicalType::VARCHAR, HistogramFunction);
    loader.RegisterFunction(histogram);

    auto area_chart = ScalarFunction(
        "area_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, AreaChartFunction);
    loader.RegisterFunction(area_chart);
}

// LoadInternal (DatabaseInstance version)
static void LoadInternal(DatabaseInstance &instance) {
    Connection con(instance);
    auto &context = *con.context;
    auto &catalog = Catalog::GetSystemCatalog(instance);

    auto miniplot_test = ScalarFunction("miniplot", {LogicalType::VARCHAR}, 
                                       LogicalType::VARCHAR, MiniplotTestFunction);
    CreateScalarFunctionInfo test_info(miniplot_test);
    catalog.CreateFunction(context, test_info);

    auto openssl_version = ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, 
                                         LogicalType::VARCHAR, MiniplotOpenSSLVersionFunction);
    CreateScalarFunctionInfo openssl_info(openssl_version);
    catalog.CreateFunction(context, openssl_info);

    auto bar_chart = ScalarFunction(
        "bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, BarChartFunction);
    CreateScalarFunctionInfo bar_info(bar_chart);
    catalog.CreateFunction(context, bar_info);

    auto line_chart = ScalarFunction(
        "line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, LineChartFunction);
    CreateScalarFunctionInfo line_info(line_chart);
    catalog.CreateFunction(context, line_info);

    auto scatter_chart = ScalarFunction(
        "scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, ScatterChartFunction);
    CreateScalarFunctionInfo scatter_info(scatter_chart);
    catalog.CreateFunction(context, scatter_info);

    auto histogram = ScalarFunction(
        "histogram_chart", 
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::VARCHAR},
        LogicalType::VARCHAR, HistogramFunction);
    CreateScalarFunctionInfo hist_info(histogram);
    catalog.CreateFunction(context, hist_info);

    auto area_chart = ScalarFunction(
        "area_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, AreaChartFunction);
    CreateScalarFunctionInfo area_info(area_chart);
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
    return "0.0.1";
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