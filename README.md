[Pending]# Miniplot

Interactive chart visualization extension for DuckDB - bringing pandas + matplotlib style data visualization directly to SQL, without leaving your database.

## 🎯 Mission

**"Replace pandas + matplotlib with just DuckDB"**

No more switching between pandas and matplotlib for data visualization.

#### One tool. One query. Instant visualization.

## ✨ What's New in v0.0.2

- 🔄 **Complete rewrite** using Plotly.js for better compatibility
- 🌐 **Browser-based rendering** - Charts open in your default browser
- 📦 **Fully offline** - Plotly.js embedded (3.4MB), no internet required
- 🎨 **Interactive features** - Zoom, pan, hover tooltips, export to PNG
- 🚀 **Simpler architecture** - No external binaries or process management
- ⚡ **Single file** - Just 9.2MB, install and use immediately

## Features

- 📊 **Multiple chart types**: Bar, Line, Scatter, and Area charts
- 🖥️ **Browser-based rendering**: Charts open in your default web browser
- 🚀 **Simple SQL interface**: Visualize data directly from SQL queries
- 💻 **Cross-platform**: Works on macOS, Linux, and Windows
- 🔒 **Fully offline**: No internet connection required
- ✨ **Interactive**: Zoom, pan, hover, and export capabilities

## Installation

```sql
INSTALL miniplot FROM community;
LOAD miniplot;
```

````

That's it! No additional setup required.

## Usage

### Bar Chart

```sql
SELECT bar_chart(
    ['Q1', 'Q2', 'Q3', 'Q4'],
    [100.0, 150.0, 200.0, 180.0],
    'Quarterly Sales'
);
```

### Line Chart

```sql
SELECT line_chart(
    ['Jan', 'Feb', 'Mar', 'Apr', 'May'],
    [5.2, 7.1, 12.5, 15.8, 20.3],
    'Monthly Temperature'
);
```

### Scatter Chart

```sql
SELECT scatter_chart(
    [1.0, 2.0, 3.0, 4.0, 5.0],
    [2.5, 5.0, 7.5, 10.0, 12.5],
    'Correlation Analysis'
);
```

### Area Chart

```sql
SELECT area_chart(
    ['Week1', 'Week2', 'Week3', 'Week4'],
    [1000.0, 1500.0, 1300.0, 1800.0],
    'Weekly Revenue'
);
```

## Real-World Example

```sql
-- Create sample data
CREATE TABLE sales AS
SELECT 'Mon' as day, 100.0 as amount UNION ALL
SELECT 'Tue', 150.0 UNION ALL
SELECT 'Wed', 120.0 UNION ALL
SELECT 'Thu', 180.0 UNION ALL
SELECT 'Fri', 140.0;

-- Visualize directly from table
SELECT bar_chart(
    list(day ORDER BY day),
    list(amount ORDER BY day),
    'Weekly Sales Report'
) FROM sales;
```

## How It Works

```
SQL Query → Data Extraction → HTML Generation → Browser Opens → Interactive Chart
```

1. **SQL Query** → Execute chart function with your data
2. **Data Extraction** → Extension processes your data
3. **HTML Generation** → Creates HTML with embedded Plotly.js
4. **Browser Opens** → Opens in your default browser
5. **Interactive Chart** → Zoom, pan, hover, export

## Why Miniplot?

### vs pandas + matplotlib

| Feature               | miniplot      | pandas + matplotlib   |
| --------------------- | ------------- | --------------------- |
| **Language**          | SQL only      | Python required       |
| **Setup**             | 1 SQL command | pip install × 2       |
| **Context Switching** | None          | Query → Python → Plot |
| **Interactive**       | ✅ Full       | ❌ Static images      |
| **Data Size**         | Unlimited     | Memory limited        |

### vs Jupyter + Plotly

| Feature          | miniplot       | Jupyter + Plotly  |
| ---------------- | -------------- | ----------------- |
| **Environment**  | Any SQL client | Browser + Server  |
| **Deployment**   | Single binary  | Python + packages |
| **Startup Time** | Instant        | Notebook loading  |
| **Sharing**      | SQL query      | Notebook file     |

## Architecture

### Simple and Clean

```
┌─────────────────────────────────┐
│   DuckDB SQL Query              │
│   SELECT bar_chart(...)         │
└────────────┬────────────────────┘
             ↓
┌─────────────────────────────────┐
│   Extension (9.2MB)             │
│   - C++ interface               │
│   - Rust HTML generator         │
│   - Plotly.js embedded          │
└────────────┬────────────────────┘
             ↓
┌─────────────────────────────────┐
│   Browser Opens                 │
│   - Interactive chart           │
│   - No internet needed          │
└─────────────────────────────────┘
```

**No external dependencies. No configuration. Just install and use.**

## Supported Chart Types

| Function        | X Axis    | Y Axis   | Description       |
| --------------- | --------- | -------- | ----------------- |
| `bar_chart`     | VARCHAR[] | DOUBLE[] | Vertical bars     |
| `line_chart`    | VARCHAR[] | DOUBLE[] | Line with markers |
| `scatter_chart` | DOUBLE[]  | DOUBLE[] | Scatter points    |
| `area_chart`    | VARCHAR[] | DOUBLE[] | Filled area       |

## Interactive Features

All charts include:

- 🔍 **Zoom** - Click and drag to zoom into regions
- 👆 **Pan** - Shift + drag to pan around
- 📊 **Hover** - Hover over points to see values
- 💾 **Export** - Download as PNG image
- 🔄 **Reset** - Double-click to reset view

## License

MIT License - see [LICENSE](LICENSE) file

## Acknowledgments

- Built with [DuckDB](https://duckdb.org/)
- Powered by [Plotly.js](https://plotly.com/javascript/)
- Written in [Rust](https://www.rust-lang.org/) and C++

## Links

- [Community Extensions](https://github.com/duckdb/community-extensions)
- [Issue Tracker](https://github.com/nkwork9999/miniplot/issues)
- [DuckDB Documentation](https://duckdb.org/docs/)

````
