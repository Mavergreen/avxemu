# avxemu's tests. Each later task appends; the _replay_objs block added in Task 7 stays last.

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

set(PATCHES_OWN_TEXT "at this commit avxemu's load-time constructor patches cpuid, on any CPU, in the __text it is running from -- the core is linked into this test program -- and faults; the follow-up fix 'keep __text executable while patching it at load, and keep our own cpuid out of it' (release 2) fixes it")
set(STUBS_IN_TEXT "${PATCHES_OWN_TEXT}; then its load-time passes rewrite this test's own stubs in __text, which the follow-up fix 'keep test and self-test stubs out of __text, ...' (release 2) fixes")

add_test(NAME run-wrapper COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/run-test.sh" "${CMAKE_CURRENT_SOURCE_DIR}/run.sh")

avxemu_test_exe(inject SOURCES inject.c stubs.s LINK avxemu_core avxemu_asm)
add_test(NAME inject COMMAND ${AVXEMU_RUN} --fails-when always --because "${STUBS_IN_TEXT}" -- $<TARGET_FILE:inject>)

avxemu_test_exe(memtest SOURCES memtest.c memtest.s LINK avxemu_core avxemu_asm)
add_test(NAME memtest COMMAND ${AVXEMU_RUN} --fails-when always --because "${STUBS_IN_TEXT}" -- $<TARGET_FILE:memtest>)

avxemu_test_exe(tramptest SOURCES tramptest.c tramp_harness.s LINK avxemu_core avxemu_asm)
add_test(NAME tramptest COMMAND ${AVXEMU_RUN} --fails-when always --because "${STUBS_IN_TEXT}" -- $<TARGET_FILE:tramptest>)

avxemu_test_exe(overread SOURCES overread.c LINK avxemu_core avxemu_asm)
add_test(NAME overread COMMAND ${AVXEMU_RUN} --fails-when always --because "${PATCHES_OWN_TEXT}" -- $<TARGET_FILE:overread>)

avxemu_test_exe(reloctest SOURCES reloctest.c LINK avxemu_core avxemu_asm)
add_test(NAME reloctest COMMAND $<TARGET_FILE:reloctest>)
set_tests_properties(reloctest PROPERTIES ENVIRONMENT "AVXEMU_DISABLE=1;AVXEMU_FORCETRAMP=1")

avxemu_test_exe(nativetest SOURCES nativetest.c LINK avxemu_core avxemu_asm)
add_test(NAME nativetest COMMAND $<TARGET_FILE:nativetest>)
set_tests_properties(nativetest PROPERTIES ENVIRONMENT "AVXEMU_DISABLE=1")

avxemu_objects(t_zcnt "${PROJECT_SOURCE_DIR}/src/decode.c" "${PROJECT_SOURCE_DIR}/src/exec_bmi.c" "${PROJECT_SOURCE_DIR}/src/names.c")
avxemu_test_exe(zcnt16 SOURCES zcnt16.c LINK t_zcnt)
add_test(NAME zcnt16 COMMAND $<TARGET_FILE:zcnt16>)

add_test(NAME selftest COMMAND ${AVXEMU_RUN} --needs mavericks --
  env AVXEMU_SELFTEST=1 DYLD_INSERT_LIBRARIES=$<TARGET_FILE:avxemu> /usr/bin/true)

avxemu_test_exe(linkhook FLAGS -O0 SOURCES linkhook.c)
target_link_libraries(linkhook PRIVATE avxemu)
add_test(NAME linkhook COMMAND ${AVXEMU_RUN} --needs mavericks -- $<TARGET_FILE:linkhook>)
add_test(NAME linkhook-no-rebind COMMAND ${AVXEMU_RUN} --needs mavericks -- env AVXEMU_NO_REBIND=1 $<TARGET_FILE:linkhook>)

avxemu_test_exe(overread_fault FLAGS -O0 SOURCES overread_fault.c)
add_test(NAME overread-fault COMMAND env AVXEMU_FORCEPATCH=1 DYLD_INSERT_LIBRARIES=$<TARGET_FILE:avxemu> $<TARGET_FILE:overread_fault>)
avxemu_test_exe(guard_page FLAGS -O0 SOURCES guard_page.c)
add_test(NAME guard-page COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/guard-page.sh" $<TARGET_FILE:guard_page> $<TARGET_FILE:avxemu>)

set_tests_properties(run-wrapper inject memtest tramptest overread reloctest nativetest zcnt16
                     overread-fault guard-page
  PROPERTIES LABELS hermetic SKIP_RETURN_CODE 77)
set_tests_properties(selftest linkhook linkhook-no-rebind
  PROPERTIES LABELS mavericks SKIP_RETURN_CODE 77)
