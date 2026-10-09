// Deliberately minimal.
//
// The Qt-facing bridge (moc + C++) is NOT compiled here: it lives in
// CMakeLists.txt, because the final link must be performed by a C++ driver.
// rustc's `cdylib` link step emits a version script that localizes every
// symbol except the crate's own `#[no_mangle]` exports, so a cargo-built .so
// can never expose `qt_plugin_instance`, and QPluginLoader rejects it.
// This crate therefore only produces the Rust half.

fn main() {
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=../tdp_plugin.h");
}
