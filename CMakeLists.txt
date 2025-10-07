cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME miniplot)

find_package(OpenSSL REQUIRED)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES src/miniplot_extension.cpp)

# Rust静的ライブラリのパス
set(CHART_VIEWER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/chart_viewer)
set(RUST_LIB ${CHART_VIEWER_DIR}/target/release/libchart_viewer.a)

# Rustライブラリを自動ビルド
add_custom_command(
    OUTPUT ${RUST_LIB}
    COMMAND cargo build --release --lib
    WORKING_DIRECTORY ${CHART_VIEWER_DIR}
    COMMENT "Building Rust chart_viewer library..."
    VERBATIM
)

add_custom_target(rust_chart_viewer ALL
    DEPENDS ${RUST_LIB}
)

# DuckDBエクステンションをビルド
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# 依存関係を追加
add_dependencies(${EXTENSION_NAME} rust_chart_viewer)
add_dependencies(${LOADABLE_EXTENSION_NAME} rust_chart_viewer)

# OpenSSLリンク
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)

# Rust静的ライブラリをリンク（重要！）
target_link_libraries(${EXTENSION_NAME} ${RUST_LIB})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${RUST_LIB})

# macOS用のシステムライブラリ
if(APPLE)
    target_link_libraries(${EXTENSION_NAME}
        "-framework Foundation"
        "-framework Security"
        resolv
    )
    target_link_libraries(${LOADABLE_EXTENSION_NAME}
        "-framework Foundation"
        "-framework Security"
        resolv
    )
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")