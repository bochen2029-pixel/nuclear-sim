# NUCLEAR-SIM — root build. Authored by M0-T2.
#
# Normative elsewhere; do NOT restate here:
#   spec/12-deployment.md §1   toolchain pins, preset names, CUDA archs, vcpkg baseline
#   spec/02-architecture.md §2 repository layout
#   spec/02-architecture.md §4 engineering policies (warnings, deps, determinism)
#
# "Port, don't invent" (07-milestones M0-T2). From C:\Astrophage, which already
# builds C++20 + CUDA 13.1 + sm_89 + MSVC on this machine: architectures set
# before the CUDA language is enabled, `LANGUAGES C` for GLFW/GLAD, a per-target
# strict() function, and static runtimes. Astrophage has neither presets nor
# vcpkg (it uses FetchContent), so the manifest/preset idiom comes from
# C:\backrooms instead — see spec/SESSIONS.md for this session's entry.

cmake_minimum_required(VERSION 3.28)

# Must precede enable_language(CUDA): CMake compiles a probe translation unit
# during compiler detection and bakes these in. Setting it afterwards silently
# produces a binary for the wrong architecture.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "89-real;80-virtual;90-virtual" CACHE STRING "")
endif()

project(nuclear_sim
    VERSION 0.1.0
    DESCRIPTION "Educational simulator of the 1945 Trinity device, from public literature only"
    # C is not used yet. GLFW and GLAD are C libraries (M7); enabling the
    # language later fails at generate time with a misleading
    # "required internal CMake variable not set". Costs nothing now.
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT to match the x64-windows-static triplet in CMakePresets.json; a
# mismatch here surfaces as link errors deep inside third-party code.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CUDA_RUNTIME_LIBRARY Static)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Optional backends
# ---------------------------------------------------------------------------

# 12 §2: defaults ON if nvcc is on PATH at configure time, else OFF.
find_program(NUKESIM_NVCC_EXECUTABLE nvcc)
if(NUKESIM_NVCC_EXECUTABLE)
    set(NUKESIM_CUDA_DEFAULT ON)
else()
    set(NUKESIM_CUDA_DEFAULT OFF)
endif()
option(NUKESIM_WITH_CUDA "Build the CUDA backend (src/gpu)" ${NUKESIM_CUDA_DEFAULT})

if(NUKESIM_WITH_CUDA)
    enable_language(CUDA)
    # 20, not Astrophage's 17: .cu translation units include core headers, and
    # 02 §4 pins the project to C++20. tests/unit/test_toolchain.cu exists to
    # prove nvcc 13.1 + MSVC 14.44 actually accepts that combination — a failure
    # belongs here at M0, not at M4-T1.
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    find_package(CUDAToolkit REQUIRED)
endif()

# 12 §1: one env var name, version-agnostic. Env or -D; the version is not
# encoded in any identifier here. M6 is the first consumer.
if(NOT OPTIX_SDK_ROOT AND DEFINED ENV{OPTIX_SDK_ROOT})
    set(OPTIX_SDK_ROOT "$ENV{OPTIX_SDK_ROOT}" CACHE PATH "OptiX SDK root directory")
endif()
set(NUKESIM_WITH_OPTIX OFF)
if(NUKESIM_WITH_CUDA AND OPTIX_SDK_ROOT AND EXISTS "${OPTIX_SDK_ROOT}/include/optix.h")
    set(NUKESIM_WITH_OPTIX ON)
endif()

# ---------------------------------------------------------------------------
# Per-target policy for first-party targets — 02 §4: ONLY first-party, per
# target, never global. Third-party include directories arrive through IMPORTED
# targets, which CMake already marks SYSTEM, so no third-party header is ever
# compiled under these flags.
#
# Covers warnings and the one link-time consequence of the static-runtime choice
# above. Every first-party target calls this; it is the single place that
# knows what "first-party" means to the build.
# ---------------------------------------------------------------------------
option(NUKESIM_WARNINGS_AS_ERRORS "Treat warnings as errors on first-party targets" ON)

function(nukesim_strict target)
    if(MSVC)
        set(_warn /W4)
        if(NUKESIM_WARNINGS_AS_ERRORS)
            list(APPEND _warn /WX)
        endif()
    else()
        set(_warn -Wall -Wextra)
        if(NUKESIM_WARNINGS_AS_ERRORS)
            list(APPEND _warn -Werror)
        endif()
    endif()

    # C/CXX only. Routing these into nvcc's host pass with -Xcompiler is what
    # 02 §4 asks for, but on MSVC nvcc already supplies /W3, and adding /W4 on
    # top raises D9025 ("overriding '/W3' with '/W4'") which /WX then promotes
    # to an error. Device code is covered by nvcc's own -Werror below instead.
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:${_warn}>)

    if(NOT MSVC)
        list(JOIN _warn "," _warn_csv)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=${_warn_csv}>)
    endif()

    if(NUKESIM_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:-Werror=all-warnings>)
        if(MSVC)
            # nvcc compiles a generated stub (tmpxft_*.cudafe1.stub.c) as part of
            # every .cu, and that stub trips C4211 at /W4. It is NVIDIA's code,
            # not ours, and 02 §4 scopes warnings-as-errors to first-party
            # sources — so suppressing it here is the policy, not an exception to
            # it. Narrow by design: first-party code cannot emit C4211.
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/wd4211>)
        endif()
    endif()

    # NVIDIA ships cudart_static.lib built against the RELEASE static CRT
    # (LIBCMT). A Debug build of ours uses LIBCMTD, so the linker sees both and
    # emits LNK4098. Dropping the release CRT from the default-library search is
    # the documented resolution: cudart_static does not hand allocations across
    # the boundary, so one CRT ends up owning them. Debug only — the Release
    # build already agrees with NVIDIA.
    if(MSVC AND NUKESIM_WITH_CUDA)
        target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:/NODEFAULTLIB:LIBCMT>)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Third-party (vcpkg manifest mode; baseline pinned in vcpkg-configuration.json).
