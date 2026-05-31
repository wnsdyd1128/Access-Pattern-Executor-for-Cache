cmake_minimum_required(VERSION 3.20)
project(APEX-Cache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 의존성 ────────────────────────────────────────────────────
find_package(yaml-cpp REQUIRED)

find_package(GTest REQUIRED)
include(GoogleTest)

# ── 인클루드 경로 ─────────────────────────────────────────────
set(APEX_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
)

# ── 라이브러리 소스 (main.cpp 제외) ───────────────────────────
file(GLOB_RECURSE APEX_LIB_SOURCES
    src/ap/*.cpp
    src/memory/*.cpp
    src/cache/*.cpp
    src/analysis/*.cpp
    src/report/*.cpp
)

if(APEX_LIB_SOURCES)
    add_library(apex_lib STATIC ${APEX_LIB_SOURCES})
    target_include_directories(apex_lib PUBLIC ${APEX_INCLUDE_DIRS})
    target_link_libraries(apex_lib PUBLIC yaml-cpp)
endif()

# ── 실행 파일 ────────────────────────────────────────────────
add_executable(apex-cache src/main.cpp)
target_include_directories(apex-cache PRIVATE ${APEX_INCLUDE_DIRS})
if(TARGET apex_lib)
    target_link_libraries(apex-cache PRIVATE apex_lib)
endif()
target_link_libraries(apex-cache PRIVATE yaml-cpp)

# ── 테스트 ───────────────────────────────────────────────────
enable_testing()

function(apex_add_test target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE ${APEX_INCLUDE_DIRS})
    if(TARGET apex_lib)
        target_link_libraries(${target} PRIVATE apex_lib)
    endif()
    target_link_libraries(${target} PRIVATE yaml-cpp GTest::GTest GTest::Main)
    gtest_discover_tests(${target})
endfunction()

# Phase 2 — AP Layer
file(GLOB AP_TESTS tests/ap/*.cpp)
foreach(src ${AP_TESTS})
    get_filename_component(name ${src} NAME_WE)
    apex_add_test(${name} ${src})
endforeach()

# Phase 3 — Memory Layer
file(GLOB MEM_TESTS tests/memory/*.cpp)
foreach(src ${MEM_TESTS})
    get_filename_component(name ${src} NAME_WE)
    apex_add_test(${name} ${src})
endforeach()

# Phase 4 — Cache Layer
file(GLOB CACHE_TESTS tests/cache/*.cpp)
foreach(src ${CACHE_TESTS})
    get_filename_component(name ${src} NAME_WE)
    apex_add_test(${name} ${src})
endforeach()

# Phase 5 — Analysis Layer
file(GLOB ANALYSIS_TESTS tests/analysis/*.cpp)
foreach(src ${ANALYSIS_TESTS})
    get_filename_component(name ${src} NAME_WE)
    apex_add_test(${name} ${src})
endforeach()

# Phase 7 — Integration
file(GLOB INTEGRATION_TESTS tests/integration/*.cpp)
foreach(src ${INTEGRATION_TESTS})
    get_filename_component(name ${src} NAME_WE)
    apex_add_test(${name} ${src})
endforeach()
