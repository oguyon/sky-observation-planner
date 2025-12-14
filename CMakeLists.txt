cmake_minimum_required(VERSION 3.10)
project(night_sky C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g")

# Find GTK3
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# Find libnova
# libnova might not have a proper cmake module or pkg-config in some envs,
# so we might need to handle it manually if pkg_check_modules fails.
# But based on previous steps, pkg-config knows about it, or at least we know paths.
# Previous Makefile used: `pkg-config --cflags gtk+-3.0` -I/usr/include/libnova
# And libs: -lnova -lm
# The user environment had issues with `pkg-config --list-all | grep nova` returning nothing for pkg-config,
# but found headers in /usr/include/libnova and lib in /usr/lib.

# So we will search for the library and include path manually if pkg-config fails or just do it manually.
find_path(LIBNOVA_INCLUDE_DIR NAMES libnova/libnova.h PATHS /usr/include /usr/local/include)
find_library(LIBNOVA_LIBRARY NAMES nova PATHS /usr/lib /usr/local/lib)

include_directories(${GTK3_INCLUDE_DIRS} ${LIBNOVA_INCLUDE_DIR})
link_directories(${GTK3_LIBRARY_DIRS})

add_executable(night_sky
    main.c
    sky_model.c
    sky_view.c
    elevation_view.c
)

target_link_libraries(night_sky
    ${GTK3_LIBRARIES}
    ${LIBNOVA_LIBRARY}
    m
)
