cmake_minimum_required(VERSION 3.13)

project(amulet_anvil LANGUAGES CXX)

set(amulet_anvil_DIR ${CMAKE_CURRENT_LIST_DIR}/src/amulet/anvil CACHE PATH "")
set(BUILD_AMULET_ANVIL_TESTS OFF CACHE BOOL "Should tests be built?")

# Set C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set platform variables
if (WIN32)
	# set windows 7 as the minimum version
	add_definitions(-D_WIN32_WINNT=0x0601)
elseif(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
elseif(UNIX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
    message( FATAL_ERROR "Unsupported platform. Please submit a pull request to support this platform." )
endif()

if (MSVC)
    add_definitions("/MP")
endif()

include(FetchContent)

# Add lz4
FetchContent_Declare(
  lz4
  GIT_REPOSITORY https://github.com/lz4/lz4.git
  GIT_TAG        ebb370ca83af193212df4dcbadcc5d87bc0de2f0
)
FetchContent_MakeAvailable(lz4)
set(LZ4_BUILD_CLI OFF)
add_subdirectory(${lz4_SOURCE_DIR}/build/cmake ${CMAKE_BINARY_DIR}/lz4)


# Find libraries
if (NOT TARGET pybind11::module)
    find_package(pybind11 CONFIG REQUIRED)
endif()
if (NOT TARGET amulet_pybind11_extensions)
    find_package(amulet_pybind11_extensions CONFIG REQUIRED)
endif()
if (NOT TARGET amulet_io)
    find_package(amulet_io CONFIG REQUIRED)
endif()
if (NOT TARGET amulet_utils)
    find_package(amulet_utils CONFIG REQUIRED)
endif()
if (NOT TARGET amulet_zlib)
    find_package(amulet_zlib CONFIG REQUIRED)
endif()
if (NOT TARGET amulet_nbt)
    find_package(amulet_nbt CONFIG REQUIRED)
endif()
if (NOT TARGET amulet_core)
    find_package(amulet_core CONFIG REQUIRED)
endif()

# Find C++ files
file(REAL_PATH src SOURCE_PATH)
file(GLOB_RECURSE EXTENSION_SOURCES LIST_DIRECTORIES false ${SOURCE_PATH}/amulet/*.py.cpp)
file(GLOB_RECURSE EXTENSION_HEADERS LIST_DIRECTORIES false ${SOURCE_PATH}/amulet/*.py.hpp)
file(GLOB_RECURSE SOURCES LIST_DIRECTORIES false ${SOURCE_PATH}/amulet/*.cpp)
file(GLOB_RECURSE HEADERS LIST_DIRECTORIES false ${SOURCE_PATH}/amulet/*.hpp)
list(REMOVE_ITEM SOURCES ${EXTENSION_SOURCES})
list(REMOVE_ITEM HEADERS ${EXTENSION_HEADERS})

# Add implementation
add_library(amulet_anvil SHARED)
target_compile_definitions(amulet_anvil PRIVATE ExportAmuletAnvil)
target_link_libraries(amulet_anvil PRIVATE lz4_static)
target_link_libraries(amulet_anvil PUBLIC pybind11::module)
target_link_libraries(amulet_anvil PUBLIC amulet_pybind11_extensions)
target_link_libraries(amulet_anvil PUBLIC amulet_io)
target_link_libraries(amulet_anvil PUBLIC amulet_utils)
target_link_libraries(amulet_anvil PRIVATE amulet_zlib)
target_link_libraries(amulet_anvil PUBLIC amulet_nbt)
target_link_libraries(amulet_anvil PUBLIC amulet_core)
target_include_directories(amulet_anvil PUBLIC ${SOURCE_PATH})
target_sources(amulet_anvil PRIVATE ${SOURCES} ${HEADERS})
foreach(FILE ${SOURCES} ${HEADERS})
    file(RELATIVE_PATH REL_PATH ${SOURCE_PATH} ${FILE})
    get_filename_component(GROUP ${REL_PATH} DIRECTORY)
    string(REPLACE "/" "\\" GROUP ${GROUP})
    source_group(${GROUP} FILES ${FILE})
endforeach()

# Add python extension
pybind11_add_module(_amulet_anvil)
target_link_libraries(_amulet_anvil PRIVATE amulet_pybind11_extensions)
target_link_libraries(_amulet_anvil PRIVATE amulet_anvil)
target_compile_definitions(_amulet_anvil PRIVATE PYBIND11_DETAILED_ERROR_MESSAGES)
target_compile_definitions(_amulet_anvil PRIVATE PYBIND11_VERSION="${pybind11_VERSION}")
target_compile_definitions(_amulet_anvil PRIVATE COMPILER_ID="${CMAKE_CXX_COMPILER_ID}")
target_compile_definitions(_amulet_anvil PRIVATE COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}")
target_sources(_amulet_anvil PRIVATE ${EXTENSION_SOURCES} ${EXTENSION_HEADERS})
foreach(FILE ${EXTENSION_SOURCES} ${EXTENSION_HEADERS})
    file(RELATIVE_PATH REL_PATH ${SOURCE_PATH} ${FILE})
    get_filename_component(GROUP ${REL_PATH} DIRECTORY)
    string(REPLACE "/" "\\" GROUP ${GROUP})
    source_group(${GROUP} FILES ${FILE})
endforeach()

if(NOT DEFINED AMULET_ANVIL_EXT_DIR)
    set(AMULET_ANVIL_EXT_DIR ${amulet_anvil_DIR})
endif()

# Install
install(TARGETS amulet_anvil DESTINATION ${amulet_anvil_DIR})
install(TARGETS _amulet_anvil DESTINATION ${AMULET_ANVIL_EXT_DIR})

if (BUILD_AMULET_ANVIL_TESTS)
    add_subdirectory(tests)
endif()
