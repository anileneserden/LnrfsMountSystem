cmake_minimum_required(VERSION 3.10)
project(KryfsMountSystem)

set(CMAKE_CXX_STANDARD 17)

option(LNRFS_IS_V2_SUBBUILD "Internal flag to prevent recursive x86-64-v2 builds" OFF)

# FUSE3 paketini bul
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

# Header dosyalarının aranacağı dizin
include_directories(include ${FUSE3_INCLUDE_DIRS})
link_directories(${FUSE3_LIBRARY_DIRS})

set(LFS_SOURCES
    src/main.cpp
    src/lnrfs.cpp
    src/fuse_ops.cpp
)

add_executable(lfs ${LFS_SOURCES})
target_link_libraries(lfs ${FUSE3_LIBRARIES})
target_compile_options(lfs PRIVATE ${FUSE3_CFLAGS_OTHER})

if(LNRFS_IS_V2_SUBBUILD)
    target_compile_options(lfs PRIVATE -march=x86-64-v2 -mtune=generic)
else()
    set(V2_BUILD_DIR "${CMAKE_BINARY_DIR}/x86_64-v2")
    find_program(OBJCOPY_EXECUTABLE objcopy)

    if(OBJCOPY_EXECUTABLE)
        add_custom_command(TARGET lfs POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${V2_BUILD_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E env
                CFLAGS=-march=x86-64-v2\ -mtune=generic
                CXXFLAGS=-march=x86-64-v2\ -mtune=generic
                "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}" -B "${V2_BUILD_DIR}" -DLNRFS_IS_V2_SUBBUILD=ON
            COMMAND "${CMAKE_COMMAND}" --build "${V2_BUILD_DIR}" -j
            COMMAND "${OBJCOPY_EXECUTABLE}" --remove-section=.note.gnu.property "${V2_BUILD_DIR}/lfs"
            COMMENT "Building x86-64-v2 compatible binary in ${V2_BUILD_DIR}"
            VERBATIM
        )
    else()
        message(WARNING "objcopy bulunamadi; .note.gnu.property kaldirilamadi.")
    endif()
endif()