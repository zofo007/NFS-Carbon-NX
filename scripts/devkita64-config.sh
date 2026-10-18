BUILD_DKPRO_PACKAGE=3
BUILD_DKPRO_AUTOMATED=1
BUILD_DKPRO_SRCDIR="$(cygpath -u "$CARBON_WORKSPACE")/toolchains/downloads"
# Compiler first; libnx, packaging tools and rules are separate required stages.
BUILD_DKPRO_SKIP_CRTLS=1
GCC_DOWNLOAD_PREREQS=0
export MAKEFLAGS="-j${CARBON_BUILD_JOBS:-2}"
export CC=gcc CXX=g++
# libcody's exact C++11 probe must choose its dialect; keep its legacy u8 character type.
export CXXFLAGS='-g -O2 -fno-char8_t'
export CPPFLAGS='-D__USE_MINGW_ACCESS -D__USE_MINGW_ANSI_STDIO=1'
CROSS_GCC_PARAMS='--with-gmp=/mingw64 --with-mpfr=/mingw64 --with-mpc=/mingw64 --with-isl=/mingw64 --with-zstd=/mingw64'
