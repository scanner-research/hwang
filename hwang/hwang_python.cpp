#include "hwang/video_index.h"
#include "hwang/mp4_index_creator.h"
#include "hwang/video_decoder_factory.h"
#include "hwang/decoder_automata.h"

#include <boost/python.hpp>
#include <boost/python/stl_iterator.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/numpy.hpp>

using namespace hwang;
namespace bp = boost::python;
namespace np = boost::python::numpy;

namespace {
class GILRelease {
 public:
  inline GILRelease() {
    PyEval_InitThreads();
    m_thread_state = PyEval_SaveThread();
  }

  inline ~GILRelease() {
    PyEval_RestoreThread(m_thread_state);
    m_thread_state = NULL;
  }

 private:
  PyThreadState* m_thread_state;
};

template <class T>
bp::list std_vector_to_py_list(const std::vector<T> &vector) {
  bp::list list;
  for (auto iter = vector.begin(); iter != vector.end(); ++iter) {
    list.append(*iter);
  }
  return list;
}

template <typename T>
inline std::vector<T> to_std_vector(const bp::object &iterable) {
  return std::vector<T>(bp::stl_input_iterator<T>(iterable),
                        bp::stl_input_iterator<T>());
}

std::string VideoIndex_serialize_wrapper(VideoIndex *index) {
  auto serialized_data = index->serialize();
  return std::string(serialized_data.begin(), serialized_data.end());
}

VideoIndex VideoIndex_deserialize_wrapper(const std::string& data) {
  const uint8_t* data_ptr = reinterpret_cast<const uint8_t *>(data.data());
  std::vector<uint8_t> v(data_ptr, data_ptr + data.size());
  return VideoIndex::deserialize(v);
}

bp::list VideoIndex_sample_offsets_wrapper(VideoIndex* index) {
  return std_vector_to_py_list(index->sample_offsets());
}

bp::list VideoIndex_sample_sizes_wrapper(VideoIndex* index) {
  return std_vector_to_py_list(index->sample_sizes());
}

bp::list VideoIndex_keyframe_indices_wrapper(VideoIndex* index) {
  return std_vector_to_py_list(index->keyframe_indices());
}

std::string VideoIndex_metadata_bytes_wrapper(VideoIndex* index) {
  return std::string(index->metadata_bytes().data(),
                     index->metadata_bytes().data() +
                         index->metadata_bytes().size());
}

bp::tuple MP4IndexCreator_feed_wrapper(MP4IndexCreator *indexer,
                                       const std::string data, size_t size) {
  GILRelease r;

  uint64_t next_offset;
  uint64_t next_size;
  bool ret = indexer->feed(reinterpret_cast<const uint8_t *>(data.data()), size,
                           next_offset, next_size);
  return bp::make_tuple(ret, next_offset, next_size);
}

bp::list slice_into_video_intervals_wrapper(const VideoIndex &index,
                                            bp::object l) {
  std::vector<uint64_t> rows = to_std_vector<uint64_t>(l);
  VideoIntervals v = slice_into_video_intervals(index, rows);
  bp::list tups;
  for (size_t i = 0; i < v.sample_index_intervals.size(); ++i) {
    std::vector<uint64_t> ve = v.valid_frames[i];
    const auto& frames = std_vector_to_py_list<uint64_t>(ve);
    tups.append(
        bp::make_tuple(bp::make_tuple(std::get<0>(v.sample_index_intervals[i]),
                                      std::get<1>(v.sample_index_intervals[i])),
                       frames));
  }
  return tups;
}

bp::list EncodedData_sample_offsets_wrapper(DecoderAutomata::EncodedData* data) {
  return std_vector_to_py_list(data->sample_offsets);
}

void EncodedData_sample_offsets_write_wrapper(DecoderAutomata::EncodedData* data,
                                                  bp::object l) {
  data->sample_offsets = to_std_vector<uint64_t>(l); 
}

bp::list EncodedData_sample_sizes_wrapper(DecoderAutomata::EncodedData* data) {
  return std_vector_to_py_list(data->sample_sizes);
}

void EncodedData_sample_sizes_write_wrapper(DecoderAutomata::EncodedData *data, bp::object l) {
  data->sample_sizes = to_std_vector<uint64_t>(l); 
}

bp::list EncodedData_valid_frames_wrapper(DecoderAutomata::EncodedData* data) {
  return std_vector_to_py_list(data->valid_frames);
}

void EncodedData_valid_frames_write_wrapper(DecoderAutomata::EncodedData *data, bp::object l) {
  data->valid_frames = to_std_vector<uint64_t>(l);
}

bp::list EncodedData_keyframes_wrapper(DecoderAutomata::EncodedData *data) {
  return std_vector_to_py_list(data->keyframes);
}

void EncodedData_keyframes_write_wrapper(DecoderAutomata::EncodedData *data, bp::object l) {
  data->keyframes = to_std_vector<uint64_t>(l);
}

std::string EncodedData_encoded_video_wrapper(DecoderAutomata::EncodedData *data) {
  return std::string(data->encoded_video.data(),
                     data->encoded_video.data() + data->encoded_video.size());
}

void EncodedData_encoded_video_write_wrapper(DecoderAutomata::EncodedData *data,
                                             const std::string &v) {
  data->encoded_video = std::vector<uint8_t>(v.data(), v.data() + v.size());
}

void DecoderAutomata_initialize_wrapper(
    DecoderAutomata &dec,
    bp::list l,
    const std::string &extradata) {
  std::vector<DecoderAutomata::EncodedData> encoded_data =
      to_std_vector<DecoderAutomata::EncodedData>(l);
  std::vector<uint8_t> extra(extradata.data(),
                             extradata.data() + extradata.size());
  dec.initialize(encoded_data, extra);
}

bp::list DecoderAutomata_get_frames_wrapper(DecoderAutomata &dec,
                                            const VideoIndex &index,
                                            uint32_t num_frames) {
  size_t frame_size = index.frame_width() * index.frame_height() * 3;
  std::vector<uint8_t> frame_buffer(frame_size * num_frames);
  dec.get_frames(frame_buffer.data(), num_frames);
  bp::list frames;
  for (uint32_t i = 0; i < num_frames; ++i) {
    uint8_t *buffer = (uint8_t*)malloc(frame_size);
    memcpy(buffer,
           frame_buffer.data() +
               i * index.frame_width() * index.frame_height() * 3,
           frame_size);
    frames.append(np::from_data(
        buffer, np::dtype::get_builtin<uint8_t>(),
        bp::make_tuple(index.frame_height(), index.frame_width(), 3),
        bp::make_tuple(index.frame_width() * 3, 3, 1), bp::object()));
  }

  return frames;
}

} // namespace

