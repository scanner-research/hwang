#include "hwang/video_index.h"

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
}

BOOST_PYTHON_MODULE(libhwang) {
  using namespace bp;
  class_<VideoIndex>("VideoIndex", no_init)
      .def("deserialize", &VideoIndex::serialize)
      .def("serialize", &VideoIndex::serialize);
}
