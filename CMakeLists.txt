cmake_minimum_required(VERSION 3.31)
project(FileSystem)

set(CMAKE_CXX_STANDARD 17)

add_library(volume STATIC
        include/volume_manager.h
        src/volume_manager.cpp
)

target_include_directories(volume PUBLIC include)

add_library(bitmap STATIC
        include/bitmap_manager.h
        src/bitmap_manager.cpp
)

target_include_directories(bitmap PUBLIC include)

add_library(fat STATIC
        include/fat_manager.h
        src/fat_manager.cpp
)

target_include_directories(fat PUBLIC include)

add_library(directory STATIC
        include/directory_manager.h
        src/directory_manager.cpp
)

target_include_directories(directory PUBLIC include)

add_library(fs_core
        include/fs_core.h
        src/fs_core.cpp
)
target_include_directories(fs_core PUBLIC include)

add_library(other INTERFACE)

target_include_directories(other INTERFACE include)

target_sources(other INTERFACE
        include/output.h
        include/file_system_config.h
)

add_executable(FileSystem src/main.cpp)

target_include_directories(FileSystem PRIVATE include)

target_link_libraries(FileSystem PRIVATE
        bitmap
        volume
        fat
        directory
        fs_core
        other
)