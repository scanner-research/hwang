/* Copyright 2016 Carnegie Mellon University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hwang/impls/nvidia/nvidia_video_decoder.h"
#include "hwang/impls/nvidia/convert.h"
#include "hwang/util/cuda.h"
#include "hwang/util/queue.h"

#include <cassert>
#include <thread>

#include <cuda.h>
#include <nvcuvid.h>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
}

namespace hwang {

namespace {
// Dummy until we add back the profiler
int32_t now() {
  return 0;
}

typedef struct BSFCompatContext {
    AVBSFContext *ctx;
    int extradata_updated;
} BSFCompatContext;

typedef struct H264BSFContext {
    int32_t  sps_offset;
    int32_t  pps_offset;
    uint8_t  length_size;
    uint8_t  new_idr;
    uint8_t  idr_sps_seen;
    uint8_t  idr_pps_seen;
    int      extradata_parsed;
} H264BSFContext;

}

NVIDIAVideoDecoder::NVIDIAVideoDecoder(int device_id, DeviceType output_type,
                                       CUcontext cuda_context)
  : device_id_(device_id),
    output_type_(output_type),
    cuda_context_(cuda_context),
    streams_(max_mapped_frames_),
    parser_(nullptr),
    decoder_(nullptr),
    frame_queue_read_pos_(0),
    frame_queue_elements_(0),
    last_displayed_frame_(-1) {
  // FOR BITSTREAM FILTERING
  avcodec_register_all();

  codec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec_) {
    fprintf(stderr, "could not find h264 decoder\n");
    exit(EXIT_FAILURE);
  }

  cc_ = avcodec_alloc_context3(codec_);
  if (!cc_) {
    fprintf(stderr, "could not alloc codec context");
    exit(EXIT_FAILURE);
  }

  // cc_->thread_count = thread_count;
  cc_->thread_count = 4;

  if (avcodec_open2(cc_, codec_, NULL) < 0) {
    fprintf(stderr, "could not open codec\n");
    assert(false);
  }
  annexb_ = nullptr;
  // FOR BITSTREAM FILTERING

  CUcontext dummy;

  CUD_CHECK(cuCtxPushCurrent(cuda_context_));
  cudaSetDevice(device_id_);

  for (int i = 0; i < max_mapped_frames_; ++i) {
    cudaStreamCreate(&streams_[i]);
    mapped_frames_[i] = 0;
  }
  for (int32_t i = 0; i < max_output_frames_; ++i) {
    frame_in_use_[i] = false;
    undisplayed_frames_[i] = false;
    invalid_frames_[i] = false;
  }

  CUD_CHECK(cuCtxPopCurrent(&dummy));

  annexb_ = nullptr;
}

NVIDIAVideoDecoder::~NVIDIAVideoDecoder() {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 53, 0)
  avcodec_free_context(&cc_);
#else
  avcodec_close(cc_);
  av_freep(&cc_);
#endif

  CUD_CHECK(cuCtxPushCurrent(cuda_context_));
  cudaSetDevice(device_id_);

  for (int i = 0; i < max_mapped_frames_; ++i) {
    if (mapped_frames_[i] != 0) {
      CUD_CHECK(cuvidUnmapVideoFrame(decoder_, mapped_frames_[i]));
    }
  }

  if (parser_) {
    CUD_CHECK(cuvidDestroyVideoParser(parser_));
  }

  if (decoder_) {
    CUD_CHECK(cuvidDestroyDecoder(decoder_));
  }

  for (int i = 0; i < max_mapped_frames_; ++i) {
    CU_CHECK(cudaStreamDestroy(streams_[i]));
  }

  if (convert_frame_ != nullptr) {
    CU_CHECK(cudaFree(convert_frame_));
  }

  CUcontext dummy;
  CUD_CHECK(cuCtxPopCurrent(&dummy));
  // HACK(apoms): We are only using the primary context right now instead of
  //   allowing the user to specify their own CUcontext. Thus we need to release
  //   the primary context we retained when using the factory function to create
  //   this object (see VideoDecoder::make_from_config).
  CUD_CHECK(cuDevicePrimaryCtxRelease(device_id_));
}

void NVIDIAVideoDecoder::configure(const FrameInfo& metadata,
                                   const std::vector<uint8_t>& extradata) {
  cudaSetDevice(device_id_);
  if (annexb_) {
    av_bitstream_filter_close(annexb_);
  }
  annexb_ = av_bitstream_filter_init("h264_mp4toannexb");

  frame_width_ = metadata.width;
  frame_height_ = metadata.height;
  metadata_packets_ = extradata;

  if (convert_frame_ != nullptr) {
    CU_CHECK(cudaFree(convert_frame_));
  }
  CU_CHECK(cudaMalloc(&convert_frame_, frame_width_ * frame_height_ * 3));

  cc_->extradata = (uint8_t *)malloc(metadata_packets_.size() +
                                     AV_INPUT_BUFFER_PADDING_SIZE);
  memcpy(cc_->extradata, metadata_packets_.data(), metadata_packets_.size());
  cc_->extradata_size = metadata_packets_.size();

  CUcontext dummy;
  CUD_CHECK(cuCtxPushCurrent(cuda_context_));
  cudaSetDevice(device_id_);

  for (int i = 0; i < max_mapped_frames_; ++i) {
    if (mapped_frames_[i] != 0) {
      CUD_CHECK(cuvidUnmapVideoFrame(decoder_, mapped_frames_[i]));
    }
  }

  if (parser_) {
    CUD_CHECK(cuvidDestroyVideoParser(parser_));
  }

  if (decoder_) {
    CUD_CHECK(cuvidDestroyDecoder(decoder_));
  }

  for (int i = 0; i < max_mapped_frames_; ++i) {
    mapped_frames_[i] = 0;
  }
  for (int32_t i = 0; i < max_output_frames_; ++i) {
    frame_in_use_[i] = false;
    undisplayed_frames_[i] = false;
    invalid_frames_[i] = false;
  }
  frame_queue_read_pos_ = 0;
  frame_queue_elements_ = 0;

  CUVIDPARSERPARAMS cuparseinfo = {};
  // cuparseinfo.CodecType = metadata.codec_type;
  cuparseinfo.CodecType = cudaVideoCodec_H264;
  cuparseinfo.ulMaxNumDecodeSurfaces = max_output_frames_;
  cuparseinfo.ulMaxDisplayDelay = 1;
  cuparseinfo.pUserData = this;
  cuparseinfo.pfnSequenceCallback =
      NVIDIAVideoDecoder::cuvid_handle_video_sequence;
  cuparseinfo.pfnDecodePicture =
      NVIDIAVideoDecoder::cuvid_handle_picture_decode;
  cuparseinfo.pfnDisplayPicture =
      NVIDIAVideoDecoder::cuvid_handle_picture_display;

  CUD_CHECK(cuvidCreateVideoParser(&parser_, &cuparseinfo));

  CUVIDDECODECREATEINFO cuinfo = {};
  // cuinfo.CodecType = metadata.codec_type;
  cuinfo.CodecType = cudaVideoCodec_H264;
  // cuinfo.ChromaFormat = metadata.chroma_format;
  cuinfo.ChromaFormat = cudaVideoChromaFormat_420;
  cuinfo.OutputFormat = cudaVideoSurfaceFormat_NV12;

  cuinfo.ulWidth = frame_width_;
  cuinfo.ulHeight = frame_height_;
  cuinfo.ulTargetWidth = cuinfo.ulWidth;
  cuinfo.ulTargetHeight = cuinfo.ulHeight;

  cuinfo.target_rect.left = 0;
  cuinfo.target_rect.top = 0;
  cuinfo.target_rect.right = cuinfo.ulWidth;
  cuinfo.target_rect.bottom = cuinfo.ulHeight;

  cuinfo.ulNumDecodeSurfaces = max_output_frames_;
  cuinfo.ulNumOutputSurfaces = max_mapped_frames_;
  cuinfo.ulCreationFlags = cudaVideoCreate_PreferCUVID;

  cuinfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;

  CUD_CHECK(cuvidCreateDecoder(&decoder_, &cuinfo));

  CUD_CHECK(cuCtxPopCurrent(&dummy));

  size_t pos = 0;
  // while (pos < metadata_packets_.size()) {
  //   int encoded_packet_size =
  //       *reinterpret_cast<int*>(metadata_packets_.data() + pos);
  //   pos += sizeof(int);
  //   uint8_t* encoded_packet = (uint8_t*)(metadata_packets_.data() + pos);
  //   pos += encoded_packet_size;

  //   feed(encoded_packet, encoded_packet_size);
  // }
}

bool NVIDIAVideoDecoder::feed(const uint8_t* encoded_buffer, size_t encoded_size,
                              bool keyframe,
                              bool discontinuity) {
  CUD_CHECK(cuCtxPushCurrent(cuda_context_));
  cudaSetDevice(device_id_);

  if (discontinuity) {
    {
      std::unique_lock<std::mutex> lock(frame_queue_mutex_);
      while (frame_queue_elements_ > 0) {
        const auto& dispinfo = frame_queue_[frame_queue_read_pos_];
        frame_in_use_[dispinfo.picture_index] = false;
        frame_queue_read_pos_ =
            (frame_queue_read_pos_ + 1) % max_output_frames_;
        frame_queue_elements_--;
      }
    }

    CUVIDSOURCEDATAPACKET cupkt = {};
    cupkt.flags |= CUVID_PKT_DISCONTINUITY;
    CUD_CHECK(cuvidParseVideoData(parser_, &cupkt));

    std::unique_lock<std::mutex> lock(frame_queue_mutex_);
    last_displayed_frame_ = -1;
    // Empty queue because we have a new section of frames
    for (int32_t i = 0; i < max_output_frames_; ++i) {
      invalid_frames_[i] = undisplayed_frames_[i];
      undisplayed_frames_[i] = false;
    }
    while (frame_queue_elements_ > 0) {
      const auto& dispinfo = frame_queue_[frame_queue_read_pos_];
      frame_in_use_[dispinfo.picture_index] = false;
      frame_queue_read_pos_ = (frame_queue_read_pos_ + 1) % max_output_frames_;
      frame_queue_elements_--;
    }

    // For bitstream filtering
    if (annexb_) {
      av_bitstream_filter_close(annexb_);
    }

    cc_->extradata = (uint8_t *)malloc(metadata_packets_.size() +
                                       AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(cc_->extradata, metadata_packets_.data(), metadata_packets_.size());
    cc_->extradata_size = metadata_packets_.size();

    annexb_ = av_bitstream_filter_init("h264_mp4toannexb");
    // For bitstream filtering

    CUcontext dummy;
    CUD_CHECK(cuCtxPopCurrent(&dummy));
    return false;
  }

  // BITSTREAM FILTERING
  uint8_t *filtered_buffer = nullptr;
  int32_t filtered_size = 0;
  int err;
  if (encoded_size > 0) {
    err = av_bitstream_filter_filter(annexb_, cc_, NULL, &filtered_buffer,
                                     &filtered_size, encoded_buffer,
                                     encoded_size, keyframe);


    if (err < 0) {
      char err_msg[256];
      av_strerror(err, err_msg, 256);
      LOG(FATAL) << "Error while filtering frame (" +
                        std::to_string(err) + "): " + std::string(err_msg);
    }

    if (keyframe) {
      BSFCompatContext *priv = (BSFCompatContext *)annexb_->priv_data;
      AVBSFContext *priv_ctx = (AVBSFContext *)priv->ctx;
      H264BSFContext *s = (H264BSFContext *)priv_ctx->priv_data;

      uint8_t *extra_buffer = priv_ctx->par_out->extradata;
      size_t extra_size = priv_ctx->par_out->extradata_size;

      int32_t temp_size = filtered_size + extra_size + 3;
      uint8_t *temp_buffer = (uint8_t *)malloc(temp_size);

      temp_buffer[0] = 0;
      temp_buffer[1] = 0;
      temp_buffer[2] = 1;
      memcpy(temp_buffer + 3, extra_buffer, extra_size);
      memcpy(temp_buffer + 3 + extra_size, filtered_buffer, filtered_size);

      free(filtered_buffer);
      filtered_buffer = temp_buffer;
      filtered_size = temp_size;
    }
  }
  // BITSTREAM FILTERING

  CUVIDSOURCEDATAPACKET cupkt = {};
  cupkt.payload_size = filtered_size;
  cupkt.payload = filtered_buffer;
  if (encoded_size == 0) {
    cupkt.flags |= CUVID_PKT_ENDOFSTREAM;
  }

  CUD_CHECK(cuvidParseVideoData(parser_, &cupkt));

  // Feed metadata packets after EOS to reinit decoder
  if (encoded_size == 0) {
    if (annexb_) {
      av_bitstream_filter_close(annexb_);
    }

    cc_->extradata = (uint8_t *)malloc(metadata_packets_.size() +
                                       AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(cc_->extradata, metadata_packets_.data(), metadata_packets_.size());
    cc_->extradata_size = metadata_packets_.size();

    annexb_ = av_bitstream_filter_init("h264_mp4toannexb");
  }

  CUcontext dummy;
  CUD_CHECK(cuCtxPopCurrent(&dummy));

  if (filtered_buffer) {
    free(filtered_buffer);
  }

  return frame_queue_elements_ > 0;
}

bool NVIDIAVideoDecoder::discard_frame() {
  std::unique_lock<std::mutex> lock(frame_queue_mutex_);
  CUD_CHECK(cuCtxPushCurrent(cuda_context_));
  cudaSetDevice(device_id_);

  if (frame_queue_elements_ > 0) {
    const auto& dispinfo = frame_queue_[frame_queue_read_pos_];
    frame_in_use_[dispinfo.picture_index] = false;
    frame_queue_read_pos_ = (frame_queue_read_pos_ + 1) % max_output_frames_;
    frame_queue_elements_--;
  }

  CUcontext dummy;
  CUD_CHECK(cuCtxPopCurrent(&dummy));

  return frame_queue_elements_ > 0;
}

bool NVIDIAVideoDecoder::get_frame(uint8_t* decoded_buffer, size_t decoded_size) {
  auto start = now();
  std::unique_lock<std::mutex> lock(frame_queue_mutex_);
  CUD_CHECK(cuCtxPushCurrent(cuda_context_));
  cudaSetDevice(device_id_);
  if (frame_queue_elements_ > 0) {
    CUVIDPARSERDISPINFO dispinfo = frame_queue_[frame_queue_read_pos_];
    frame_queue_read_pos_ = (frame_queue_read_pos_ + 1) % max_output_frames_;
    frame_queue_elements_--;
    lock.unlock();

    CUVIDPROCPARAMS params = {};
    params.progressive_frame = dispinfo.progressive_frame;
    params.second_field = 0;
    params.top_field_first = dispinfo.top_field_first;

    int mapped_frame_index = dispinfo.picture_index % max_mapped_frames_;
    auto start_map = now();
    unsigned int pitch = 0;
    CUD_CHECK(cuvidMapVideoFrame(decoder_, dispinfo.picture_index,
                                 &mapped_frames_[mapped_frame_index], &pitch,
                                 &params));
    // cuvidMapVideoFrame does not wait for convert kernel to finish so sync
    // TODO(apoms): make this an event insertion and have the async 2d memcpy
    //              depend on the event
    // if (profiler_) {
    //   profiler_->add_interval("map_frame", start_map, now());
    // }
    CUdeviceptr mapped_frame = mapped_frames_[mapped_frame_index];
    CU_CHECK(convertNV12toRGBA(reinterpret_cast<const uint8_t *>(mapped_frame),
                               pitch, convert_frame_, frame_width_ * 3,
                               frame_width_, frame_height_, 0));
    CU_CHECK(cudaMemcpy(decoded_buffer, convert_frame_,
                        frame_width_ * frame_height_ * 3, cudaMemcpyDefault));

    CUD_CHECK(
        cuvidUnmapVideoFrame(decoder_, mapped_frames_[mapped_frame_index]));
    mapped_frames_[mapped_frame_index] = 0;

    std::unique_lock<std::mutex> lock(frame_queue_mutex_);
    frame_in_use_[dispinfo.picture_index] = false;
  }

  CUcontext dummy;
  CUD_CHECK(cuCtxPopCurrent(&dummy));

  // if (profiler_) {
  //   profiler_->add_interval("get_frame", start, now());
  // }

  return frame_queue_elements_;
}

int NVIDIAVideoDecoder::decoded_frames_buffered() {
  return frame_queue_elements_;
}

void NVIDIAVideoDecoder::wait_until_frames_copied() {}

int NVIDIAVideoDecoder::cuvid_handle_video_sequence(void* opaque,
                                                    CUVIDEOFORMAT* format) {
  NVIDIAVideoDecoder& decoder = *reinterpret_cast<NVIDIAVideoDecoder*>(opaque);
  return 1;
}

int NVIDIAVideoDecoder::cuvid_handle_picture_decode(void* opaque,
                                                    CUVIDPICPARAMS* picparams) {
  NVIDIAVideoDecoder& decoder = *reinterpret_cast<NVIDIAVideoDecoder*>(opaque);

  int mapped_frame_index = picparams->CurrPicIdx;
  while (decoder.frame_in_use_[picparams->CurrPicIdx]) {
    usleep(500);
  };
  std::unique_lock<std::mutex> lock(decoder.frame_queue_mutex_);
  decoder.undisplayed_frames_[picparams->CurrPicIdx] = true;

  CUresult result = cuvidDecodePicture(decoder.decoder_, picparams);
  CUD_CHECK(result);

  return result == CUDA_SUCCESS;
}

int NVIDIAVideoDecoder::cuvid_handle_picture_display(
    void* opaque, CUVIDPARSERDISPINFO* dispinfo) {
  NVIDIAVideoDecoder& decoder = *reinterpret_cast<NVIDIAVideoDecoder*>(opaque);
  if (!decoder.invalid_frames_[dispinfo->picture_index]) {
    {
      std::unique_lock<std::mutex> lock(decoder.frame_queue_mutex_);
      decoder.frame_in_use_[dispinfo->picture_index] = true;
    }
    while (true) {
      std::unique_lock<std::mutex> lock(decoder.frame_queue_mutex_);
      if (decoder.frame_queue_elements_ < max_output_frames_) {
        int write_pos =
            (decoder.frame_queue_read_pos_ + decoder.frame_queue_elements_) %
            max_output_frames_;
        decoder.frame_queue_[write_pos] = *dispinfo;
        decoder.frame_queue_elements_++;
        decoder.last_displayed_frame_++;
        break;
      }
      usleep(1000);
    }
  } else {
    std::unique_lock<std::mutex> lock(decoder.frame_queue_mutex_);
    decoder.invalid_frames_[dispinfo->picture_index] = false;
  }
  std::unique_lock<std::mutex> lock(decoder.frame_queue_mutex_);
  decoder.undisplayed_frames_[dispinfo->picture_index] = false;
  return true;
}

}
