# CMake toolchain for tools/desktop-dev-build.sh: cross build for the Steam Frame on an x86_64
# desktop (clang + lld) against a copy of the Frame's own /usr, kept in <root>/sysroot.
# <root> is ~/frame-build/mlxr (set by the script as MLXR_DEV_ROOT).
set(FRAME_ROOT "$ENV{MLXR_DEV_ROOT}")
set(FRAME_SYSROOT "${FRAME_ROOT}/sysroot")
# a second build tree (DEV_DIR) shares mlxr's sysroot (MLXR_DEV_SYSROOT)
if(DEFINED ENV{MLXR_DEV_SYSROOT})
    set(FRAME_SYSROOT "$ENV{MLXR_DEV_SYSROOT}")
endif()
set(FRAME_TRIPLE aarch64-unknown-linux-gnu)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT "${FRAME_SYSROOT}")
# wrappers (made by desktop-dev-build.sh) that add -Wno-error last: the desktop clang is newer than
# the Frame's 19 and its extra warnings must not fail the build
set(CMAKE_C_COMPILER "${FRAME_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${FRAME_ROOT}/bin/clang++")
set(CMAKE_C_COMPILER_TARGET ${FRAME_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${FRAME_TRIPLE})
set(CMAKE_AR llvm-ar CACHE FILEPATH "")
set(CMAKE_RANLIB llvm-ranlib CACHE FILEPATH "")

file(GLOB FRAME_GCC_DIR LIST_DIRECTORIES true "${FRAME_SYSROOT}/usr/lib/gcc/${FRAME_TRIPLE}/15*")
list(GET FRAME_GCC_DIR 0 FRAME_GCC_DIR)
set(CMAKE_C_FLAGS_INIT "--gcc-install-dir=${FRAME_GCC_DIR}")
set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=${FRAME_GCC_DIR}")
foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_INIT "-fuse-ld=lld --gcc-install-dir=${FRAME_GCC_DIR}")
endforeach()

set(CMAKE_FIND_ROOT_PATH "${FRAME_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${FRAME_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${FRAME_SYSROOT}/usr/lib/pkgconfig:${FRAME_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64-static -L "${FRAME_SYSROOT}")
