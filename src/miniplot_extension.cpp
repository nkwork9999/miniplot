#define DUCKDB_EXTENSION_MAIN

#include "miniplot_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <duckdb/common/vector_operations/vector_operations.hpp>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace duckdb {

// Rust関数の型定義（使用する関数のみ）
typedef void (*rust_hello_init_fn)();
typedef const char* (*rust_show_dynamic_chart_fn)(const char*);
typedef const char* (*rust_show_line_chart_fn)(const char*);
typedef const char* (*rust_show_scatter_chart_fn)(const char*);
typedef const char* (*rust_show_histogram_fn)(const char*);
typedef const char* (*rust_show_area_chart_fn)(const char*);
typedef const char* (*rust_show_chart_fn)();
typedef void (*rust_hello_free_fn)(char*);

// グローバル関数ポインタ
static rust_hello_init_fn rust_hello_init = nullptr;
static rust_show_dynamic_chart_fn rust_show_dynamic_chart = nullptr;
static rust_show_line_chart_fn rust_show_line_chart = nullptr;
static rust_show_scatter_chart_fn rust_show_scatter_chart = nullptr;
static rust_show_histogram_fn rust_show_histogram = nullptr;
static rust_show_area_chart_fn rust_show_area_chart = nullptr;
static rust_show_chart_fn rust_show_chart = nullptr;
static rust_hello_free_fn rust_hello_free = nullptr;
static void* rust_lib_handle = nullptr;

// テスト関数
inline void MiniplotTestFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &name_vector = args.data[0];
    UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
        return StringVector::AddString(result, "Miniplot " + name.GetString() + " 📊");
    });
}

// バーチャート関数
inline void BarChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];
    
    string data_path = "/tmp/miniplot_data.txt";
    std::ofstream file(data_path);
    
    // タイトル
    file << title.GetValue(0).ToString() << "\n";
    
    // X軸データ
    auto x_val = x_list.GetValue(0);
    if (!x_val.IsNull() && x_val.type().id() == LogicalTypeId::LIST) {
        auto &x_children = ListValue::GetChildren(x_val);
        for (idx_t i = 0; i < x_children.size(); i++) {
            if (i > 0) file << ",";
            file << x_children[i].ToString();
        }
    }
    file << "\n";
    
    // Y軸データ
    auto y_val = y_list.GetValue(0);
    if (!y_val.IsNull() && y_val.type().id() == LogicalTypeId::LIST) {
        auto &y_children = ListValue::GetChildren(y_val);
        for (idx_t i = 0; i < y_children.size(); i++) {
            if (i > 0) file << ",";
            file << y_children[i].ToString();
        }
    }
    file << "\n";
    file.close();
    
    if (rust_show_dynamic_chart) {
        const char* msg = rust_show_dynamic_chart(data_path.c_str());
        result.SetValue(0, Value(string(msg)));
        if (rust_hello_free && msg) {
            rust_hello_free((char*)msg);
        }
    } else {
        result.SetValue(0, Value("Bar chart function not loaded"));
    }
}

// 折れ線グラフ関数
inline void LineChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];
    
    string data_path = "/tmp/miniplot_line_data.txt";
    std::ofstream file(data_path);
    
    file << title.GetValue(0).ToString() << "\n";
    
    auto x_val = x_list.GetValue(0);
    if (!x_val.IsNull() && x_val.type().id() == LogicalTypeId::LIST) {
        auto &x_children = ListValue::GetChildren(x_val);
        for (idx_t i = 0; i < x_children.size(); i++) {
            if (i > 0) file << ",";
            file << x_children[i].ToString();
        }
    }
    file << "\n";
    
    auto y_val = y_list.GetValue(0);
    if (!y_val.IsNull() && y_val.type().id() == LogicalTypeId::LIST) {
        auto &y_children = ListValue::GetChildren(y_val);
        for (idx_t i = 0; i < y_children.size(); i++) {
            if (i > 0) file << ",";
            file << y_children[i].ToString();
        }
    }
    file << "\n";
    file.close();
    
    if (rust_show_line_chart) {
        const char* msg = rust_show_line_chart(data_path.c_str());
        result.SetValue(0, Value(string(msg)));
        if (rust_hello_free && msg) {
            rust_hello_free((char*)msg);
        }
    } else {
        result.SetValue(0, Value("Line chart function not loaded"));
    }
}

