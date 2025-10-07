use chart_viewer::{ChartData, ChartApp};
use std::fs;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        eprintln!("Usage: chart_viewer_standalone <data.json>");
        std::process::exit(1);
    }
    
    // JSONファイルからデータを読み込む
    let json_str = fs::read_to_string(&args[1])?;
    let json: serde_json::Value = serde_json::from_str(&json_str)?;
    
    let data = ChartData {
        title: json["title"].as_str().unwrap_or("Chart").to_string(),
        x_data: json["x_data"]
            .as_array()
            .unwrap_or(&vec![])
            .iter()
            .map(|v| v.as_str().unwrap_or("").to_string())
            .collect(),
        y_data: json["y_data"]
            .as_array()
            .unwrap_or(&vec![])
            .iter()
            .filter_map(|v| v.as_f64())
            .collect(),
        chart_type: json["chart_type"].as_str().unwrap_or("bar").to_string(),
    };
    
    // JSONファイルを削除
    let _ = fs::remove_file(&args[1]);
    
    ChartApp::run(data)?;
    
    Ok(())
}