BOOST_PYTHON_MODULE(libhwang) {
  np::initialize();
  using namespace bp;
  class_<VideoIndex>("VideoIndex", no_init)
      .def("deserialize", VideoIndex_deserialize_wrapper)
      .staticmethod("deserialize")
      .def("serialize", VideoIndex_serialize_wrapper)
      .add_property("timescale", &VideoIndex::timescale)
      .add_property("duration", &VideoIndex::duration)
      .add_property("fps", &VideoIndex::fps)
      .add_property("frame_width", &VideoIndex::frame_width)
      .add_property("frame_height", &VideoIndex::frame_height)
      .add_property("sample_offsets", VideoIndex_sample_offsets_wrapper)
      .add_property("sample_sizes", VideoIndex_sample_sizes_wrapper)
      .add_property("keyframe_indices", VideoIndex_keyframe_indices_wrapper)
      .add_property("metadata_bytes", VideoIndex_metadata_bytes_wrapper);

  class_<MP4IndexCreator>("MP4IndexCreator", init<uint64_t>())
      .def("feed", MP4IndexCreator_feed_wrapper)
      .def("is_done", &MP4IndexCreator::is_done)
      .def("is_error", &MP4IndexCreator::is_error)
      .def("error_message", &MP4IndexCreator::error_message,
           return_value_policy<reference_existing_object>())
      .def("get_video_index", &MP4IndexCreator::get_video_index);
  bp::def("slice_into_video_intervals", slice_into_video_intervals_wrapper);

  enum_<DeviceType>("DeviceType")
      .value("CPU", DeviceType::CPU)
      .value("GPU", DeviceType::GPU);

  class_<DeviceHandle>("DeviceHandle")
      .def_readwrite("type", &DeviceHandle::type)
      .def_readwrite("id", &DeviceHandle::id);

  enum_<VideoDecoderType>("VideoDecoderType")
      .value("SOFTWARE", VideoDecoderType::SOFTWARE)
      .value("NVIDIA", VideoDecoderType::NVIDIA);

  class_<std::vector<uint64_t>>("VecU64").def(
      vector_indexing_suite<std::vector<uint64_t>>());

  class_<std::vector<uint8_t>>("VecU8").def(
      vector_indexing_suite<std::vector<uint8_t>>());

  class_<DecoderAutomata::EncodedData>("EncodedData")
      .add_property("encoded_video", EncodedData_encoded_video_wrapper,
                    EncodedData_encoded_video_write_wrapper)
      .def_readwrite("width", &DecoderAutomata::EncodedData::width)
      .def_readwrite("height", &DecoderAutomata::EncodedData::height)
      .def_readwrite("start_keyframe",
                     &DecoderAutomata::EncodedData::start_keyframe)
      .def_readwrite("end_keyframe",
                     &DecoderAutomata::EncodedData::end_keyframe)
      .add_property("sample_offsets", EncodedData_sample_offsets_wrapper,
                    EncodedData_sample_offsets_write_wrapper)
      .add_property("sample_sizes", EncodedData_sample_sizes_wrapper,
                    EncodedData_sample_sizes_write_wrapper)
      .add_property("keyframes", EncodedData_keyframes_wrapper,
                    EncodedData_keyframes_write_wrapper)
      .add_property("valid_frames", EncodedData_valid_frames_wrapper,
                    EncodedData_valid_frames_write_wrapper);

  class_<std::vector<DecoderAutomata::EncodedData>>("ed").def(
      vector_indexing_suite<std::vector<DecoderAutomata::EncodedData>>());

  class_<DecoderAutomata, boost::noncopyable>(
      "DecoderAutomata", init<DeviceHandle, uint32_t, VideoDecoderType>())
      .def("initialize", DecoderAutomata_initialize_wrapper)
      .def("get_frames", DecoderAutomata_get_frames_wrapper);
}
