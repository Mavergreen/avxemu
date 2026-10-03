cmake_minimum_required(VERSION 3.25)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/UPSTREAM_VERSION" AVXEMU_UPSTREAM LIMIT_COUNT 1)
set(AVXEMU_VERSION "${AVXEMU_UPSTREAM}")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AVXEMU_VERSION LIMIT_COUNT 1)
endif()

project(AVXEmu LANGUAGES C ASM)

find_package(MavericksShipyard REQUIRED)
include(Mavericks)
include(MavericksMode)
include(MavericksCompatGuard)
mavericks_build_mode(MAVERICKS_MODE)
mavericks_require_expected_mode("${MAVERICKS_MODE}")
message(STATUS "AVXEmu ${AVXEMU_VERSION}: build mode ${MAVERICKS_MODE}")

option(AVXEMU_BUILD_UPDATER "Build the Sparkle updater (needs avxemu in the installed shipyard's product registry)" OFF)

# The emulator's runtime is SSE4.2-only, so it runs on the CPUs it exists for; the flags and
# the source order are build.sh's, which is what lets this build reproduce the CDN's objects.
set(AVXEMU_CORE_FLAGS -O2 -Wall -std=c11 -msse4.2 -mno-avx -mno-fma)
set(AVXEMU_HOST_FLAGS -O2 -std=c11 -mavx2 -mfma -mbmi -mbmi2 -mlzcnt -mf16c)

function(avxemu_objects name)
  add_library(${name} OBJECT ${ARGN})
  target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${AVXEMU_CORE_FLAGS}>")
  target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
endfunction()

avxemu_objects(avxemu_core
  src/exec.c src/exec_bmi.c src/softfma.c src/names.c src/decode.c
  src/lde.c src/patch_mem.c src/tramp.c src/reloc.c src/handler.c)
avxemu_objects(avxemu_asm src/selftest.s src/selftest.c src/tramp.s)

add_library(avxemu SHARED $<TARGET_OBJECTS:avxemu_core> $<TARGET_OBJECTS:avxemu_asm>)
set_target_properties(avxemu PROPERTIES
  LINKER_LANGUAGE C
  MACOSX_RPATH ON
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
mavericks_assert_binary_compatible(avxemu)

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/objects/core.txt"
  CONTENT "$<JOIN:$<TARGET_OBJECTS:avxemu_core>,\n>\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/objects/asm.txt"
  CONTENT "$<JOIN:$<TARGET_OBJECTS:avxemu_asm>,\n>\n")

if(AVXEMU_BUILD_UPDATER)
  include(MavericksSparkle)
  mavericks_add_updater_app(
    PRODUCT avxemu
    PRODUCT_NAME "AVXEmu"
    ALLOW_GENERIC
    VERSION "${AVXEMU_VERSION}"
    CONFIRM_TITLE "AVXEmu updated"
    CONFIRM_BODY "AVXEmu was updated. Programs that load it use the new version the next time they start.")
endif()

enable_testing()
# shipyard's run-repo-tests.sh drives ctest only when THIS file declares an add_test, so the
# first test lives here rather than in test/CMakeLists.txt: without it CI would run nothing.
add_test(NAME vex-leak-core
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test/vex-leak.sh"
          "${CMAKE_BINARY_DIR}/objects/core.txt" "${CMAKE_BINARY_DIR}/objects/asm.txt")
set_tests_properties(vex-leak-core PROPERTIES LABELS hermetic SKIP_RETURN_CODE 77)
add_subdirectory(test)
