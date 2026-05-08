use std::env;

fn main() {
    if let Ok(lib_dir) = env::var("QENGINE_FFI_LIB_DIR") {
        println!("cargo:rustc-link-search=native={lib_dir}");
        println!("cargo:rustc-link-lib=static=qengine_ffi");
        if env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default() != "msvc" {
            println!("cargo:rustc-link-lib=dylib=stdc++");
        }
    }

    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    match target_os.as_str() {
        "linux" => println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN"),
        "macos" => println!("cargo:rustc-link-arg=-Wl,-rpath,@loader_path"),
        _ => {}
    }
}
