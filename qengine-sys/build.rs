/// Build script for qengine-sys.
///
/// Compiles the thin C++ bridge that wraps cycfi/Q pitch detection.
/// Requires the `third_party/q` and `third_party/infra` git submodules to be
/// initialised (`git submodule update --init --recursive`).
use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let root = manifest.parent().unwrap();

    let q_include = root.join("third_party/q/q_lib/include");
    let infra_include = root.join("third_party/infra/include");

    // Verify that the submodules are present.
    if !q_include.join("q/pitch/pitch_detector.hpp").exists() {
        panic!(
            "Q library headers not found at {q_include:?}.\n\
             Please run: git submodule update --init --recursive"
        );
    }

    // Compile the C++ bridge.
    cc::Build::new()
        .cpp(true)
        .std("c++20")
        .include(&q_include)
        .include(&infra_include)
        .include(manifest.join("src/cpp"))
        .file(manifest.join("src/cpp/q_bridge.cpp"))
        .warnings(false) // suppress noisy template warnings from Q
        .compile("q_bridge");

    println!("cargo:rerun-if-changed=src/cpp/q_bridge.cpp");
    println!("cargo:rerun-if-changed=src/cpp/q_bridge.h");
}
