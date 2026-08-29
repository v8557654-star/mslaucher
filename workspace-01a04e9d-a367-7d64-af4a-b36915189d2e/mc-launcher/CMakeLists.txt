cmake_minimum_required(VERSION 3.21)

project(MCLauncher VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)

add_library(miniz STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)
target_include_directories(miniz PUBLIC third_party/miniz)

add_executable(MCLauncher WIN32
    src/main.cpp
    src/common.cpp
    src/http.cpp
    src/app_paths.cpp
    src/zip_extract.cpp
    src/auth.cpp
    src/minecraft.cpp
    src/modrinth.cpp
    src/curseforge.cpp
    src/skin.cpp
    src/launcher.cpp
    src/main_window.cpp
    src/common.h
    src/http.h
    src/app_paths.h
    src/zip_extract.h
    src/auth.h
    src/minecraft.h
    src/modrinth.h
    src/curseforge.h
    src/skin.h
    src/launcher.h
    src/main_window.h
)

target_include_directories(MCLauncher PRIVATE src)
target_link_libraries(MCLauncher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    miniz
)

if(WIN32)
    target_link_libraries(MCLauncher PRIVATE Crypt32)
    target_compile_definitions(MCLauncher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    target_compile_options(MCLauncher PRIVATE /W4 /permissive-)
else()
    target_compile_options(MCLauncher PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS MCLauncher RUNTIME DESTINATION .)
