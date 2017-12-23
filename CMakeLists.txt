# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.2.0 FATAL_ERROR)

project(Hwang)

###### Config options #####
option(BUILD_CUDA "" OFF)
option(BUILD_TESTS "" ON)

if (BUILD_TESTS)
  enable_testing()
endif()

###### Setup #########
# Verify C++11 support
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++1y" COMPILER_SUPPORTS_CXX1Y)
if(COMPILER_SUPPORTS_CXX1Y)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y")
# if(COMPILER_SUPPORTS_CXX11)
#   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
  message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++1y support.")
endif()

# Build optimized version if not specified
if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, defaulting to Release")
    set(CMAKE_BUILD_TYPE "Release")
endif()

# Include our custom cmake modules for finding packages
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules/")

set(GLOBAL_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb")
  set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
if(NOT APPLE AND UNIX)
  set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -pthread -ldl -lrt")
endif()

###### Optional Dependencies #######
set(THIRDPARTY_DIR ${CMAKE_SOURCE_DIR}/thirdparty/install)

if (NOT EXISTS "$ENV{BOOSTROOT}" AND NOT EXISTS "${BOOSTROOT}")
  set(BOOSTROOT ${THIRDPARTY_DIR})
endif()

###### Required Dependencies #######
###### Parse dependency file #######
file(STRINGS ${CMAKE_SOURCE_DIR}/dependencies.txt ConfigContents)
foreach(NameAndValue ${ConfigContents})
  # Strip leading spaces
  string(REGEX REPLACE "^[ ]+" "" NameAndValue ${NameAndValue})
  # Find variable name
  string(REGEX MATCH "^[^=]+" Name ${NameAndValue})
  # Find the value
  string(REPLACE "${Name}=" "" Value ${NameAndValue})
  # Set the variable
  set(${Name} "${Value}")
endforeach()

list(APPEND CMAKE_PREFIX_PATH ${FFMPEG_DIR})
list(APPEND CMAKE_PREFIX_PATH ${PROTOBUF_DIR})

find_package(SaneProtobuf REQUIRED)
find_package(FFmpeg REQUIRED)
find_package(Boost COMPONENTS thread program_options regex python REQUIRED)
find_package(GFlags REQUIRED)
find_package(Glog REQUIRED)
find_package(GoogleTest REQUIRED)
find_package(PythonLibs 2.7 EXACT REQUIRED)
find_package(OpenMP REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")

set(GTEST_INCLUDE_DIRS
  "${GOOGLETEST_INCLUDE_DIRS}")

set(GTEST_LIBRARIES
  "${GOOGLETEST_LIBRARIES}")

set(GTEST_LIB_MAIN
  "${GOOGLETEST_INCLUDE_DIRS}/../lib/libgtest_main.a")

message("${Boost_LIBRARIES}")
set(HWANG_LIBRARIES
  "${PROTOBUF_LIBRARY}"
  "${FFMPEG_LIBRARIES}"
  "${FFMPEG_LIBRARIES}"
  "${GFLAGS_LIBRARIES}"
  "${GLOG_LIBRARIES}"
  "-lpython2.7"
  "${Boost_LIBRARIES}"
  "${Boost_LIBRARY_DIRS}/libboost_python.so"
  "${Boost_LIBRARY_DIRS}/libboost_numpy.so")

include_directories(
  "."
  "${CMAKE_CURRENT_BINARY_DIR}" # for protobuf generated files
  "${PROTOBUF_INCLUDE_DIRS}"
  "${FFMPEG_INCLUDE_DIR}"
  "${GLOG_INCLUDE_DIRS}"
  "${PYTHON_INCLUDE_DIRS}"
  "${Boost_INCLUDE_DIRS}")

if (BUILD_TESTS)
  include_directories("${GTEST_INCLUDE_DIRS}")
endif()


###### CUDA Dependencies
if (BUILD_CUDA)
  find_package(CUDA REQUIRED)
  add_definitions(-DHAVE_CUDA)
  include_directories(${CUDA_INCLUDE_DIRS})
  if(COMPILER_SUPPORTS_CXX1Y)
    set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -std=c++11")
  endif()

  # Include nvidia decoder library
  list(APPEND HWANG_LIBRARIES
    hwang_util_cuda
    "${CUDA_LIBRARIES}"
    "/usr/lib/x86_64-linux-gnu/libnvcuvid.so"
    "-lcuda")
endif()

if (APPLE)
  include_directories(
    "/System/Library/Frameworks/Accelerate.framework/Versions/Current/Frameworks/vecLib.framework/Versions/Current/Headers/")
endif()

###### Project code #######
set(PROTO_FILES
  hwang/descriptors.proto)

protobuf_generate_cpp(
  PROTO_SRCS PROTO_HDRS OFF ${PROTO_FILES})

protobuf_generate_python(PROTO_PY OFF ${PROTO_FILES})

add_custom_target(proto_files DEPENDS
  ${PROTO_HDRS} ${PROTO_PY} )

macro(add_deps)
   add_dependencies(${targetName} proto_files)
endmacro()

function(add_library targetName)
  _add_library(${targetName} ${ARGN})
  add_deps()
endfunction()

function(add_executable targetName)
  _add_executable(${targetName} ${ARGN})
  add_deps()
endfunction()

add_subdirectory(hwang)

add_library(hwang SHARED
  $<TARGET_OBJECTS:hwang_source>
  ${PROTO_SRCS})

if(APPLE)
  set(PLATFORM_LINK_FLAGS
    "-framework CoreFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework Security"
    "-framework VideoDecodeAcceleration"
    "-framework VideoToolbox"
    "-framework Accelerate"
    "-undefined dynamic_lookup"
    )
endif()

target_link_libraries(hwang PUBLIC
  ${HWANG_LIBRARIES}
  ${PLATFORM_LINK_FLAGS})

set(PUBLIC_HEADER_FILES
  hwang/common.h
  hwang/mp4_index_creator.h
  hwang/decoder_automata.h
  hwang/video_decoder_interface.h
  hwang/video_decoder_factory.h
  hwang/video_index.h)

set(PYDIR ${CMAKE_CURRENT_BINARY_DIR})

# Install commands
MACRO(INSTALL_HEADERS_WITH_DIRECTORY HEADER_LIST)

FOREACH(HEADER ${${HEADER_LIST}})
STRING(REGEX MATCH "(.*)[/\\]" DIR ${HEADER})
INSTALL(FILES ${HEADER} DESTINATION include/${DIR})
ENDFOREACH(HEADER)

ENDMACRO(INSTALL_HEADERS_WITH_DIRECTORY)

install(TARGETS hwang
  EXPORT HwangTarget
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)

set(HWANG_LIBRARY "libhwang.so")

set(HWANG_LIBRARY "${CMAKE_INSTALL_PREFIX}/lib/${HWANG_LIBRARY}")

configure_file(cmake/HwangConfig.cmake.in
  "${PROJECT_BINARY_DIR}/cmake/HwangConfig.cmake" @ONLY)

install(FILES "${PROJECT_BINARY_DIR}/cmake/HwangConfig.cmake"
  DESTINATION cmake)

install_headers_with_directory(PUBLIC_HEADER_FILES)

install(EXPORT HwangTarget
  DESTINATION cmake)

# Make init files so python code can import from subdirectories
# foreach(FIL ${PROTO_FILES} ${GRPC_PROTO_FILES})
#   get_filename_component(DIR_FIL ${FIL} DIRECTORY)
#   get_filename_component(FIL_WE ${FIL} NAME_WE)
#   add_custom_command(TARGET proto_files POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E touch ${PYDIR}/${DIR_FIL}/__init__.py)
# endforeach()
