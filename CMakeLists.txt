cmake_minimum_required(VERSION 3.14)
project(libwikimark VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Always build position-independent code so the static libraries
# can be linked into shared objects (e.g. the wikimark-python
# CFFI extension). Setting this before the vendored subdirectories
# propagates it to cmark-gfm and libyaml as well.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Options ---
option(WIKIMARK_TESTS "Build tests" ON)
option(WIKIMARK_BENCH "Build benchmarks" OFF)

# --- Build cmark-gfm as a subdirectory ---
set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cmark-gfm)

# --- Build libyaml as a subdirectory ---
set(INSTALL_CMAKE_DIR OFF CACHE BOOL "" FORCE)
set(INSTALL_INCLUDE_DIR OFF CACHE BOOL "" FORCE)
set(INSTALL_LIB_DIR OFF CACHE BOOL "" FORCE)
set(YAML_STATIC_LIB_NAME yaml_static)
add_subdirectory(third_party/libyaml)

# --- libwikimark static library ---
set(WIKIMARK_SOURCES
    src/wikimark.c
    src/wikilink.c
    src/normalize.c
    src/scanner.c
    src/frontmatter.c
    src/variable.c
    src/attributes.c
    src/annotation.c
    src/callout.c
    src/redirect.c
    src/template.c
    src/expand.c
    src/spans.c
)

add_library(wikimark STATIC ${WIKIMARK_SOURCES})

target_include_directories(wikimark
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # cmark-gfm headers (public + internal)
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
        ${CMAKE_BINARY_DIR}/third_party/cmark-gfm/src
        # libyaml headers
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyaml/include
)

target_link_libraries(wikimark
    PUBLIC
        libcmark-gfm-extensions_static
        libcmark-gfm_static
        yaml
)

# --- CLI tool (with test engine) ---
add_executable(wikimark-cli tools/wikimark.c tools/test_engine.c)
set_target_properties(wikimark-cli PROPERTIES OUTPUT_NAME wikimark)
target_link_libraries(wikimark-cli PRIVATE wikimark)
target_include_directories(wikimark-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
    ${CMAKE_BINARY_DIR}/third_party/cmark-gfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyaml/include
)

# --- Benchmark harness ---
if(WIKIMARK_BENCH)
    add_executable(bench_wikimark bench/bench_main.c tools/test_engine.c)
    target_link_libraries(bench_wikimark PRIVATE wikimark)
    target_include_directories(bench_wikimark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
        ${CMAKE_BINARY_DIR}/third_party/cmark-gfm/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyaml/include
    )

    # Convenience target: `make bench` runs the harness over each input.
    add_custom_target(bench
        COMMAND ${CMAKE_COMMAND} -E env
            WM_BENCH_TEMPLATE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test_data/templates
            WM_BENCH_PAGES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test_data/pages
            $<TARGET_FILE:bench_wikimark> ${CMAKE_CURRENT_SOURCE_DIR}/bench/inputs/plain.md
        COMMAND ${CMAKE_COMMAND} -E env
            WM_BENCH_TEMPLATE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test_data/templates
            WM_BENCH_PAGES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test_data/pages
            $<TARGET_FILE:bench_wikimark> ${CMAKE_CURRENT_SOURCE_DIR}/bench/inputs/wiki-heavy.wm
        COMMAND ${CMAKE_COMMAND} -E env
            WM_BENCH_TEMPLATE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test_data/templates
            WM_BENCH_PAGES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test_data/pages
            $<TARGET_FILE:bench_wikimark> ${CMAKE_CURRENT_SOURCE_DIR}/bench/inputs/kitchen-sink.wm
        DEPENDS bench_wikimark
        COMMENT "Running libwikimark benchmark suite"
    )
endif()

# --- Fuzzing harness ---
# Build with: cmake -DWIKIMARK_FUZZ=ON -DCMAKE_C_FLAGS="-fsanitize=fuzzer,address -g" ..
# Run:        ./fuzz_render tests/fuzz/corpus/
option(WIKIMARK_FUZZ "Build fuzzing targets" OFF)
if(WIKIMARK_FUZZ)
    add_executable(fuzz_render tests/fuzz/fuzz_render.c)
    target_link_libraries(fuzz_render PRIVATE wikimark)
    target_include_directories(fuzz_render PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
        ${CMAKE_BINARY_DIR}/third_party/cmark-gfm/src
    )
endif()

# --- Tests ---
#
# The runner shells out to a processor command. We pass the CLI with
# --template-dir / --pages-dir flags pointing at the fixture data,
# so tests that use template transclusion or page embeds work. The
# baseline exclusions file documents the 85 tests that diverge
# intentionally (cmark-gfm baseline differences + WikiMark-specific
# deviations for relative links and standalone images).
if(WIKIMARK_TESTS)
    enable_testing()
    set(WM_CLI_CMD "$<TARGET_FILE:wikimark-cli> --template-dir ${CMAKE_CURRENT_SOURCE_DIR}/test_data/templates --pages-dir ${CMAKE_CURRENT_SOURCE_DIR}/test_data/pages")
    set(WM_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/../wikimark/tests/run_tests.py)
    set(WM_EXCLUSIONS cmark-gfm-baseline-exclusions.json)

    add_test(
        NAME spec-tests
        COMMAND python3 ${WM_RUNNER}
            ${WM_CLI_CMD}
            --suite wikimark-spec,wikimark-extra
            --exclude-file ${WM_EXCLUSIONS}
            --normalize
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    add_test(
        NAME commonmark-tests
        COMMAND python3 ${WM_RUNNER}
            ${WM_CLI_CMD}
            --suite commonmark
            --exclude-file ${WM_EXCLUSIONS}
            --normalize
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    add_test(
        NAME gfm-tests
        COMMAND python3 ${WM_RUNNER}
            ${WM_CLI_CMD}
            --suite gfm-spec,gfm-extensions
            --exclude-file ${WM_EXCLUSIONS}
            --normalize
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()
