# avxemu's tests. Each later task appends; the _replay_objs block added in Task 7, and then the
# AVXEMU_NOCACHE block, stay last.

avxemu_objects(t_cpu cpu.c)

add_executable(cpuprobe cpuprobe.c $<TARGET_OBJECTS:t_cpu>)
target_compile_options(cpuprobe PRIVATE "$<$<COMPILE_LANGUAGE:C>:${AVXEMU_CORE_FLAGS}>")
set(AVXEMU_RUN sh "${CMAKE_CURRENT_SOURCE_DIR}/run.sh" --cpuprobe $<TARGET_FILE:cpuprobe>)

function(avxemu_test_exe name)
  cmake_parse_arguments(A "" "" "FLAGS;SOURCES;LINK" ${ARGN})
  if(NOT A_FLAGS)
    set(A_FLAGS ${AVXEMU_CORE_FLAGS})
  endif()
  set(objs "")
  foreach(l IN LISTS A_LINK)
    list(APPEND objs $<TARGET_OBJECTS:${l}>)
  endforeach()
  add_executable(${name} ${A_SOURCES} ${objs})
  target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${A_FLAGS}>")
  target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
  set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endfunction()

add_test(NAME run-wrapper COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/run-test.sh" "${CMAKE_CURRENT_SOURCE_DIR}/run.sh")

avxemu_test_exe(inject SOURCES inject.c stubs.s LINK avxemu_core avxemu_asm)
add_test(NAME inject COMMAND $<TARGET_FILE:inject>)

avxemu_test_exe(memtest SOURCES memtest.c memtest.s LINK avxemu_core avxemu_asm)
add_test(NAME memtest COMMAND $<TARGET_FILE:memtest>)

avxemu_test_exe(tramptest SOURCES tramptest.c tramp_harness.s LINK avxemu_core avxemu_asm)
add_test(NAME tramptest COMMAND $<TARGET_FILE:tramptest>)

avxemu_test_exe(overread SOURCES overread.c LINK avxemu_core avxemu_asm)
add_test(NAME overread COMMAND $<TARGET_FILE:overread>)

avxemu_test_exe(reloctest SOURCES reloctest.c LINK avxemu_core avxemu_asm)
add_test(NAME reloctest COMMAND $<TARGET_FILE:reloctest>)
set_tests_properties(reloctest PROPERTIES ENVIRONMENT "AVXEMU_DISABLE=1;AVXEMU_FORCETRAMP=1")

avxemu_test_exe(nativetest SOURCES nativetest.c LINK avxemu_core avxemu_asm)
add_test(NAME nativetest COMMAND $<TARGET_FILE:nativetest>)
set_tests_properties(nativetest PROPERTIES ENVIRONMENT "AVXEMU_DISABLE=1")

# The opt-in (AVXEMU_MINSPILL) live-register BMI thunks, each against bmi_exec. Hermetic: the
# emitted code is base-ISA only, and AVXEMU_DISABLE keeps the constructor out (the test sets up
# the pool itself).
avxemu_test_exe(minspilltest SOURCES minspilltest.c minspill_harness.s LINK avxemu_core avxemu_asm)
add_test(NAME minspilltest COMMAND $<TARGET_FILE:minspilltest>)
set_tests_properties(minspilltest PROPERTIES ENVIRONMENT "AVXEMU_DISABLE=1")

# Links avxemu into itself so the load-time passes act on its own cpuid, lzcnt and AVX2 run:
# the analysis cache records them, replays them identically, and refuses bad files.
avxemu_test_exe(cachetest SOURCES cachetest.c cachetest.s LINK avxemu_core avxemu_asm t_cpu)
add_test(NAME cachetest COMMAND $<TARGET_FILE:cachetest>)

avxemu_objects(t_zcnt "${PROJECT_SOURCE_DIR}/src/decode.c" "${PROJECT_SOURCE_DIR}/src/exec_bmi.c" "${PROJECT_SOURCE_DIR}/src/names.c")
avxemu_test_exe(zcnt16 SOURCES zcnt16.c LINK t_zcnt)
add_test(NAME zcnt16 COMMAND $<TARGET_FILE:zcnt16>)

# Inserted into a program of our own, not /usr/bin/true: SIP strips DYLD_INSERT_LIBRARIES from
# system binaries on macOS newer than 10.9. The verdict is the self-test's own summary line, so a
# run where avxemu never loaded cannot pass.
avxemu_test_exe(selftest_host FLAGS -O0 SOURCES selftest_host.c)
add_test(NAME selftest COMMAND env AVXEMU_SELFTEST=1 DYLD_INSERT_LIBRARIES=$<TARGET_FILE:avxemu>
  $<TARGET_FILE:selftest_host>)
set_tests_properties(selftest PROPERTIES PASS_REGULAR_EXPRESSION "avxemu selftest: all shapes OK")

avxemu_test_exe(linkhook FLAGS -O0 SOURCES linkhook.c)
target_link_libraries(linkhook PRIVATE avxemu)
# AVXEMU_FORCEPATCH keeps avxemu installed on a CPU that has everything, where it would
# otherwise stay inert (inerttest) and there would be no rebind to test.
add_test(NAME linkhook COMMAND env AVXEMU_FORCEPATCH=1 $<TARGET_FILE:linkhook>)
add_test(NAME linkhook-no-rebind COMMAND env AVXEMU_FORCEPATCH=1 AVXEMU_NO_REBIND=1 $<TARGET_FILE:linkhook>)

# Linked on a CPU that has everything, avxemu stays inert. The plain run checks the real early
# return on such a CPU, and on one without is the negative control: avxemu must be installed.
avxemu_test_exe(inerttest FLAGS -O0 SOURCES inerttest.c LINK t_cpu)
target_link_libraries(inerttest PRIVATE avxemu)
add_test(NAME inerttest COMMAND $<TARGET_FILE:inerttest>)
add_test(NAME inerttest-assume-capable COMMAND env AVXEMU_ASSUME_CAPABLE=1 $<TARGET_FILE:inerttest>)

# Linked against the dylib, like linkhook: the production configuration. N threads race each
# site's runtime relocation. It skips itself on a CPU with AVX2, where the loop never faults.
avxemu_test_exe(threadtest FLAGS -O2 -std=c11 SOURCES threadtest.c threadtest.s LINK t_cpu)
target_link_libraries(threadtest PRIVATE avxemu)
add_test(NAME threadtest COMMAND $<TARGET_FILE:threadtest> 20 12)

avxemu_test_exe(overread_fault FLAGS -O0 SOURCES overread_fault.c)
set(ROSETTA_OVERREAD "under Rosetta avxemu's fault handler does not repair and retry the read that straddles an unmapped page, and the test segfaults; it passes natively on 10.9 and on Intel with AVX2; the cause is not yet diagnosed")
add_test(NAME overread-fault COMMAND ${AVXEMU_RUN} --fails-when rosetta --because "${ROSETTA_OVERREAD}" -- env AVXEMU_FORCEPATCH=1 DYLD_INSERT_LIBRARIES=$<TARGET_FILE:avxemu> $<TARGET_FILE:overread_fault>)
avxemu_test_exe(guard_page FLAGS -O0 SOURCES guard_page.c)
add_test(NAME guard-page COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/guard-page.sh" $<TARGET_FILE:guard_page> $<TARGET_FILE:avxemu>)

set_tests_properties(run-wrapper inject memtest tramptest overread reloctest nativetest
                     minspilltest cachetest zcnt16 selftest linkhook linkhook-no-rebind
                     inerttest inerttest-assume-capable threadtest overread-fault guard-page
  PROPERTIES LABELS hermetic SKIP_RETURN_CODE 77)

avxemu_objects(t_refdigest refdigest.c cpu.c)
avxemu_test_exe(refdigest_test SOURCES refdigest_test.c LINK t_refdigest)
add_test(NAME refdigest COMMAND $<TARGET_FILE:refdigest_test>)
set_tests_properties(refdigest PROPERTIES LABELS hermetic SKIP_RETURN_CODE 77)

set(AVXEMU_REPLAY_PROGRAMS "")
function(avxemu_oracle name)
  cmake_parse_arguments(A "" "" "DRIVER;HW;ASM;LINK" ${ARGN})
  avxemu_objects(${name}_driver ${A_DRIVER})
  target_include_directories(${name}_driver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  set(objs $<TARGET_OBJECTS:${name}_driver> $<TARGET_OBJECTS:t_refdigest>)
  if(A_HW)
    add_library(${name}_hw OBJECT ${A_HW})
    target_compile_options(${name}_hw PRIVATE ${AVXEMU_HOST_FLAGS})
    target_include_directories(${name}_hw PRIVATE "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND objs $<TARGET_OBJECTS:${name}_hw>)
  endif()
  foreach(l IN LISTS A_LINK)
    list(APPEND objs $<TARGET_OBJECTS:${l}>)
  endforeach()
  add_executable(${name} ${A_ASM} ${objs})
  set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
  set(AVXEMU_REPLAY_PROGRAMS ${AVXEMU_REPLAY_PROGRAMS} ${name} PARENT_SCOPE)
  add_test(NAME ${name}-replay COMMAND $<TARGET_FILE:${name}> check "${CMAKE_CURRENT_SOURCE_DIR}/reference/${name}.ref")
  add_test(NAME ${name}-record COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/record-matches.sh" $<TARGET_FILE:${name}>
           "${CMAKE_BINARY_DIR}/reference/${name}.ref" "${CMAKE_CURRENT_SOURCE_DIR}/reference/${name}.ref")
  set_tests_properties(${name}-replay PROPERTIES LABELS replay SKIP_RETURN_CODE 77)
  set_tests_properties(${name}-record PROPERTIES LABELS hardware SKIP_RETURN_CODE 77)
endfunction()

avxemu_objects(t_exec "${PROJECT_SOURCE_DIR}/src/exec.c" "${PROJECT_SOURCE_DIR}/src/exec_bmi.c"
                      "${PROJECT_SOURCE_DIR}/src/softfma.c" "${PROJECT_SOURCE_DIR}/src/names.c")
avxemu_oracle(oracle DRIVER oracle.c HW oracle_hw.c LINK t_exec)

avxemu_objects(t_bmi "${PROJECT_SOURCE_DIR}/src/exec_bmi.c" "${PROJECT_SOURCE_DIR}/src/names.c")
avxemu_oracle(bmi_oracle DRIVER bmi_oracle.c HW bmi_hw.c LINK t_bmi)

avxemu_oracle(patchtest DRIVER patchtest.c HW patchtest_hw.c ASM patch_stubs.s LINK avxemu_core avxemu_asm)

avxemu_oracle(bmimem DRIVER bmimem.c ASM bmimem.s LINK avxemu_core avxemu_asm)

avxemu_objects(t_decode "${PROJECT_SOURCE_DIR}/src/decode.c" "${PROJECT_SOURCE_DIR}/src/names.c")
avxemu_objects(t_lde "${PROJECT_SOURCE_DIR}/src/lde.c")
avxemu_test_exe(bintest SOURCES bintest.c LINK t_decode)
avxemu_test_exe(patchdiff SOURCES patchdiff.c LINK t_lde)
avxemu_test_exe(zdecode SOURCES zdecode.c LINK t_lde t_decode)
avxemu_test_exe(fuzz SOURCES fuzz.c fuzz.s LINK avxemu_core avxemu_asm)
add_test(NAME claude-decode COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/claude-decode.sh"
  $<TARGET_FILE:bintest> $<TARGET_FILE:patchdiff> $<TARGET_FILE:zdecode> "${CMAKE_BINARY_DIR}/claude-decode")
add_test(NAME claude-hardware COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/claude-hardware.sh"
  $<TARGET_FILE:cpuprobe> $<TARGET_FILE:fuzz> "${CMAKE_BINARY_DIR}/claude-hardware")
add_test(NAME claude-trampoline COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/claude-trampoline.sh"
  $<TARGET_FILE:cpuprobe> $<TARGET_FILE:avxemu> "${CMAKE_BINARY_DIR}/claude-trampoline")
set_tests_properties(claude-decode PROPERTIES LABELS claude-binary SKIP_RETURN_CODE 77)
set_tests_properties(claude-hardware claude-trampoline PROPERTIES LABELS "claude-binary;hardware" SKIP_RETURN_CODE 77)

set(_replay_objs $<TARGET_OBJECTS:t_refdigest>)
foreach(p IN LISTS AVXEMU_REPLAY_PROGRAMS)
  list(APPEND _replay_objs $<TARGET_OBJECTS:${p}_driver>)
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/objects/replay.txt" CONTENT "$<JOIN:${_replay_objs},\n>\n")
add_test(NAME vex-leak-replay
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/vex-leak.sh"
          "${CMAKE_BINARY_DIR}/objects/replay.txt" "${CMAKE_BINARY_DIR}/objects/asm.txt")
set_tests_properties(vex-leak-replay PROPERTIES LABELS hermetic SKIP_RETURN_CODE 77)

# Every test here loads avxemu, whose constructor would otherwise record its load-time analysis
# into ~/Library/Caches/avxemu (src/lcache.c). Only cachetest uses the cache, in a directory of
# its own; its own launch must not use one either.
get_property(_avxemu_tests DIRECTORY PROPERTY TESTS)
set_property(TEST ${_avxemu_tests} APPEND PROPERTY ENVIRONMENT AVXEMU_NOCACHE=1)
