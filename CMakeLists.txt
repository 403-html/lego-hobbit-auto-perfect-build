cmake_minimum_required(VERSION 3.15)
project(AutoPerfectBuild LANGUAGES CXX)

# LEGOHobbit.exe (the build Steam launches) is a 32-bit process, so the proxy DLL
# must be 32-bit too:
#   Visual Studio : cmake -B build -A Win32 && cmake --build build --config Release
#   MinGW (cross) : cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure a 32-bit build (-A Win32 or the i686 MinGW toolchain).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(dinput8 SHARED
  src/main.cpp
  src/game.cpp
  src/hook.cpp
  src/config.cpp
  src/log.cpp
  src/proxy_dinput8.cpp
  src/ui.cpp
  src/overlay_d3d9.cpp
  src/input_hooks.cpp
  src/dinput8.def)

set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")
target_compile_definitions(dinput8 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

if(MSVC)
  target_compile_options(dinput8 PRIVATE /W4 /O2)
  set_property(TARGET dinput8 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT
else()
  target_compile_options(dinput8 PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
  target_link_options(dinput8 PRIVATE -static -static-libgcc -static-libstdc++
                      -Wl,--enable-stdcall-fixup -s)
endif()
