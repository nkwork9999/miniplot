# Miniplot

Interactive chart visualization extension for DuckDB, allowing you to create beautiful data visualizations directly from SQL queries.

## Features

- 📊 **Multiple chart types**: Bar, Line, Scatter, and Area charts
- 🖼️ **Native GUI rendering**: Charts open in native windows using Rust/Iced
- 🚀 **Simple SQL interface**: Visualize data directly from SQL queries
- 💻 **Cross-platform**: Works on macOS, Linux, and Windows

## Installation

### From Community Extensions (Coming Soon)

```sql
INSTALL miniplot FROM community;
LOAD miniplot;
```

````

### From Source

```sql
LOAD './build/release/extension/miniplot/miniplot.duckdb_extension';
```

## Usage

### Bar Chart

```sql
SELECT bar_chart(
    LIST_VALUE('Q1', 'Q2', 'Q3', 'Q4'),
    LIST_VALUE(100, 150, 200, 180),
    'Quarterly Sales'
);
```

### Line Chart

```sql
SELECT line_chart(
    LIST_VALUE('Jan', 'Feb', 'Mar', 'Apr', 'May'),
    LIST_VALUE(5.2, 7.1, 12.5, 15.8, 20.3),
    'Monthly Temperature'
);
```

### Scatter Chart

```sql
SELECT scatter_chart(
    LIST_VALUE(1.0, 2.0, 3.0, 4.0, 5.0),
    LIST_VALUE(2.5, 5.0, 7.5, 10.0, 12.5),
    'Correlation Analysis'
);
```

### Area Chart

```sql
SELECT area_chart(
    LIST_VALUE('Week1', 'Week2', 'Week3', 'Week4'),
    LIST_VALUE(1000, 1500, 1300, 1800),
    'Weekly Revenue'
);
```

## Building

### Prerequisites

- DuckDB development files
- Rust toolchain (1.70+)
- CMake
- C++11 compiler

### Managing dependencies

DuckDB extensions uses VCPKG for dependency management. Enabling VCPKG is very simple: follow the [installation instructions](https://vcpkg.io/en/getting-started) or just run the following:

```shell
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_TOOLCHAIN_PATH=`pwd`/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Build steps

```sh
# Clone with submodules
git clone --recurse-submodules https://github.com/nkwork9999/miniplot.git
cd miniplot

# Build Rust components and extension
./build.sh

# Or manually:
cd rust_lib && cargo build --release && cd ..
cd chart_viewer && cargo build --release && cd ..
make
```

The main binaries that will be built are:

- `./build/release/duckdb` - DuckDB shell with the extension loaded
- `./build/release/test/unittest` - Test runner
- `./build/release/extension/miniplot/miniplot.duckdb_extension` - Loadable extension

## Running the extension

To run the extension code, simply start the shell with `./build/release/duckdb`.

## Running the tests

```sh
make test
```

## Architecture

Miniplot uses a hybrid architecture:

- **C++ Extension**: DuckDB interface
- **Rust FFI Library**: Data processing
- **Iced Application**: Chart rendering

## Configuration

Set custom chart viewer path if needed:

```bash
export CHART_VIEWER_PATH=/path/to/chart_viewer
```

## Limitations

- Requires graphical environment (not suitable for headless servers)
- Charts open in separate windows
- External process launch may require security permissions

## License

MIT License - see [LICENSE](LICENSE) file

## Acknowledgments

Based on [DuckDB Extension Template](https://github.com/duckdb/extension-template)
````