# Resolved here rather than task-by-task so a dropped or renamed port fails at
# M0 configure time instead of halfway through M0-T3.
# ---------------------------------------------------------------------------
find_package(fmt CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

# Everything nscore will link. 02 §4 keeps the dependency set closed: adding to
# this list requires an ADR.
add_library(nukesim_deps INTERFACE)
target_link_libraries(nukesim_deps INTERFACE
    fmt::fmt
    tomlplusplus::tomlplusplus
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    unofficial::sqlite3::sqlite3)

# ---------------------------------------------------------------------------
# Tests
#
# M0-T2's DoD allows "ctest runs (0 tests OK)", but the test presets set
# noTestsAction=error, and a build system that has never linked or run anything
# is not evidence that the toolchain works. test_toolchain is the smallest thing
# that proves the whole chain end to end: every pinned dependency compiles,
# links and executes, and (when CUDA is on) nvcc accepts C++20 and the CUDA
# runtime answers. Real module tests arrive with M0-T3/T4/T5, which may split
# this into tests/unit/CMakeLists.txt.
# ---------------------------------------------------------------------------
enable_testing()
include(Catch)

# ---------------------------------------------------------------------------
# Generated constants (M0-T3-a). spec/appendix/constants.data.toml is the single
# source of truth (01 §8); data/constants.toml and the headers below are its
# output and are committed so a fresh clone compiles without running Python.
# The `constants.generated_current` test is what stops them drifting.
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(NUKESIM_CONSTANTS_STRICT ${CMAKE_SOURCE_DIR}/spec/appendix/constants.data.toml)
set(NUKESIM_GEN_CONSTANTS ${CMAKE_SOURCE_DIR}/tools/gen_constants/gen_constants.py)
set(NUKESIM_CONSTANTS_GENERATED
    ${CMAKE_SOURCE_DIR}/data/constants.toml
    ${CMAKE_SOURCE_DIR}/src/core/constants/constants.h
    ${CMAKE_SOURCE_DIR}/src/core/constants/constants.cuh)

add_custom_command(
    OUTPUT ${NUKESIM_CONSTANTS_GENERATED}
    COMMAND ${Python3_EXECUTABLE} ${NUKESIM_GEN_CONSTANTS}
    DEPENDS ${NUKESIM_CONSTANTS_STRICT} ${NUKESIM_GEN_CONSTANTS}
    COMMENT "Regenerating constants from spec/appendix/constants.data.toml"
    VERBATIM)
add_custom_target(nukesim_constants DEPENDS ${NUKESIM_CONSTANTS_GENERATED})

add_library(nukesim_core_constants INTERFACE)
add_dependencies(nukesim_core_constants nukesim_constants)
target_include_directories(nukesim_core_constants INTERFACE ${CMAKE_SOURCE_DIR}/src)

set(NUKESIM_TOOLCHAIN_TEST_SRC tests/unit/test_toolchain.cpp)
if(NUKESIM_WITH_CUDA)
    list(APPEND NUKESIM_TOOLCHAIN_TEST_SRC tests/unit/test_toolchain_cuda.cu)
endif()

add_executable(test_toolchain ${NUKESIM_TOOLCHAIN_TEST_SRC})
target_link_libraries(test_toolchain PRIVATE nukesim_deps Catch2::Catch2WithMain)
if(NUKESIM_WITH_CUDA)
    target_link_libraries(test_toolchain PRIVATE CUDA::cudart_static)
    target_compile_definitions(test_toolchain PRIVATE NUKESIM_WITH_CUDA=1)
endif()
nukesim_strict(test_toolchain)

# TEST_PREFIX is a convention, not decoration. catch_discover_tests registers one
# ctest entry per Catch2 test case, named after the case's own text — so the
# per-task VERIFY probes the spec already commits to (`ctest -R constants` in
# M0-T3, `-R rng` in M0-T4, `-R loaders` in M0-T5) would match nothing without a
# stable module prefix. Every future test executable follows this pattern.
catch_discover_tests(test_toolchain TEST_PREFIX "toolchain.")

add_executable(test_constants tests/unit/test_constants.cpp)
target_link_libraries(test_constants PRIVATE nukesim_core_constants Catch2::Catch2WithMain)
nukesim_strict(test_constants)
catch_discover_tests(test_constants TEST_PREFIX "constants.")

# The two Python checks run under the same `ctest -R constants` probe the spec
# commits to for M0-T3 — a stale generated header and a spec/code divergence are
# both constants failures, whatever language notices them.
add_test(NAME constants.roundtrip_bijection
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify/constants_roundtrip.py)
add_test(NAME constants.generated_current
         COMMAND ${Python3_EXECUTABLE} ${NUKESIM_GEN_CONSTANTS} --check)
add_test(NAME constants.generator_rejects_missing_provenance
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/verify/constants_negative.py)
# ---------------------------------------------------------------------------
# nscore loaders (M0-T5). GUI-free and deterministic by construction (D7).
# ---------------------------------------------------------------------------
add_library(nukesim_core STATIC
    src/core/geometry/geometry.cpp
    src/core/hash/sha256.cpp
    src/core/hash/crc32.cpp
    src/core/checkpoint/checkpoint.cpp
    src/core/store/store.cpp
    src/core/xs/xs.cpp
    src/core/material/material.cpp
    src/core/scenario/scenario.cpp
    src/core/scenario/canonical_hash.cpp
    src/ref/ref_transport.cpp
    src/ref/ref_generation.cpp
    src/physics/eigen/eigen.cpp
    src/physics/kinetics/kinetics.cpp
    src/physics/hydro/tier1.cpp
    src/physics/hydro/tier2.cpp
    src/physics/tally/tally.cpp
    src/physics/couple/couple.cpp
    src/api/run_provenance.cpp
    src/api/studio.cpp)
target_link_libraries(nukesim_core PUBLIC nukesim_core_constants nukesim_deps)
target_include_directories(nukesim_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
nukesim_strict(nukesim_core)

add_executable(test_loaders tests/unit/test_loaders.cpp)
target_link_libraries(test_loaders PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_loaders PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
# The tests read the canonical examples out of the spec markdown itself, so they
# need to find the source tree at run time.
target_compile_definitions(test_loaders PRIVATE
    NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_loaders)
catch_discover_tests(test_loaders TEST_PREFIX "loaders.")

add_executable(test_geometry tests/unit/test_geometry.cpp)
target_link_libraries(test_geometry PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_geometry PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_geometry PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_geometry)
catch_discover_tests(test_geometry TEST_PREFIX "geometry.")

add_executable(test_benchmarks tests/unit/test_benchmarks.cpp)
target_link_libraries(test_benchmarks PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_benchmarks PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_benchmarks)
catch_discover_tests(test_benchmarks TEST_PREFIX "benchmarks.")

add_executable(test_eigen tests/unit/test_eigen.cpp)
target_link_libraries(test_eigen PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_eigen PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_eigen PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_eigen)
catch_discover_tests(test_eigen TEST_PREFIX "eigen.")

add_executable(test_kinetics tests/unit/test_kinetics.cpp)
target_link_libraries(test_kinetics PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_kinetics PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_kinetics PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_kinetics)
catch_discover_tests(test_kinetics TEST_PREFIX "kinetics.")

add_executable(test_tier1 tests/unit/test_tier1.cpp)
target_link_libraries(test_tier1 PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_tier1 PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_tier1 PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_tier1)
catch_discover_tests(test_tier1 TEST_PREFIX "tier1.")

add_executable(test_tier2 tests/unit/test_tier2.cpp)
target_link_libraries(test_tier2 PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_tier2 PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_tier2 PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_tier2)
catch_discover_tests(test_tier2 TEST_PREFIX "tier2.")

add_executable(test_tally tests/unit/test_tally.cpp)
target_link_libraries(test_tally PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_tally PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_tally PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_tally)
catch_discover_tests(test_tally TEST_PREFIX "tally.")

add_executable(test_couple tests/unit/test_couple.cpp)
target_link_libraries(test_couple PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_couple PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_couple PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_couple)
catch_discover_tests(test_couple TEST_PREFIX "couple.")

# nscore fusion API (M3-T3-f onward): run.json provenance + the evaluate /
# generate_run surface (03 §6, 06 §frontends). Part of nscore, not a frontend
# (D7) — it composes physics/couple; frontends (src/app/) call it.
add_executable(test_api tests/unit/test_api.cpp)
target_link_libraries(test_api PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_api PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_api PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_api)
catch_discover_tests(test_api TEST_PREFIX "api.")

# checkpoint.bin v2 container + CRC32 (M5-T1-a): the resume container (03 §8, D9).
add_executable(test_checkpoint tests/unit/test_checkpoint.cpp)
target_link_libraries(test_checkpoint PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_checkpoint PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
nukesim_strict(test_checkpoint)
catch_discover_tests(test_checkpoint TEST_PREFIX "checkpoint.")

# sweep.db results store (M5-T2): the 03 §7 batch results DB (runs + cursor) +
# unit_id idempotency (rerun = no-op, D6/D8). Core infra (src/core/store), not a
# frontend (D7); sqlite3 arrives through nukesim_core -> nukesim_deps.
add_executable(test_store tests/unit/test_store.cpp)
target_link_libraries(test_store PRIVATE nukesim_core Catch2::Catch2WithMain)
nukesim_strict(test_store)
catch_discover_tests(test_store TEST_PREFIX "store.")

# ---------------------------------------------------------------------------
# nukefarm (M5-T3): the batch/cloud sweep driver (06 §2, D8). A FRONTEND (D7) —
# no physics; it composes nscore outputs (scores a ns::physics::TallyResult).
# M5-T3-a: the sweep.toml loader + the axis_class/ScoreKind enforcement + the
# Sampler interface + grid/lhs/random. The FS work-queue + store-backed run loop
# + the submit/worker/status/resume CLI are M5-T3-b. Built as a static lib so the
# CLI (M5-T3-b) and the tests link the same code; toml++/constants/rng/tally all
# arrive through nukesim_core (PUBLIC).
# ---------------------------------------------------------------------------
add_library(nukesim_nukefarm STATIC
    src/app/nukefarm/sweep.cpp
    src/app/nukefarm/samplers/builtin.cpp
    src/app/nukefarm/runner.cpp
    src/app/nukefarm/queue.cpp
    src/app/nukefarm/worker.cpp
    src/app/nukefarm/cli.cpp)
target_link_libraries(nukesim_nukefarm PUBLIC nukesim_core)
target_include_directories(nukesim_nukefarm PUBLIC ${CMAKE_SOURCE_DIR}/src)
nukesim_strict(nukesim_nukefarm)

add_executable(test_nukefarm tests/unit/test_nukefarm.cpp)
target_link_libraries(test_nukefarm PRIVATE nukesim_nukefarm Catch2::Catch2WithMain)
nukesim_strict(test_nukefarm)
catch_discover_tests(test_nukefarm TEST_PREFIX "nukefarm.")

# The nukefarm CLI (M5-T3-e): the first src/app executable — a thin CLI11 dispatch
# over the tested nukesim_nukefarm handlers. A non-test binary (like gpu_perf), so
# the ctest count is unchanged.
add_executable(nukefarm src/app/nukefarm/main.cpp)
target_link_libraries(nukefarm PRIVATE nukesim_nukefarm)
nukesim_strict(nukefarm)

# The fusion-binding bridge: the demon-core studio API (evaluate / generate_run) as
# JSON in/out — the mechanism-independent DATA surface the visualizer's binding
# calls (a dev-server shell / WASM / native addon all wrap this). A non-test binary
# over nukesim_core's src/api; ctest count unchanged.
add_executable(studio_bridge src/app/studio_bridge/main.cpp)
target_link_libraries(studio_bridge PRIVATE nukesim_core)
nukesim_strict(studio_bridge)

add_executable(test_ref tests/unit/test_ref.cpp)
target_link_libraries(test_ref PRIVATE nukesim_core Catch2::Catch2WithMain)
target_include_directories(test_ref PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_compile_definitions(test_ref PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nukesim_strict(test_ref)
catch_discover_tests(test_ref TEST_PREFIX "ref.")

add_executable(test_rng tests/unit/test_rng.cpp)
target_link_libraries(test_rng PRIVATE nukesim_core_constants Catch2::Catch2WithMain)
target_include_directories(test_rng PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
nukesim_strict(test_rng)
catch_discover_tests(test_rng TEST_PREFIX "rng.")

# The frozen vector is only evidence if an independent implementation still
# agrees with it — this re-runs the Python Philox, which must reproduce the
# three published Random123 vectors before it will certify anything (04 §2).
add_test(NAME rng.kat_independent_oracle
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/verify/rng_kat_gen.py --check)

# 11 §5's first-principles oracle. --strict means a numerical discrepancy fails
# the build, not just a stale file: the whole point is that the spec's own
# numbers get checked by arithmetic, and a finding nobody sees is worth nothing.
add_test(NAME oracle.verification_current
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/verify/oracle.py --check --strict)

# 01 §8.3 / 11 §4: no compute-path source may reference a crosscheck constant.
# The ns::consts::crosscheck namespace exists so this grep is possible (04 §1).
add_test(NAME constants.crosscheck_misuse
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/verify/crosscheck_misuse.py)

# ---------------------------------------------------------------------------
# CUDA backend foundation (M4-T1): SoA buffers + device Philox + the three
# deterministic primitives of 05 §6 (slots / streams / reductions).
#
# CUDA-guarded on purpose. The hosted CI runners carry no CUDA toolkit, so
# NUKESIM_WITH_CUDA auto-detects OFF (12 §2) and this whole block is skipped —
# CI stays at its committed CPU test count and green. The dev machine has nvcc,
# so it builds `nukesim_gpu` and runs the `gpu.` probe. The device Philox is the
# SAME headers as ref/ (core/rng), made device-callable by the NUKESIM_HD macro
# (SYNC-M4); no second implementation exists to drift from the frozen KATs.
# ---------------------------------------------------------------------------
if(NUKESIM_WITH_CUDA)
    add_library(nukesim_gpu STATIC
        src/gpu/gpu_backend.cu
        src/gpu/device_data.cu
        src/gpu/transport.cu
        src/gpu/eigen.cu)
    target_include_directories(nukesim_gpu PUBLIC ${CMAKE_SOURCE_DIR}/src)
    # nukesim_core (host) supplies the CPU LayerStack / materials / xs that the
    # M4-T2-a device builders mirror and the parity tests compare against.
    target_link_libraries(nukesim_gpu PUBLIC nukesim_core CUDA::cudart_static)
    # The device Philox reuses core/rng, whose Counter/Key are std::array. MSVC's
    # std::array::operator[] is constexpr __host__ (not __host__ __device__), so
    # calling it from device code needs --expt-relaxed-constexpr — the documented
    # nvcc mechanism for reusing constexpr host functions on the device. It
    # changes no results; it only lets the ONE RNG source serve both backends
    # (04 §2: the device RNG must be the same bijection as ref/, not a copy). The
    # portability KATs (gpu.*) are what prove the reuse is bit-correct.
    target_compile_options(nukesim_gpu PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    nukesim_strict(nukesim_gpu)

    add_executable(test_gpu tests/unit/test_gpu.cpp)
    target_link_libraries(test_gpu PRIVATE nukesim_gpu Catch2::Catch2WithMain)
    target_include_directories(test_gpu PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
    # spec_examples.h (used to build toy loader inputs) references it even in the
    # inline helpers this TU does not call, so it must be defined to compile.
    target_compile_definitions(test_gpu PRIVATE NUKESIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    nukesim_strict(test_gpu)
    catch_discover_tests(test_gpu TEST_PREFIX "gpu.")

    # M4-T4 perf harness: times the GPU eigen/transport across a batch sweep and
    # appends to artifacts/perf_history.jsonl (11 §1). Deliberately NOT a ctest —
    # it is a measurement tool, not a gate, so the anchored `gpu.` count is
    # unchanged. Dev-only (inside NUKESIM_WITH_CUDA); CPU CI never builds it.
    add_executable(gpu_perf tools/perf/gpu_perf.cpp)
    target_link_libraries(gpu_perf PRIVATE nukesim_gpu)
    target_include_directories(gpu_perf PRIVATE ${CMAKE_SOURCE_DIR}/src)
    nukesim_strict(gpu_perf)
endif()

# ---------------------------------------------------------------------------
message(STATUS "nuclear-sim ${PROJECT_VERSION}")
message(STATUS "  C++            ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA           ${NUKESIM_WITH_CUDA}  ${CMAKE_CUDA_COMPILER_VERSION}  arch ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "  OptiX          ${NUKESIM_WITH_OPTIX}  (M6+; set OPTIX_SDK_ROOT to enable)")
message(STATUS "  vcpkg triplet  ${VCPKG_TARGET_TRIPLET}")
message(STATUS "  warn-as-error  ${NUKESIM_WARNINGS_AS_ERRORS}")
