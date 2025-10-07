use std::ffi::CStr;
use std::os::raw::c_char;
use std::fs;
use std::process::Command;

// Plotly.jsをバイナリに埋め込む
const PLOTLY_JS: &str = include_str!("../assets/plotly.min.js");

#[repr(C)]
pub struct ChartData {
    pub x_labels: *const *const c_char,
    pub x_len: usize,
    pub y_values: *const f64,
    pub y_len: usize,
    pub title: *const c_char,
    pub chart_type: *const c_char,
}

#[no_mangle]
pub extern "C" fn chart_viewer_show_chart(data: *const ChartData) -> i32 {
    unsafe {
        if data.is_null() {
            return -1;
        }
        
        let chart_data = &*data;
        
        // タイトル取得
        let title = CStr::from_ptr(chart_data.title)
            .to_str()
            .unwrap_or("Chart");
        
        // チャートタイプ取得
        let chart_type = CStr::from_ptr(chart_data.chart_type)
            .to_str()
            .unwrap_or("bar");
        
        // X軸ラベル取得
        let mut x_labels = Vec::new();
        for i in 0..chart_data.x_len {
            let label_ptr = *chart_data.x_labels.add(i);
            let label = CStr::from_ptr(label_ptr).to_str().unwrap_or("");
            x_labels.push(label);
        }
        
        // Y軸値取得
        let y_values = std::slice::from_raw_parts(chart_data.y_values, chart_data.y_len);
        
        // HTML生成（Plotly.js埋め込み）
        let html = generate_html(title, &x_labels, y_values, chart_type);
        
        // 一時ファイルに書き込み
        let temp_dir = std::env::temp_dir();
        let html_path = temp_dir.join(format!("duckdb_chart_{}.html", std::process::id()));
        
        if fs::write(&html_path, html).is_err() {
            return -1;
        }
        
        // ブラウザで開く
        if open_in_browser(&html_path).is_err() {
            return -1;
        }
        
        0
    }
}

fn generate_html(
    title: &str,
    x_labels: &[&str],
    y_values: &[f64],
    chart_type: &str
) -> String {
    // エスケープ処理
    let title_escaped = title.replace("'", "\\'").replace("\n", "\\n");
    
    // X軸のJavaScript配列
    let x_array = x_labels.iter()
        .map(|s| format!("'{}'", s.replace("'", "\\'")))
        .collect::<Vec<_>>()
        .join(", ");
    
    // Y軸のJavaScript配列
    let y_array = y_values.iter()
        .map(|v| v.to_string())
        .collect::<Vec<_>>()
        .join(", ");
    
    // チャートタイプのマッピング
    let (plotly_type, mode) = match chart_type {
        "line" => ("scatter", ", mode: 'lines+markers'"),
        "scatter" => ("scatter", ", mode: 'markers'"),
        "area" => ("scatter", ", mode: 'lines', fill: 'tozeroy'"),
        _ => ("bar", ""),
    };
    
    format!(r#"<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{title_escaped} - DuckDB Chart</title>
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}
        body {{
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Helvetica Neue", Arial, sans-serif;
            background: #f5f5f5;
            padding: 15px;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }}
        h1 {{
            margin: 0 0 15px 0;
            color: #333;
            font-size: 20px;
            font-weight: 600;
        }}
        #chart {{
            width: 100%;
            height: 550px;
        }}
        .info {{
            margin-top: 15px;
            padding: 10px;
            background: #f8f9fa;
            border-radius: 6px;
            font-size: 12px;
            color: #666;
            text-align: center;
        }}
    </style>
</head>
<body>
    <div class="container">
        <h1>{title_escaped}</h1>
        <div id="chart"></div>
        <div class="info">
            📊 DuckDB miniplot • Plotly.js • Fully offline
        </div>
    </div>
    
    <script>
    // Plotly.js embedded (offline mode)
    {PLOTLY_JS}
    </script>
    
    <script>
    // Chart data and configuration
    var data = [{{
        x: [{x_array}],
        y: [{y_array}],
        type: '{plotly_type}'{mode},
        marker: {{
            color: 'rgb(51, 153, 204)',
            size: 8
        }},
        line: {{
            color: 'rgb(51, 153, 204)',
            width: 2
        }}
    }}];
    
    var layout = {{
        xaxis: {{
            showgrid: true,
            gridcolor: '#e0e0e0'
        }},
        yaxis: {{
            title: 'Value',
            showgrid: true,
            gridcolor: '#e0e0e0'
        }},
        plot_bgcolor: '#fafafa',
        paper_bgcolor: '#ffffff',
        font: {{
            family: '-apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif',
            size: 12
        }},
        margin: {{ t: 30, r: 30, b: 50, l: 60 }},
        autosize: true
    }};
    
    var config = {{
        responsive: true,
        displayModeBar: true,
        displaylogo: false,
        modeBarButtonsToRemove: ['lasso2d', 'select2d'],
        toImageButtonOptions: {{
            format: 'png',
            filename: 'duckdb_chart',
            height: 600,
            width: 1000,
            scale: 2
        }}
    }};
    
    Plotly.newPlot('chart', data, layout, config);
    
    // ウィンドウリサイズ対応
    window.addEventListener('resize', function() {{
        Plotly.Plots.resize('chart');
    }});
    </script>
</body>
</html>"#)
}

fn open_in_browser(path: &std::path::Path) -> std::io::Result<()> {
    #[cfg(target_os = "macos")]
    {
        Command::new("open").arg(path).spawn()?;
    }
    
    #[cfg(target_os = "linux")]
    {
        Command::new("xdg-open").arg(path).spawn()?;
    }
    
    #[cfg(target_os = "windows")]
    {
        Command::new("cmd")
            .args(&["/C", "start", "", path.to_str().unwrap()])
            .spawn()?;
    }
    
    Ok(())
}