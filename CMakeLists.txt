cmake_minimum_required(VERSION 3.10)
project(DigitalContagion VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

file(GLOB SOURCES "src/*.cpp")

if(APPLE AND EXISTS "/opt/homebrew/opt/sfml@2")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sfml@2")
elseif(APPLE AND EXISTS "/usr/local/opt/sfml@2")
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/sfml@2")
endif()

find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

# Set executable output to build/simulation
# Target name is simulation, so the output binary will be named simulation
add_executable(simulation ${SOURCES})

target_include_directories(simulation PRIVATE include)

target_link_libraries(simulation PRIVATE sfml-graphics sfml-window sfml-system)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fopenmp" HAS_OPENMP_FLAG)

if(HAS_OPENMP_FLAG)
    target_compile_options(simulation PRIVATE "-fopenmp")
    target_link_options(simulation PRIVATE "-fopenmp")
    target_compile_definitions(simulation PRIVATE HAS_OPENMP)
endif()
