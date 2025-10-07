fn main() {
    #[cfg(target_os = "macos")]
    {
        // dispatch_get_main_queue と dispatch_async_f は libSystem に含まれる
        println!("cargo:rustc-link-lib=dylib=System");
    }
}