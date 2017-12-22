#add_subdirectory(util)

set(SOURCE_FILES
  util/fs.cpp
  mp4_index_creator.cpp
  video_index.cpp
  decoder_automata.cpp
  video_decoder_factory.cpp
  hwang_python.cpp)

if (BUILD_CUDA)
  add_definitions(-DHAVE_NVIDIA_VIDEO_HARDWARE)
  list(APPEND SOURCE_FILES
    impls/nvidia/nvidia_video_decoder.cpp)
  cuda_add_library(util_cuda
    impls/nvidia/convert.cu)
  install(TARGETS util_cuda
    EXPORT HwangTarget
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib)
endif()

# if (MFX_FOUND)
#   add_definitions(-DHAVE_INTEL_VIDEO_HARDWARE)
#   list(APPEND SOURCE_FILES
#     intel/intel_video_decoder.cpp)
# endif()

list(APPEND SOURCE_FILES
  impls/software/software_video_decoder.cpp)

message(${SOURCE_FILES})
add_library(hwang_source OBJECT
  ${SOURCE_FILES})

set_source_files_properties(${PROTO_SRCS} ${GRPC_PROTO_SRCS} PROPERTIES
  GENERATED TRUE)

add_executable(MP4IndexCreatorTest mp4_index_creator_test.cpp)
target_link_libraries(MP4IndexCreatorTest
  ${GTEST_LIBRARIES} ${GTEST_LIB_MAIN} hwang)
add_test(MP4IndexCreatorTest MP4IndexCreatorTest)

add_executable(DecoderAutomataTest decoder_automata_test.cpp)
target_link_libraries(DecoderAutomataTest
  ${GTEST_LIBRARIES} ${GTEST_LIB_MAIN} hwang)
add_test(DecoderAutomataTest DecoderAutomataTest)