// 散布図関数
inline void ScatterChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];
    
    string data_path = "/tmp/miniplot_scatter_data.txt";
    std::ofstream file(data_path);
    
    file << title.GetValue(0).ToString() << "\n";
    
    auto x_val = x_list.GetValue(0);
    if (!x_val.IsNull() && x_val.type().id() == LogicalTypeId::LIST) {
        auto &x_children = ListValue::GetChildren(x_val);
        for (idx_t i = 0; i < x_children.size(); i++) {
            if (i > 0) file << ",";
            file << x_children[i].ToString();
        }
    }
    file << "\n";
    
    auto y_val = y_list.GetValue(0);
    if (!y_val.IsNull() && y_val.type().id() == LogicalTypeId::LIST) {
        auto &y_children = ListValue::GetChildren(y_val);
        for (idx_t i = 0; i < y_children.size(); i++) {
            if (i > 0) file << ",";
            file << y_children[i].ToString();
        }
    }
    file << "\n";
    file.close();
    
    if (rust_show_scatter_chart) {
        const char* msg = rust_show_scatter_chart(data_path.c_str());
        result.SetValue(0, Value(string(msg)));
        if (rust_hello_free && msg) {
            rust_hello_free((char*)msg);
        }
    } else {
        result.SetValue(0, Value("Scatter chart function not loaded"));
    }
}

// ヒストグラム関数
inline void HistogramFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &data_list = args.data[0];
    auto &bins = args.data[1];
    auto &title = args.data[2];
    
    string data_path = "/tmp/miniplot_histogram_data.txt";
    std::ofstream file(data_path);
    
    file << title.GetValue(0).ToString() << "\n";
    
    // データ値
    auto data_val = data_list.GetValue(0);
    if (!data_val.IsNull() && data_val.type().id() == LogicalTypeId::LIST) {
        auto &data_children = ListValue::GetChildren(data_val);
        for (idx_t i = 0; i < data_children.size(); i++) {
            if (i > 0) file << ",";
            file << data_children[i].ToString();
        }
    }
    file << "\n";
    
    // ビン数
    file << bins.GetValue(0).ToString() << "\n";
    file.close();
    
    if (rust_show_histogram) {
        const char* msg = rust_show_histogram(data_path.c_str());
        result.SetValue(0, Value(string(msg)));
        if (rust_hello_free && msg) {
            rust_hello_free((char*)msg);
        }
    } else {
        result.SetValue(0, Value("Histogram function not loaded"));
    }
}

// エリアチャート関数
inline void AreaChartFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &x_list = args.data[0];
    auto &y_list = args.data[1];
    auto &title = args.data[2];
    
    string data_path = "/tmp/miniplot_area_data.txt";
    std::ofstream file(data_path);
    
    file << title.GetValue(0).ToString() << "\n";
    
    auto x_val = x_list.GetValue(0);
    if (!x_val.IsNull() && x_val.type().id() == LogicalTypeId::LIST) {
        auto &x_children = ListValue::GetChildren(x_val);
        for (idx_t i = 0; i < x_children.size(); i++) {
            if (i > 0) file << ",";
            file << x_children[i].ToString();
        }
    }
    file << "\n";
    
    auto y_val = y_list.GetValue(0);
    if (!y_val.IsNull() && y_val.type().id() == LogicalTypeId::LIST) {
        auto &y_children = ListValue::GetChildren(y_val);
        for (idx_t i = 0; i < y_children.size(); i++) {
            if (i > 0) file << ",";
            file << y_children[i].ToString();
        }
    }
    file << "\n";
    file.close();
    
    if (rust_show_area_chart) {
        const char* msg = rust_show_area_chart(data_path.c_str());
        result.SetValue(0, Value(string(msg)));
        if (rust_hello_free && msg) {
            rust_hello_free((char*)msg);
        }
    } else {
        result.SetValue(0, Value("Area chart function not loaded"));
    }
}

