# Builds the Qt-loadable form of the Rust plugin.
#
# Why this exists: `cargo build` alone cannot produce a QPluginLoader-compatible
# .so. rustc emits a linker version script for `cdylib` that makes every symbol
# except the crate's own `#[no_mangle]` exports local, which hides
# `qt_plugin_instance` and therefore makes Qt refuse the plugin. The working
# arrangement is the one below: cargo produces a staticlib, and the C++ driver
# performs the final link so Qt's plugin entry point stays exported.

cmake_minimum_required(VERSION 3.20)
project(tdp_qt_plugin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

set(TDP_POC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# 1. Rust side -> staticlib.
set(CARGO_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/cargo)
set(RUST_STATICLIB
    ${CARGO_TARGET_DIR}/release/libtdp_rust_plugin.a)

add_custom_command(
    OUTPUT ${RUST_STATICLIB}
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${CARGO_TARGET_DIR}
            cargo build --release --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs
        ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
        ${TDP_POC_ROOT}/tdp_plugin.h
    COMMENT "cargo build (staticlib)"
    VERBATIM)
add_custom_target(tdp_rust ALL DEPENDS ${RUST_STATICLIB})

# 2. Qt bridge -> moc + object.
qt_add_library(tdp_qt_plugin SHARED
    src/qt_shim.cpp
    ${TDP_POC_ROOT}/tdp_plugin.h)
add_dependencies(tdp_qt_plugin tdp_rust)
target_include_directories(tdp_qt_plugin PRIVATE ${TDP_POC_ROOT})
target_link_libraries(tdp_qt_plugin PRIVATE Qt6::Core ${RUST_STATICLIB})
set_target_properties(tdp_qt_plugin PROPERTIES
    OUTPUT_NAME "tdp_qt_plugin"
    PREFIX "lib"
    # Keep Qt's plugin entry point visible.
    ENABLE_EXPORTS ON)
