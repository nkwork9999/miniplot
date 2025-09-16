use std::ffi::{c_char, CStr, CString};
use std::process::Command;

/// 拡張機能の初期化関数
#[no_mangle]
pub extern "C" fn rust_hello_init() {
    // 初期化処理（必要に応じて）
}

// /// バージョンを返す関数
// #[no_mangle]
// pub extern "C" fn rust_hello_version() -> *const c_char {
//     let version = CString::new("0.1.0").unwrap();
//     version.into_raw()
// }

// /// Hello Worldを返す関数
// #[no_mangle]
// pub extern "C" fn rust_hello_world() -> *const c_char {
//     let message = CString::new("Hello from Duck!").unwrap();
//     message.into_raw()
// }

/// 動的な棒グラフを表示する関数
#[no_mangle]
pub extern "C" fn rust_show_dynamic_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "bar")
}

/// 折れ線グラフを表示する関数
#[no_mangle]
pub extern "C" fn rust_show_line_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "line")
}

/// 散布図を表示する関数
#[no_mangle]
pub extern "C" fn rust_show_scatter_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "scatter")
}

/// ヒストグラムを表示する関数
#[no_mangle]
pub extern "C" fn rust_show_histogram(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "histogram")
}

/// エリアチャートを表示する関数
#[no_mangle]
pub extern "C" fn rust_show_area_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "area")
}

/// 共通のチャート表示関数（チャートタイプをコマンドライン引数として渡す）
fn show_chart_with_type(data_path: *const c_char, chart_type: &str) -> *const c_char {
    let data_path_str = unsafe {
        CStr::from_ptr(data_path).to_string_lossy().to_string()
    };
    
    // パスを修正：環境変数またはビルド後の相対パス
    let chart_viewer_path = if let Ok(path) = std::env::var("CHART_VIEWER_PATH") {
        path
    } else {
        // miniplotディレクトリからの相対パス
        "./chart_viewer/target/release/chart_viewer".to_string()
    };
    
    match Command::new(&chart_viewer_path)
        .arg(&data_path_str)
        .arg(chart_type)
        .spawn() 
    {
        Ok(_) => {
            let message = CString::new(format!("{} chart viewer launched", chart_type)).unwrap();
            message.into_raw()
        }
        Err(_) => {
            // フォールバック1: ルートディレクトリ
            match Command::new("./chart_viewer")
                .arg(&data_path_str)
                .arg(chart_type)
                .spawn() 
            {
                Ok(_) => {
                    let message = CString::new(format!("{} chart viewer launched", chart_type)).unwrap();
                    message.into_raw()
                }
                Err(_) => {
                    // フォールバック2: PATH内を探す
                    match Command::new("chart_viewer")
                        .arg(&data_path_str)
                        .arg(chart_type)
                        .spawn() 
                    {
                        Ok(_) => {
                            let message = CString::new(format!("{} chart viewer launched", chart_type)).unwrap();
                            message.into_raw()
                        }
                        Err(e) => {
                            let message = CString::new(format!("Failed to launch {} viewer: {}. Set CHART_VIEWER_PATH environment variable.", chart_type, e)).unwrap();
                            message.into_raw()
                        }
                    }
                }
            }
        }
    }
}

/// 棒グラフを表示する関数（既存・後方互換性のため残す）
#[no_mangle]
pub extern "C" fn rust_show_chart() -> *const c_char {
    // 環境変数またはデフォルトパス
    let chart_viewer_path = if let Ok(path) = std::env::var("CHART_VIEWER_PATH") {
        path
    } else {
        "./chart_viewer/target/release/chart_viewer".to_string()
    };
    
    match Command::new(&chart_viewer_path).spawn() {
        Ok(_) => {
            let message = CString::new("Chart viewer launched").unwrap();
            message.into_raw()
        }
        Err(_) => {
            // フォールバック
            match Command::new("./chart_viewer").spawn() {
                Ok(_) => {
                    let message = CString::new("Chart viewer launched").unwrap();
                    message.into_raw()
                }
                Err(_) => {
                    match Command::new("chart_viewer").spawn() {
                        Ok(_) => {
                            let message = CString::new("Chart viewer launched").unwrap();
                            message.into_raw()
                        }
                        Err(e) => {
                            let message = CString::new(format!("Failed to launch chart viewer: {}. Set CHART_VIEWER_PATH environment variable.", e)).unwrap();
                            message.into_raw()
                        }
                    }
                }
            }
        }
    }
}

/// メモリ解放用の関数
#[no_mangle]
pub extern "C" fn rust_hello_free(s: *mut c_char) {
    unsafe {
        if s.is_null() {
            return;
        }
        let _ = CString::from_raw(s);
    }
}