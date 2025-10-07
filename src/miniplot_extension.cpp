#define DUCKDB_EXTENSION_MAIN

#include "miniplot_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/catalog_exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_entries.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/catalog/catalog.hpp"

#include <fstream>
#include <vector>
#include <openssl/opensslv.h>

namespace duckdb {

// Rust FFI declarations
extern "C" {
    struct ChartData {
        const char** x_labels;
        size_t x_len;
        const double* y_values;
        size_t y_len;
        const char* title;
        const char* chart_type;
    };
    
    int chart_viewer_show_chart(const ChartData* data);
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

// Helper function to extract string list
static std::vector<std::string> ExtractStringList(const Vector &list_vector, idx_t row_idx) {
    std::vector<std::string> result;
    
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

// Helper function to extract double list
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

// Helper function to call Rust chart viewer
static void CallChartViewer(
    const std::vector<std::string> &x_strings,
    const std::vector<double> &y_values,
    const string &title,
    const char* chart_type,
    Vector &result
) {
    // Validate input
    if (x_strings.empty() && y_values.empty()) {
        throw InvalidInputException("Chart data cannot be empty");
    }
    
    if (!x_strings.empty() && !y_values.empty() && x_strings.size() != y_values.size()) {
        throw InvalidInputException(
            "X and Y data must have the same length (X: %llu, Y: %llu)",
            (unsigned long long)x_strings.size(),
            (unsigned long long)y_values.size()
        );
    }
    
    try {
        // Convert to C arrays
        std::vector<const char*> x_ptrs;
        for (const auto& s : x_strings) {
            x_ptrs.push_back(s.c_str());
        }

        // Prepare ChartData
        ChartData chart_data;
        chart_data.x_labels = x_ptrs.empty() ? nullptr : x_ptrs.data();
        chart_data.x_len = x_ptrs.size();
        chart_data.y_values = y_values.empty() ? nullptr : y_values.data();
        chart_data.y_len = y_values.size();
        chart_data.title = title.c_str();
        chart_data.chart_type = chart_type;

        // Call Rust function
        int ret = chart_viewer_show_chart(&chart_data);
        
        if (ret == 0) {
            result.SetValue(0, Value("Chart opened in browser"));
        } else {
            throw IOException("Failed to open chart viewer (error code: %d)", ret);
        }
    } catch (const Exception &e) {
        throw;
    } catch (const std::exception &e) {
        throw InternalException("Unexpected error while opening chart: %s", e.what());
    }
}

// Bar chart function
inline void BarChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    try {
        auto &x_list = args.data[0];
        auto &y_list = args.data[1];
        auto &title_vec = args.data[2];

        // Extract data
        auto x_strings = ExtractStringList(x_list, 0);
        auto y_values = ExtractDoubleList(y_list, 0);
        string title_str = title_vec.GetValue(0).ToString();

        CallChartViewer(x_strings, y_values, title_str, "bar", result);
    } catch (const Exception &e) {
        throw;
    } catch (const std::exception &e) {
        throw InternalException("Bar chart error: %s", e.what());
    }
}

// Line chart function
inline void LineChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    try {
        auto &x_list = args.data[0];
        auto &y_list = args.data[1];
        auto &title_vec = args.data[2];

        auto x_strings = ExtractStringList(x_list, 0);
        auto y_values = ExtractDoubleList(y_list, 0);
        string title_str = title_vec.GetValue(0).ToString();

        CallChartViewer(x_strings, y_values, title_str, "line", result);
    } catch (const Exception &e) {
        throw;
    } catch (const std::exception &e) {
        throw InternalException("Line chart error: %s", e.what());
    }
}

// Scatter chart function
inline void ScatterChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    try {
        auto &x_list = args.data[0];
        auto &y_list = args.data[1];
        auto &title_vec = args.data[2];

        auto x_values = ExtractDoubleList(x_list, 0);
        auto y_values = ExtractDoubleList(y_list, 0);
        string title_str = title_vec.GetValue(0).ToString();

        // Convert X values to strings
        std::vector<std::string> x_strings;
        for (double v : x_values) {
            x_strings.push_back(std::to_string(v));
        }

        CallChartViewer(x_strings, y_values, title_str, "scatter", result);
    } catch (const Exception &e) {
        throw;
    } catch (const std::exception &e) {
        throw InternalException("Scatter chart error: %s", e.what());
    }
}

