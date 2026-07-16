# libspartacus — DeepCool SPARTACUS 360 / 420 C++17 client library
#
# This CMake build is an alternative to the Makefile (which is the primary, tested build on
# machines without CMake). Typical use:
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ctest --test-dir build            # runs the golden-vector tests
#   cmake --install build --prefix /usr/local
#
# Consumers then:
#   find_package(spartacus REQUIRED)
#   target_link_libraries(myapp PRIVATE spartacus::spartacus)
# or, without CMake, `pkg-config --cflags --libs --static spartacus`.

cmake_minimum_required(VERSION 3.16)

# The library version lives in include/spartacus/Version.hpp (single source of truth).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/spartacus/Version.hpp" _spartacus_version_hpp)
foreach(part MAJOR MINOR PATCH)
    if(NOT _spartacus_version_hpp MATCHES "#define SPARTACUS_VERSION_${part} ([0-9]+)")
        message(FATAL_ERROR "SPARTACUS_VERSION_${part} not found in Version.hpp")
    endif()
    set(SPARTACUS_VERSION_${part} ${CMAKE_MATCH_1})
endforeach()

project(spartacus
    VERSION ${SPARTACUS_VERSION_MAJOR}.${SPARTACUS_VERSION_MINOR}.${SPARTACUS_VERSION_PATCH}
    LANGUAGES CXX)

option(SPARTACUS_WITH_IMAGE     "Build image helpers (bundled baseline-JPEG encoder)" ON)
option(SPARTACUS_BUILD_EXAMPLES "Build the example programs" ON)
option(SPARTACUS_BUILD_TESTS    "Build the golden-vector tests" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# When built as the top-level project, export compile_commands.json for clangd/IDEs.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# One warning set for every target in this project.
set(SPARTACUS_WARNINGS
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# ── libusb-1.0 (required transport) ──────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(SPARTACUS_LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# ── library target ───────────────────────────────────────────────────────────────────
set(SPARTACUS_SOURCES
    src/Protocol.cpp
    src/Display.cpp
    src/Linker.cpp)
if(SPARTACUS_WITH_IMAGE)
    list(APPEND SPARTACUS_SOURCES src/Image.cpp)
endif()

add_library(spartacus STATIC ${SPARTACUS_SOURCES})
add_library(spartacus::spartacus ALIAS spartacus)

target_compile_features(spartacus PUBLIC cxx_std_17)
set_target_properties(spartacus PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(spartacus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src           # private UsbDevice.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)  # bundled spartacus_jpeg.h

# libusb is linked PUBLIC so consumers of the static library inherit the link dependency.
target_link_libraries(spartacus PUBLIC PkgConfig::SPARTACUS_LIBUSB)

if(SPARTACUS_WITH_IMAGE)
    target_compile_definitions(spartacus PRIVATE SPARTACUS_WITH_IMAGE)
endif()

target_compile_options(spartacus PRIVATE ${SPARTACUS_WARNINGS})

# ── golden-vector tests (pure builders only; no libusb, no hardware) ─────────────────
if(SPARTACUS_BUILD_TESTS)
    enable_testing()
    add_executable(spartacus_tests tests/test_vectors.cpp src/Protocol.cpp)
    target_include_directories(spartacus_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(spartacus_tests PRIVATE cxx_std_17)
    set_target_properties(spartacus_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(spartacus_tests PRIVATE ${SPARTACUS_WARNINGS})
    add_test(NAME golden_vectors COMMAND spartacus_tests)
endif()

# ── examples ─────────────────────────────────────────────────────────────────────────
if(SPARTACUS_BUILD_EXAMPLES)
    set(SPARTACUS_EXAMPLES
        brightness orientation native_telemetry
        linker_read_speeds linker_set_speeds linker_argb linker_motherboard_sync)
    if(SPARTACUS_WITH_IMAGE)
        list(APPEND SPARTACUS_EXAMPLES image clear)
    endif()
    foreach(ex ${SPARTACUS_EXAMPLES})
        add_executable(${ex} examples/${ex}.cpp)
        target_link_libraries(${ex} PRIVATE spartacus::spartacus)
        target_compile_options(${ex} PRIVATE ${SPARTACUS_WARNINGS})
        set_target_properties(${ex} PROPERTIES
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
    endforeach()
endif()

# ── install: library, headers, find_package config, pkg-config ───────────────────────
install(TARGETS spartacus
    EXPORT spartacusTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/spartacus DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(SPARTACUS_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/spartacus)

install(EXPORT spartacusTargets
    FILE spartacusTargets.cmake
    NAMESPACE spartacus::
    DESTINATION ${SPARTACUS_CMAKE_DIR})

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/spartacusConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/spartacusConfig.cmake
    INSTALL_DESTINATION ${SPARTACUS_CMAKE_DIR})

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/spartacusConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/spartacusConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/spartacusConfigVersion.cmake
    DESTINATION ${SPARTACUS_CMAKE_DIR})

# pkg-config metadata from the same template the Makefile uses. Generated at install
# time (not configure time) so a `cmake --install … --prefix X` override is honoured.
install(CODE "
    set(PREFIX \"\${CMAKE_INSTALL_PREFIX}\")
    set(LIBDIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\")
    set(INCLUDEDIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}\")
    set(VERSION \"${PROJECT_VERSION}\")
    configure_file(
        \"${CMAKE_CURRENT_SOURCE_DIR}/packaging/spartacus.pc.in\"
        \"${CMAKE_CURRENT_BINARY_DIR}/spartacus.pc\"
        @ONLY)
")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/spartacus.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
