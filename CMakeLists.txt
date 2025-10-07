cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME miniplot)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be added in ./vcpkg.json and then
# used in cmake with find_package. Feel free to remove or replace with other dependencies.
# Note that it should also be removed from vcpkg.json to prevent needlessly installing it..
find_package(OpenSSL REQUIRED)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES src/miniplot_extension.cpp)

# ===== Rust静的ライブラリの設定 =====
set(CHART_VIEWER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/chart_viewer)
set(RUST_LIB ${CHART_VIEWER_DIR}/target/release/libchart_viewer.a)

# Rustライブラリを自動ビルド
add_custom_command(
    OUTPUT ${RUST_LIB}
    COMMAND cargo build --release --lib
    WORKING_DIRECTORY ${CHART_VIEWER_DIR}
    COMMENT "Building Rust chart_viewer library from ${CHART_VIEWER_DIR}..."
    VERBATIM
)

# カスタムターゲットを作成
add_custom_target(rust_chart_viewer ALL
    DEPENDS ${RUST_LIB}
)

# DuckDBエクステンションをビルド
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Rustビルドに依存
add_dependencies(${EXTENSION_NAME} rust_chart_viewer)
add_dependencies(${LOADABLE_EXTENSION_NAME} rust_chart_viewer)

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)

# Rust静的ライブラリをリンク
target_link_libraries(${EXTENSION_NAME} ${RUST_LIB})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${RUST_LIB})

# プラットフォーム別の依存関係
if(APPLE)
    # macOS: システムライブラリを最初にリンク（重要！）
    target_link_libraries(${LOADABLE_EXTENSION_NAME}
        "-framework System"
        "-framework Foundation"
        "-framework AppKit"
        "-framework Carbon"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework IOKit"
        "-framework Metal"
        "-framework QuartzCore"
        resolv
        c++
    )
    
    target_link_libraries(${EXTENSION_NAME}
        "-framework System"
        "-framework Foundation"
        "-framework AppKit"
        "-framework Carbon"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework IOKit"
        "-framework Metal"
        "-framework QuartzCore"
        resolv
        c++
    )
    
elseif(UNIX AND NOT APPLE)
    # Linux
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    
    target_include_directories(${EXTENSION_NAME} PRIVATE ${GTK3_INCLUDE_DIRS})
    target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE ${GTK3_INCLUDE_DIRS})
    
    target_link_libraries(${EXTENSION_NAME}
        ${GTK3_LIBRARIES}
        pthread
        dl
        m
    )
    target_link_libraries(${LOADABLE_EXTENSION_NAME}
        ${GTK3_LIBRARIES}
        pthread
        dl
        m
    )
elseif(WIN32)
    target_link_libraries(${EXTENSION_NAME}
        userenv
        ws2_32
        bcrypt
        ntdll
    )
    target_link_libraries(${LOADABLE_EXTENSION_NAME}
        userenv
        ws2_32
        bcrypt
        ntdll
    )
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")