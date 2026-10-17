cmake_minimum_required(VERSION 3.5...3.29)

set(TARGET_NAME miniplot)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# OpenSSLを検索
find_package(OpenSSL REQUIRED)

set(EXTENSION_SOURCES src/miniplot_extension.cpp)

include_directories(src/include ${OPENSSL_INCLUDE_DIR})

# Use the JSON parser already bundled with DuckDB. Wasm has a separate final
# side-module link, so it also needs the archive explicitly.
if(EMSCRIPTEN)
    set(DUCKDB_EXTENSION_MINIPLOT_LINKED_LIBS "$<TARGET_FILE:duckdb_yyjson>")
endif()

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# OpenSSLをリンク
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto duckdb_yyjson)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto duckdb_yyjson)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