// Rustライブラリのロード
static void LoadRustLibrary(DatabaseInstance &instance) {
    if (rust_lib_handle) return;
    
    string lib_path;
    const char* env_path = std::getenv("MINIPLOT_LIB_PATH");
    
    if (env_path) {
        lib_path = env_path;
    } else {
#ifdef __APPLE__
        lib_path = "./rust_lib/target/release/libminiplot_rust.dylib";
#elif defined(_WIN32)
        lib_path = "./rust_lib/target/release/miniplot_rust.dll";
#else
        lib_path = "./rust_lib/target/release/libminiplot_rust.so";
#endif
    }
    
#ifdef _WIN32
    rust_lib_handle = LoadLibrary(lib_path.c_str());
    if (rust_lib_handle) {
        rust_hello_init = (rust_hello_init_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_hello_init");
        rust_show_dynamic_chart = (rust_show_dynamic_chart_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_show_dynamic_chart");
        rust_show_line_chart = (rust_show_line_chart_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_show_line_chart");
        rust_show_scatter_chart = (rust_show_scatter_chart_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_show_scatter_chart");
        rust_show_histogram = (rust_show_histogram_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_show_histogram");
        rust_show_area_chart = (rust_show_area_chart_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_show_area_chart");
        rust_show_chart = (rust_show_chart_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_show_chart");
        rust_hello_free = (rust_hello_free_fn)GetProcAddress((HMODULE)rust_lib_handle, "rust_hello_free");
    }
#else
    rust_lib_handle = dlopen(lib_path.c_str(), RTLD_LAZY);
    if (rust_lib_handle) {
        rust_hello_init = (rust_hello_init_fn)dlsym(rust_lib_handle, "rust_hello_init");
        rust_show_dynamic_chart = (rust_show_dynamic_chart_fn)dlsym(rust_lib_handle, "rust_show_dynamic_chart");
        rust_show_line_chart = (rust_show_line_chart_fn)dlsym(rust_lib_handle, "rust_show_line_chart");
        rust_show_scatter_chart = (rust_show_scatter_chart_fn)dlsym(rust_lib_handle, "rust_show_scatter_chart");
        rust_show_histogram = (rust_show_histogram_fn)dlsym(rust_lib_handle, "rust_show_histogram");
        rust_show_area_chart = (rust_show_area_chart_fn)dlsym(rust_lib_handle, "rust_show_area_chart");
        rust_show_chart = (rust_show_chart_fn)dlsym(rust_lib_handle, "rust_show_chart");
        rust_hello_free = (rust_hello_free_fn)dlsym(rust_lib_handle, "rust_hello_free");
    }
#endif
    
    if (rust_hello_init) {
        rust_hello_init();
    }
}

static void LoadInternal(DatabaseInstance &instance) {
    // Rustライブラリのロード（オプショナル）
    LoadRustLibrary(instance);
    
    // テスト関数
    auto miniplot_test = ScalarFunction("miniplot", {LogicalType::VARCHAR}, LogicalType::VARCHAR, MiniplotTestFunction);
    ExtensionUtil::RegisterFunction(instance, miniplot_test);
    
    // バーチャート
    auto bar_chart = ScalarFunction(
        "bar_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        BarChartFunction
    );
    ExtensionUtil::RegisterFunction(instance, bar_chart);
    
    // 折れ線グラフ
    auto line_chart = ScalarFunction(
        "line_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        LineChartFunction
    );
    ExtensionUtil::RegisterFunction(instance, line_chart);
    
    // 散布図
    auto scatter_chart = ScalarFunction(
        "scatter_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        ScatterChartFunction
    );
    ExtensionUtil::RegisterFunction(instance, scatter_chart);
    
    // ヒストグラム
    auto histogram = ScalarFunction(
        "histogram_chart",
        {LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::INTEGER,
         LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        HistogramFunction
    );
    ExtensionUtil::RegisterFunction(instance, histogram);
    
    // エリアチャート
    auto area_chart = ScalarFunction(
        "area_chart",
        {LogicalType::LIST(LogicalType::VARCHAR), 
         LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        AreaChartFunction
    );
    ExtensionUtil::RegisterFunction(instance, area_chart);
}

void MiniplotExtension::Load(DuckDB &db) {
    LoadInternal(*db.instance);
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

DUCKDB_EXTENSION_API void miniplot_init(duckdb::DatabaseInstance &db) {
    duckdb::DuckDB db_wrapper(db);
    db_wrapper.LoadExtension<duckdb::MiniplotExtension>();
}

DUCKDB_EXTENSION_API const char *miniplot_version() {
    return duckdb::DuckDB::LibraryVersion();
}

}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif