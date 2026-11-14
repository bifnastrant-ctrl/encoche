cmake_minimum_required(VERSION 3.20)
project(Encoche LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Runtime statique : l'exe tourne sans installer le redistribuable Visual C++
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_executable(Encoche WIN32
    src/main.cpp
    src/music_module.cpp
)

target_compile_definitions(Encoche PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_options(Encoche PRIVATE /utf-8 /W3)
target_link_libraries(Encoche PRIVATE
    windowsapp gdiplus shlwapi shell32 user32 gdi32 ole32)
