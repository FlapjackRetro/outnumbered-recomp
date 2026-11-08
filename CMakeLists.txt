cmake_minimum_required(VERSION 3.16)
project(OutNumberedRecomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT MSVC)
  add_compile_options(-w -fno-strict-aliasing "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>")   # generated code is warning-noisy
endif()

# gen/ is written by tools/lift_full.py (git-ignored)
file(GLOB GENERATED CONFIGURE_DEPENDS gen/recomp_*.c gen/vga_*.c gen/tmpl_*.c)
if(NOT GENERATED)
  message(FATAL_ERROR "gen/ is empty: run tools/lift_full.py first")
endif()

add_executable(sso src/main.c src/recomp/cpu.c src/recomp/runtime16.c src/recomp/overrides.c src/recomp/exepack.c src/recomp/ega.c src/recomp/audio.cpp
  third_party/ymfm/ymfm_opl.cpp third_party/ymfm/ymfm_adpcm.cpp third_party/ymfm/ymfm_pcm.cpp ${GENERATED})
target_include_directories(sso PRIVATE third_party/ymfm)
target_compile_definitions(sso PRIVATE X87_SHARED)   # one FP stack across the generated chunks
# Optional window + keyboard (SDL2). -DSSO_SDL=OFF forces the headless build.
option(SSO_SDL "Build the SDL2 front end (window, keyboard, sound). OFF = headless test build" ON)
if(SSO_SDL)
  find_package(SDL2 QUIET)
  if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 not found, so there would be no window.\n"
      "  Debian/Ubuntu: sudo apt install libsdl2-dev   Fedora: sudo dnf install SDL2-devel\n"
      "  MSYS2: pacman -S mingw-w64-x86_64-SDL2\n"
      "  Or configure with -DSSO_SDL=OFF (tools/build.py --headless) for a headless test build.")
  endif()
endif()
if(SSO_SDL AND SDL2_FOUND)
  target_sources(sso PRIVATE src/recomp/frontend_sdl.c)
  if(TARGET SDL2::SDL2)
    target_link_libraries(sso PRIVATE SDL2::SDL2)
  else()
    target_include_directories(sso PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(sso PRIVATE ${SDL2_LIBRARIES})
  endif()
  message(STATUS "SDL2 front end enabled")
else()
  target_sources(sso PRIVATE src/recomp/frontend_stub.c)
  message(STATUS "SDL2 not found: headless build (SSO_KEYS scripting only)")
endif()
target_compile_definitions(sso PRIVATE RECOMP_IRQ RECOMP_MEM_HOOK RECOMP_TRACE)
target_include_directories(sso PRIVATE src/recomp gen)
find_package(Threads REQUIRED)
target_link_libraries(sso PRIVATE Threads::Threads m)
if(NOT MSVC)
  set_source_files_properties(src/recomp/audio.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
set_target_properties(sso PROPERTIES ENABLE_EXPORTS ON)   # symbol names in watchdog backtraces

enable_testing()
add_executable(test_x87 test/test_x87.c)
target_link_libraries(test_x87 PRIVATE m)
add_test(NAME x87 COMMAND test_x87)
