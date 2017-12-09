#include "hwang/video_index.h"
#include "hwang/mp4_index_creator.h"

#include <boost/python.hpp>

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

std::string VideoIndex_serialize_wrapper(VideoIndex *index) {
  auto serialized_data = index->serialize();
  return std::string(serialized_data.begin(), serialized_data.end());
}

VideoIndex VideoIndex_deserialize_wrapper(const std::string& data) {
  const uint8_t* data_ptr = reinterpret_cast<const uint8_t *>(data.data());
  std::vector<uint8_t> v(data_ptr, data_ptr + data.size());
  return VideoIndex::deserialize(v);
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

} // namespace

BOOST_PYTHON_MODULE(libhwang) {
  using namespace bp;
  class_<VideoIndex>("VideoIndex", no_init)
      .def("deserialize", VideoIndex_deserialize_wrapper)
      .staticmethod("deserialize")
      .def("serialize", VideoIndex_serialize_wrapper);

  class_<MP4IndexCreator>("MP4IndexCreator", init<uint64_t>())
      .def("feed", MP4IndexCreator_feed_wrapper)
      .def("is_done", &MP4IndexCreator::is_done)
      .def("is_error", &MP4IndexCreator::is_error)
      .def("error_message", &MP4IndexCreator::error_message,
           return_value_policy<reference_existing_object>())
      .def("get_video_index", &MP4IndexCreator::get_video_index);
}
