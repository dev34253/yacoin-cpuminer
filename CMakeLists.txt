cmake_minimum_required(VERSION 3.16)
project(yacoin-cpuminer VERSION 0.1.0 LANGUAGES C CXX)

# scrypt-jane's compile-time variant selection (SCRYPT_CHOOSE_COMPILETIME) and its
# inline assembly are written for gcc; yacoind builds it with gcc too (plan F3).
if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "yacoin-cpuminer must be built with gcc/g++ "
                      "(found C=${CMAKE_C_COMPILER_ID}, CXX=${CMAKE_CXX_COMPILER_ID})")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  message(FATAL_ERROR "yacoin-cpuminer supports x86-64 only (plan §2)")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

# Baseline: the node's flags (-O3 -msse2 -> ChaCha SSE2 path). With YAC_NATIVE=ON,
# -march=native lets scrypt-jane pick the SSSE3/AVX ChaCha code on this CPU instead.
option(YAC_NATIVE "Build with -march=native (local builds; binary not portable)" ON)
set(YAC_ARCH_FLAGS -msse2)
if(YAC_NATIVE)
  set(YAC_ARCH_FLAGS -march=native)
endif()

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# ---- scrypt-jane (copied from yacoin, see third_party/scrypt-jane/SOURCE.md) ----
# src/hash/yac_scrypt.c #includes the unmodified scrypt-jane.c and adds the
# caller-owned-scratch entry point, so the mix code exists only once.
add_library(yac_scrypt STATIC src/hash/yac_scrypt.c)
target_include_directories(yac_scrypt PUBLIC src/hash third_party/scrypt-jane)
target_compile_definitions(yac_scrypt PRIVATE SCRYPT_KECCAK512 SCRYPT_CHACHA SCRYPT_CHOOSE_COMPILETIME)
target_compile_options(yac_scrypt PRIVATE -O3 ${YAC_ARCH_FLAGS} -w)

# ---- miner core (everything except main) ----
add_library(yac_core STATIC
  src/util/hex.cpp
)
target_include_directories(yac_core PUBLIC src third_party)
target_include_directories(yac_core SYSTEM PUBLIC third_party/nlohmann)
target_link_libraries(yac_core PUBLIC yac_scrypt CURL::libcurl Threads::Threads)
target_compile_options(yac_core PRIVATE -O2 ${YAC_ARCH_FLAGS} -Wall -Wextra -Wno-unused-parameter)

add_executable(yacoin-cpuminer src/main.cpp)
target_link_libraries(yacoin-cpuminer PRIVATE yac_core)
target_compile_options(yacoin-cpuminer PRIVATE -O2 -Wall -Wextra)
target_compile_definitions(yac_core PUBLIC YAC_MINER_VERSION="${PROJECT_VERSION}")

# ---- tests ----
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
