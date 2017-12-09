#include "hwang/video_index.h"
#include "hwang/mp4_index_creator.h"

#include <boost/python.hpp>
#include <boost/python/stl_iterator.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>

using namespace hwang;
namespace bp = boost::python;

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

} // namespace

BOOST_PYTHON_MODULE(libhwang) {
  using namespace bp;
  class_<VideoIndex>("VideoIndex", no_init)
      .def("deserialize", VideoIndex_deserialize_wrapper)
      .staticmethod("deserialize")
      .def("serialize", VideoIndex_serialize_wrapper)
      .add_property("sample_offsets", VideoIndex_sample_offsets_wrapper)
      .add_property("sample_sizes", VideoIndex_sample_sizes_wrapper)
      .add_property("keyframe_indices", VideoIndex_keyframe_indices_wrapper);

  class_<MP4IndexCreator>("MP4IndexCreator", init<uint64_t>())
      .def("feed", MP4IndexCreator_feed_wrapper)
      .def("is_done", &MP4IndexCreator::is_done)
      .def("is_error", &MP4IndexCreator::is_error)
      .def("error_message", &MP4IndexCreator::error_message,
           return_value_policy<reference_existing_object>())
      .def("get_video_index", &MP4IndexCreator::get_video_index);
  bp::def("slice_into_video_intervals", slice_into_video_intervals_wrapper);
}
