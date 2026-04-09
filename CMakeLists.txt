cmake_minimum_required(VERSION 3.14)
project(libwikimark VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Options ---
option(WIKIMARK_TESTS "Build tests" ON)
option(WIKIMARK_BENCH "Build benchmarks" OFF)

# --- Build cmark-gfm as a subdirectory ---
set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cmark-gfm)

# --- libwikimark shared library ---
set(WIKIMARK_SOURCES
    src/wikimark.c
    src/wikilink.c
    src/normalize.c
    src/scanner.c
)

add_library(wikimark STATIC ${WIKIMARK_SOURCES})

target_include_directories(wikimark
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # cmark-gfm headers (public + internal for parser.h, render.h, etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
        ${CMAKE_BINARY_DIR}/third_party/cmark-gfm/src
)

target_link_libraries(wikimark
    PUBLIC
        libcmark-gfm-extensions_static
        libcmark-gfm_static
)

# --- CLI tool ---
add_executable(wikimark-cli tools/wikimark.c)
set_target_properties(wikimark-cli PROPERTIES OUTPUT_NAME wikimark)
target_link_libraries(wikimark-cli PRIVATE wikimark)
target_include_directories(wikimark-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
    ${CMAKE_BINARY_DIR}/third_party/cmark-gfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
)

# --- Tests ---
if(WIKIMARK_TESTS)
    enable_testing()
    add_test(
        NAME spec-tests
        COMMAND ${CMAKE_COMMAND} -E env
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/../wikimark/tests/run_tests.py
            $<TARGET_FILE:wikimark-cli>
            --suite wikimark-spec,wikimark-extra
            --normalize
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    add_test(
        NAME commonmark-tests
        COMMAND ${CMAKE_COMMAND} -E env
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/../wikimark/tests/run_tests.py
            $<TARGET_FILE:wikimark-cli>
            --suite commonmark
            --normalize
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    add_test(
        NAME gfm-tests
        COMMAND ${CMAKE_COMMAND} -E env
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/../wikimark/tests/run_tests.py
            $<TARGET_FILE:wikimark-cli>
            --suite gfm-spec,gfm-extensions
            --normalize
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()
