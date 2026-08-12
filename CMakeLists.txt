cmake_minimum_required(VERSION 3.25)
project(PowerModeTray LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "PowerModeTray builds with MSVC only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: no redistributable, single .exe.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# CMake injects /EHsc and /O2 by default; both are wrong for this target.
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "/O2" "/O1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/O2" "/O1" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

add_executable(PowerModeTray WIN32
  src/main.cpp
  src/overlay.cpp
  src/osd.cpp
  src/autostart.cpp
  res/PowerModeTray.rc
)

target_include_directories(PowerModeTray PRIVATE src res)

target_compile_definitions(PowerModeTray PRIVATE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN NOMINMAX STRICT
)

target_compile_options(PowerModeTray PRIVATE
  /W4 /permissive- /GR- /EHs-c- /utf-8
  $<$<CONFIG:Release>:/GL>
)

target_link_options(PowerModeTray PRIVATE
  # The manifest is embedded through the .rc; suppress the linker's own.
  /MANIFEST:NO
  $<$<CONFIG:Release>:/LTCG>
  $<$<CONFIG:Release>:/OPT:REF>
  $<$<CONFIG:Release>:/OPT:ICF>
)

# powrprof is resolved at runtime by name (see overlay.cpp) and never linked.
target_link_libraries(PowerModeTray PRIVATE
  user32 gdi32 shell32 advapi32 comctl32 wtsapi32
)
