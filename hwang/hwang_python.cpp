#include "hwang/video_index.h"
#include "hwang/mp4_index_creator.h"
#include "hwang/video_decoder_factory.h"
#include "hwang/decoder_automata.h"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

using namespace hwang;
namespace py = pybind11;

namespace {

std::string VideoIndex_serialize_wrapper(VideoIndex *index) {
  auto serialized_data = index->serialize();
  return std::string(serialized_data.begin(), serialized_data.end());
}

VideoIndex VideoIndex_deserialize_wrapper(const std::string &data) {
  const uint8_t *data_ptr = reinterpret_cast<const uint8_t *>(data.data());
  std::vector<uint8_t> v(data_ptr, data_ptr + data.size());
  return VideoIndex::deserialize(v);
}

std::tuple<bool, uint64_t, uint64_t>
MP4IndexCreator_feed_wrapper(MP4IndexCreator *indexer, const std::string data,
                             size_t size) {
  py::gil_scoped_release release;

  uint64_t next_offset;
  uint64_t next_size;
  bool ret = indexer->feed(reinterpret_cast<const uint8_t *>(data.data()), size,
                           next_offset, next_size);
  return std::make_tuple(ret, next_offset, next_size);
}

std::vector<std::tuple<std::tuple<uint64_t, uint64_t>, std::vector<uint64_t>>>
slice_into_video_intervals_wrapper(const VideoIndex &index,
                                   std::vector<uint64_t> rows) {
  VideoIntervals v = slice_into_video_intervals(index, rows);
  std::vector<std::tuple<std::tuple<uint64_t, uint64_t>, std::vector<uint64_t>>>
      tups;
  for (size_t i = 0; i < v.sample_index_intervals.size(); ++i) {
    std::vector<uint64_t> ve = v.valid_frames[i];
    tups.push_back(std::make_tuple(v.sample_index_intervals[i], ve));
  }
  return tups;
}

std::string
EncodedData_encoded_video_wrapper(DecoderAutomata::EncodedData *data) {
  return std::string(data->encoded_video.data(),
                     data->encoded_video.data() + data->encoded_video.size());
}

void EncodedData_encoded_video_write_wrapper(DecoderAutomata::EncodedData *data,
                                             const std::string &v) {
  data->encoded_video = std::vector<uint8_t>(v.data(), v.data() + v.size());
}

void DecoderAutomata_initialize_wrapper(
    DecoderAutomata &dec,
    const std::vector<DecoderAutomata::EncodedData> &encoded_data,
    const std::vector<uint8_t> &extradata) {
  Result result = dec.initialize(encoded_data, extradata);
  if (!result.ok) {
    throw std::runtime_error(result.message);
  }
}

std::vector<py::array_t<uint8_t>> DecoderAutomata_get_frames_wrapper(
    DecoderAutomata &dec, const VideoIndex &index, uint32_t num_frames) {
  size_t frame_size = index.frame_width() * index.frame_height() * 3;
  std::vector<uint8_t> frame_buffer(frame_size * num_frames);
  Result result = dec.get_frames(frame_buffer.data(), num_frames);
  if (!result.ok) {
    throw std::runtime_error(result.message);
  }
  std::vector<py::array_t<uint8_t>> frames;
  for (uint32_t i = 0; i < num_frames; ++i) {
    uint8_t *buffer = (uint8_t *)malloc(frame_size);
    memcpy(buffer,
           frame_buffer.data() +
               i * index.frame_width() * index.frame_height() * 3,
           frame_size);
    // Pass deallocation responsibility (i.e. ownership) to Python runtime.
    // https://stackoverflow.com/questions/44659924/returning-numpy-arrays-via-pybind11
    py::capsule free_when_done(buffer, [](void* buf) { free(buf); });
    frames.push_back(py::array_t<uint8_t>(
        {(long int)index.frame_height(), (long int)index.frame_width(), 3L},
        {(long int)index.frame_width() * 3, 3L, 1L},
        buffer,
        free_when_done));
  }

  return frames;
}

} // namespace

PYBIND11_MODULE(_python, m) {
  m.doc() = "Hwang C library";
  m.attr("__name__") = "hwang._python";

  py::class_<VideoIndex>(m, "VideoIndex")
      .def_static("deserialize", &VideoIndex_deserialize_wrapper)
      .def("serialize", &VideoIndex_serialize_wrapper)
      .def("timescale", &VideoIndex::timescale)
      .def("duration", &VideoIndex::duration)
      .def("fps", &VideoIndex::fps)
      .def("frame_width", &VideoIndex::frame_width)
      .def("frame_height", &VideoIndex::frame_height)
      .def("frames", &VideoIndex::frames)
      .def("sample_offsets", &VideoIndex::sample_offsets)
      .def("sample_sizes", &VideoIndex::sample_sizes)
      .def("keyframe_indices", &VideoIndex::keyframe_indices)
      .def("metadata_bytes", &VideoIndex::metadata_bytes);

  py::class_<MP4IndexCreator>(m, "MP4IndexCreator")
      .def(py::init<uint64_t>())
      .def("feed", &MP4IndexCreator_feed_wrapper)
      .def("is_done", &MP4IndexCreator::is_done)
      .def("is_error", &MP4IndexCreator::is_error)
      .def("error_message", &MP4IndexCreator::error_message)
      .def("get_video_index", &MP4IndexCreator::get_video_index);

  m.def("slice_into_video_intervals", &slice_into_video_intervals_wrapper);

  py::enum_<DeviceType>(m, "DeviceType", py::arithmetic())
      .value("CPU", DeviceType::CPU)
      .value("GPU", DeviceType::GPU);

  py::class_<DeviceHandle>(m, "DeviceHandle")
      .def(py::init<>())
      .def_readwrite("type", &DeviceHandle::type)
      .def_readwrite("id", &DeviceHandle::id);

  py::enum_<VideoDecoderType>(m, "VideoDecoderType", py::arithmetic())
      .value("SOFTWARE", VideoDecoderType::SOFTWARE)
      .value("NVIDIA", VideoDecoderType::NVIDIA);

  py::class_<DecoderAutomata::EncodedData>(m, "EncodedData")
      .def(py::init<>())
      .def_property("encoded_video", EncodedData_encoded_video_wrapper,
                    EncodedData_encoded_video_write_wrapper)
      .def_readwrite("width", &DecoderAutomata::EncodedData::width)
      .def_readwrite("height", &DecoderAutomata::EncodedData::height)
      .def_readwrite("start_keyframe",
                     &DecoderAutomata::EncodedData::start_keyframe)
      .def_readwrite("end_keyframe",
                     &DecoderAutomata::EncodedData::end_keyframe)
      .def_readwrite("sample_offsets",
                     &DecoderAutomata::EncodedData::sample_offsets)
      .def_readwrite("sample_sizes",
                     &DecoderAutomata::EncodedData::sample_sizes)
      .def_readwrite("keyframes", &DecoderAutomata::EncodedData::keyframes)
      .def_readwrite("valid_frames",
                     &DecoderAutomata::EncodedData::valid_frames);

  py::class_<DecoderAutomata>(m, "DecoderAutomata")
      .def(py::init<DeviceHandle, uint32_t, VideoDecoderType>())
      .def("initialize", &DecoderAutomata_initialize_wrapper)
      .def("get_frames", &DecoderAutomata_get_frames_wrapper);
}
