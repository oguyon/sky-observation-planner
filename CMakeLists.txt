cmake_minimum_required(VERSION 3.10)
project(night_sky C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g")

# Find GTK4
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

# Find libnova
# Search for libnova.h inside 'libnova' subdirectory
find_path(LIBNOVA_INCLUDE_PATH NAMES libnova.h PATH_SUFFIXES libnova PATHS /usr/include /usr/local/include)

if (LIBNOVA_INCLUDE_PATH)
    # If found in /usr/include/libnova, we want /usr/include because source uses <libnova/libnova.h>
    get_filename_component(LIBNOVA_INCLUDE_DIR "${LIBNOVA_INCLUDE_PATH}/.." ABSOLUTE)
else()
    message(WARNING "libnova headers not found")
endif()

find_library(LIBNOVA_LIBRARY NAMES nova PATHS /usr/lib /usr/local/lib)

include_directories(${GTK4_INCLUDE_DIRS} ${LIBNOVA_INCLUDE_DIR})
link_directories(${GTK4_LIBRARY_DIRS})

add_executable(night_sky
    main.c
    sky_model.c
    sky_view.c
    elevation_view.c
)

target_link_libraries(night_sky
    ${GTK4_LIBRARIES}
    ${LIBNOVA_LIBRARY}
    m
)