// Histogram function (コメントアウト - 未実装)
/*
inline void HistogramFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    try {
        auto &data_list = args.data[0];
        auto &title_vec = args.data[2];

        auto values = ExtractDoubleList(data_list, 0);
        string title_str = title_vec.GetValue(0).ToString();

        // Empty x labels for histogram
        std::vector<std::string> empty_labels;

        CallChartViewer(empty_labels, values, title_str, "histogram", result);
    } catch (const Exception &e) {
        throw;
    } catch (const std::exception &e) {
        throw InternalException("Histogram error: %s", e.what());
    }
}
*/

// Area chart function
inline void AreaChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    try {
        auto &x_list = args.data[0];
        auto &y_list = args.data[1];
        auto &title_vec = args.data[2];

        auto x_strings = ExtractStringList(x_list, 0);
        auto y_values = ExtractDoubleList(y_list, 0);
        string title_str = title_vec.GetValue(0).ToString();

        CallChartViewer(x_strings, y_values, title_str, "area", result);
    } catch (const Exception &e) {
        throw;
    } catch (const std::exception &e) {
        throw InternalException("Area chart error: %s", e.what());
    }
}

// LoadInternal (ExtensionLoader version)
static void LoadInternal(ExtensionLoader &loader) {
    // Test function
    auto miniplot_test = ScalarFunction("miniplot", {LogicalType::VARCHAR}, 
                                       LogicalType::VARCHAR, MiniplotTestFunction);
    loader.RegisterFunction(miniplot_test);

    // OpenSSL version function
    auto openssl_version = ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, 
                                         LogicalType::VARCHAR, MiniplotOpenSSLVersionFunction);
    loader.RegisterFunction(openssl_version);

    // Bar chart
    auto bar_chart = ScalarFunction(
        "bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, BarChartFunction);
    loader.RegisterFunction(bar_chart);

    // Line chart
    auto line_chart = ScalarFunction(
        "line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, LineChartFunction);
    loader.RegisterFunction(line_chart);

    // Scatter chart
    auto scatter_chart = ScalarFunction(
        "scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, ScatterChartFunction);
    loader.RegisterFunction(scatter_chart);

    // Histogram (コメントアウト - 未実装)
    /*
    auto histogram = ScalarFunction(
        "histogram_chart", 
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::VARCHAR},
        LogicalType::VARCHAR, HistogramFunction);
    loader.RegisterFunction(histogram);
    */

    // Area chart
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

    // Test function
    auto miniplot_test = ScalarFunction("miniplot", {LogicalType::VARCHAR}, 
                                       LogicalType::VARCHAR, MiniplotTestFunction);
    CreateScalarFunctionInfo test_info(miniplot_test);
    catalog.CreateFunction(context, test_info);

    // OpenSSL version function
    auto openssl_version = ScalarFunction("miniplot_openssl_version", {LogicalType::VARCHAR}, 
                                         LogicalType::VARCHAR, MiniplotOpenSSLVersionFunction);
    CreateScalarFunctionInfo openssl_info(openssl_version);
    catalog.CreateFunction(context, openssl_info);

    // Bar chart
    auto bar_chart = ScalarFunction(
        "bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, BarChartFunction);
    CreateScalarFunctionInfo bar_info(bar_chart);
    catalog.CreateFunction(context, bar_info);

    // Line chart
    auto line_chart = ScalarFunction(
        "line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, LineChartFunction);
    CreateScalarFunctionInfo line_info(line_chart);
    catalog.CreateFunction(context, line_info);

    // Scatter chart
    auto scatter_chart = ScalarFunction(
        "scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        LogicalType::VARCHAR, ScatterChartFunction);
    CreateScalarFunctionInfo scatter_info(scatter_info);
    catalog.CreateFunction(context, scatter_info);

    // Histogram (コメントアウト - 未実装)
    /*
    auto histogram = ScalarFunction(
        "histogram_chart", 
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::VARCHAR},
        LogicalType::VARCHAR, HistogramFunction);
    CreateScalarFunctionInfo hist_info(histogram);
    catalog.CreateFunction(context, hist_info);
    */

    // Area chart
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