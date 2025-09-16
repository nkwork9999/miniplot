use std::ffi::{c_char, CStr, CString};
use std::process::Command;


#[no_mangle]
pub extern "C" fn rust_hello_init() {

}


// #[no_mangle]
// pub extern "C" fn rust_hello_version() -> *const c_char {
//     let version = CString::new("0.1.0").unwrap();
//     version.into_raw()
// }


// #[no_mangle]
// pub extern "C" fn rust_hello_world() -> *const c_char {
//     let message = CString::new("Hello from Duck!").unwrap();
//     message.into_raw()
// }


#[no_mangle]
pub extern "C" fn rust_show_dynamic_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "bar")
}


#[no_mangle]
pub extern "C" fn rust_show_line_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "line")
}


#[no_mangle]
pub extern "C" fn rust_show_scatter_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "scatter")
}


#[no_mangle]
pub extern "C" fn rust_show_histogram(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "histogram")
}


#[no_mangle]
pub extern "C" fn rust_show_area_chart(data_path: *const c_char) -> *const c_char {
    show_chart_with_type(data_path, "area")
}


fn show_chart_with_type(data_path: *const c_char, chart_type: &str) -> *const c_char {
    let data_path_str = unsafe {
        CStr::from_ptr(data_path).to_string_lossy().to_string()
    };
    

    let chart_viewer_path = if let Ok(path) = std::env::var("CHART_VIEWER_PATH") {
        path
    } else {

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


#[no_mangle]
pub extern "C" fn rust_show_chart() -> *const c_char {

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


#[no_mangle]
pub extern "C" fn rust_hello_free(s: *mut c_char) {
    unsafe {
        if s.is_null() {
            return;
        }
        let _ = CString::from_raw(s);
    }
}