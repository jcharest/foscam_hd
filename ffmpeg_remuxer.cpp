#include "ffmpeg_remuxer.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <boost/shared_array.hpp>

namespace {

const size_t VIDEO_BUFFER_SIZE = 4 * 1024;
const size_t VIDEO_PROBE_SIZE = 256 * 1024;
const size_t AUDIO_BUFFER_SIZE = 4 * 1024;

}  // namespace

namespace foscam_hd {

FFMpegRemuxerException::FFMpegRemuxerException(const std::string & what)
    : what_("FFMpegRemuxerException: " + what) {
}

const char* FFMpegRemuxerException::what() const noexcept {
  return what_.c_str();
}

FFMpegRemuxer::FFMpegRemuxer(
    std::unique_ptr<InDataFunctor> && video_func,
    std::unique_ptr<InDataFunctor> && audio_func, double framerate,
    std::unique_ptr<OutStreamFunctor> && stream_func)
    : framerate_(framerate),
      start_thread_(false),
      stop_thread_(false),
      thread_(&FFMpegRemuxer::ThreadRun, this),
      video_input_stream_(VIDEO_BUFFER_SIZE, move(video_func)),
      audio_input_stream_(AUDIO_BUFFER_SIZE, move(audio_func)),
      output_stream_(VIDEO_BUFFER_SIZE, move(stream_func)) {
  start_thread_ = true;
}

FFMpegRemuxer::~FFMpegRemuxer() {
  stop_thread_ = true;
  thread_.join();
}

FFMpegRemuxer::Registrator::Registrator() {
  av_register_all();
}

namespace {

int ReadData(void * opaque, uint8_t * buffer, int buffer_size) {
  InDataFunctor * pFunc = reinterpret_cast<InDataFunctor *>(opaque);
  return (*pFunc)(buffer, buffer_size);
}

}  // namespace

FFMpegRemuxer::InputStreamContext::InputStreamContext(
    size_t buffer_size, std::unique_ptr<InDataFunctor> && data_func)
    : av_format_(nullptr),
      av_avio_(nullptr),
      data_func_(move(data_func)) {
  try {
    av_format_ = avformat_alloc_context();
    if (!av_format_) {
      throw FFMpegRemuxerException("Failed to allocate avformat context");
    }
    uint8_t * av_buffer = reinterpret_cast<uint8_t *>(av_malloc(buffer_size));
    if (!av_buffer) {
      throw FFMpegRemuxerException("Failed to allocate avformat buffer");
    }
    av_avio_ = avio_alloc_context(av_buffer, buffer_size, 0, data_func_.get(),
                                  ReadData, nullptr, nullptr);
    if (!av_avio_) {
      throw FFMpegRemuxerException("Failed to allocate avformat avio context");
    }
    av_format_->pb = av_avio_;
  } catch (std::exception & ex) {
    Release();
    throw ex;
  }
}

FFMpegRemuxer::InputStreamContext::~InputStreamContext() {
  Release();
}

size_t FFMpegRemuxer::InputStreamContext::GetAvailableData() {
  return data_func_->GetAvailableData();
}

void FFMpegRemuxer::InputStreamContext::Release() {
  avformat_free_context(av_format_);
  if (av_avio_) {
    av_freep(&av_avio_->buffer);
    av_freep(&av_avio_);
  }
}

FFMpegRemuxer::AudioInputStreamContext::AudioInputStreamContext(
    size_t buffer_size, std::unique_ptr<InDataFunctor> && data_func)
    : FFMpegRemuxer::InputStreamContext(buffer_size, move(data_func)),
      audio_resampler_(nullptr),
      audio_fifo_(nullptr) {
}

FFMpegRemuxer::AudioInputStreamContext::~AudioInputStreamContext() {
}

void FFMpegRemuxer::AudioInputStreamContext::Release() {
  swr_free(&audio_resampler_);
  av_audio_fifo_free(audio_fifo_);
}

namespace {

int WriteData(void * opaque, uint8_t * buffer, int buffer_size) {
  OutStreamFunctor * pFunc = reinterpret_cast<OutStreamFunctor *>(opaque);
  return (*pFunc)(buffer, buffer_size);
}

}  // namespace

FFMpegRemuxer::OutputStreamContext::OutputStreamContext(
    size_t buffer_size, std::unique_ptr<OutStreamFunctor> && stream_func)
    : av_format_(nullptr),
      av_avio_(nullptr),
      video_stream_(nullptr),
      audio_stream_(nullptr),
      stream_func_(move(stream_func)) {
  avformat_alloc_output_context2(&av_format_, NULL, NULL, "test.mp4");
  if (!av_format_) {
    throw FFMpegRemuxerException("Failed to allocate avformat context");
  }

  uint8_t * av_buffer = reinterpret_cast<uint8_t *>(av_malloc(buffer_size));
  if (!av_buffer) {
    throw FFMpegRemuxerException("Failed to allocate avformat buffer");
  }
  av_avio_ = avio_alloc_context(av_buffer, buffer_size, 1, stream_func_.get(),
                                nullptr, WriteData, nullptr);
  if (!av_avio_) {
    throw FFMpegRemuxerException("Failed to allocate avformat avio context");
  }
  av_format_->pb = av_avio_;
}

FFMpegRemuxer::OutputStreamContext::~OutputStreamContext() {
  Release();
}

void FFMpegRemuxer::OutputStreamContext::Release() {
  avformat_free_context(av_format_);

  if (av_avio_) {
    av_freep(&av_avio_->buffer);
    av_freep(&av_avio_);
  }
}

void FFMpegRemuxer::ThreadRun() {
  while (!start_thread_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  bool output_header_written = false;
  while (!stop_thread_) {
    if (video_input_stream_.GetAvailableData() == 0
        && audio_input_stream_.GetAvailableData() == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (output_header_written) {
      RemuxVideoPacket(video_input_stream_);
      TranscodeAudioPacket(audio_input_stream_);
    } else {
      if (video_input_stream_.GetAvailableData() > VIDEO_PROBE_SIZE) {
        CreateVideoStream(video_input_stream_);
        CreateAudioStream(audio_input_stream_);

        // Set fragmented mp4 options
        AVDictionary * flags = nullptr;
        av_dict_set(&flags, "movflags",
                    "empty_moov+default_base_moof+frag_keyframe", 0);

        auto ret = avformat_write_header(output_stream_.av_format_, &flags);
        if (ret < 0) {
          throw FFMpegRemuxerException("Failed to write header");
        }
        output_header_written = true;
      }
    }
  }

  if (output_header_written) {
    av_write_trailer(output_stream_.av_format_);
  }
}

void FFMpegRemuxer::CreateVideoStream(InputStreamContext & input_stream) {
  AVInputFormat * format = av_find_input_format("h264");
  AVDictionary * options = nullptr;
  av_dict_set_int(&options, "probesize2", VIDEO_PROBE_SIZE, 0);
  av_dict_set(&options, "framerate", std::to_string(framerate_).c_str(), 0);

  auto ret = avformat_open_input(&input_stream.av_format_, nullptr, format,
                                 &options);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to open avformat input");
  }

  ret = avformat_find_stream_info(input_stream.av_format_, nullptr);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to find video stream");
  }

  AVStream * in_stream = input_stream.av_format_->streams[0];
  av_dump_format(input_stream.av_format_, 0, "Video", 0);

  // Create output stream
  output_stream_.video_stream_ = avformat_new_stream(output_stream_.av_format_,
                                                   in_stream->codec->codec);
  if (!output_stream_.video_stream_) {
    throw FFMpegRemuxerException("Failed to create output video stream");
  }

  ret = avcodec_copy_context(output_stream_.video_stream_->codec,
                             in_stream->codec);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to copy video stream");
  }

  output_stream_.video_stream_->codec->codec_tag = 0;
  output_stream_.video_stream_->time_base = in_stream->codec->time_base;
}

void FFMpegRemuxer::CreateAudioStream(
    AudioInputStreamContext & input_stream) {
  AVInputFormat * format = av_find_input_format("s16le");

  auto ret = avformat_open_input(&input_stream.av_format_, nullptr, format,
                                 nullptr);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to open avformat input");
  }

  ret = avformat_find_stream_info(input_stream.av_format_, nullptr);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to find audio stream");
  }

  AVStream * in_stream = input_stream.av_format_->streams[0];

  // Open decoder
  in_stream->codec->sample_rate = 8000;
  in_stream->codec->channels = 1;
  in_stream->codec->channel_layout = av_get_default_channel_layout(
      in_stream->codec->channels);
  ret = avcodec_open2(in_stream->codec,
                      avcodec_find_decoder(in_stream->codec->codec_id),
                      nullptr);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to open decoder");
  }

  av_dump_format(input_stream.av_format_, 0, "Audio", 0);

  // Create output stream
  output_stream_.audio_stream_ = avformat_new_stream(output_stream_.av_format_,
                                                   nullptr);
  if (!output_stream_.audio_stream_) {
    throw FFMpegRemuxerException("Failed to create output audio stream");
  }

  output_stream_.audio_stream_->time_base = in_stream->time_base;
  output_stream_.audio_stream_->codec->sample_rate =
      in_stream->codec->sample_rate;
  output_stream_.audio_stream_->codec->sample_fmt = AV_SAMPLE_FMT_FLT;
  output_stream_.audio_stream_->codec->channels = in_stream->codec->channels;
  output_stream_.audio_stream_->codec->channel_layout =
      in_stream->codec->channel_layout;
  ret = avcodec_open2(output_stream_.audio_stream_->codec,
                      avcodec_find_encoder(AV_CODEC_ID_AC3), nullptr);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to open encoder");
  }

  input_stream.audio_resampler_ = swr_alloc_set_opts(
      nullptr, output_stream_.audio_stream_->codec->channel_layout,
      output_stream_.audio_stream_->codec->sample_fmt,
      output_stream_.audio_stream_->codec->sample_rate,
      in_stream->codec->channel_layout, in_stream->codec->sample_fmt,
      in_stream->codec->sample_rate, 0, nullptr);
  if (input_stream.audio_resampler_ == nullptr) {
    throw FFMpegRemuxerException("Failed to allocate resampler");
  }

  ret = swr_init(input_stream.audio_resampler_);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to initialize resampler");
  }

  input_stream.audio_fifo_ = av_audio_fifo_alloc(
      output_stream_.audio_stream_->codec->sample_fmt,
      output_stream_.audio_stream_->codec->channels, 1);
  if (input_stream.audio_fifo_ == nullptr) {
    throw FFMpegRemuxerException("Failed to allocate fifo");
  }
}

struct CAVPacket : public AVPacket {
  CAVPacket() {
    av_init_packet(this);
    data = nullptr;
    size = 0;
  }

  ~CAVPacket() {
    av_packet_unref(this);
  }
};

void FFMpegRemuxer::RemuxVideoPacket(InputStreamContext & input_stream) {
  CAVPacket packet;
  auto ret = av_read_frame(input_stream.av_format_, &packet);
  if (ret < 0) {
    return;
  }

  av_packet_rescale_ts(&packet, input_stream.av_format_->streams[0]->time_base,
                       output_stream_.video_stream_->time_base);
  packet.stream_index = output_stream_.video_stream_->index;

  ret = av_interleaved_write_frame(output_stream_.av_format_, &packet);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to remux packet");
  }
}

namespace {

struct AVInputSamplesDeleter {
  void operator()(uint8_t ** p) {
    if (p) {
      if (&p[0]) {
        av_freep(&p[0]);
      }

      free(p);
    }
  }
};
typedef std::unique_ptr<uint8_t *, AVInputSamplesDeleter> AVInputSamplesPtr;

}  // namespace

void FFMpegRemuxer::TranscodeAudioPacket(
    AudioInputStreamContext & input_stream) {

  // Read packet
  CAVPacket decoded_packet;
  auto ret = av_read_frame(input_stream.av_format_, &decoded_packet);
  if (ret < 0) {
    return;
  }

  // Decode
  AVFramePtr input_frame(av_frame_alloc());
  if (input_frame == nullptr) {
    throw FFMpegRemuxerException("Failed to allocate frame");
  }
  int got_fFrame = 0;
  ret = avcodec_decode_audio4(input_stream.av_format_->streams[0]->codec,
                              input_frame.get(), &got_fFrame, &decoded_packet);
  if (ret < 0) {
    char error[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
    throw FFMpegRemuxerException(
        std::string("Failed to decode frame: ") + error);
  }
  if (!got_fFrame) {
    return;
  }

  // Allocate Input samples
  int output_channels = output_stream_.audio_stream_->codec->channels;

  AVInputSamplesPtr input_samples;
  input_samples.reset(
      reinterpret_cast<AVInputSamplesPtr::element_type *>(calloc(
          output_channels, sizeof(*input_samples.get()))));
  if (!input_samples) {
    throw FFMpegRemuxerException(
        "Failed to allocate converted input sample pointers");
  }

  int frame_size = input_frame->nb_samples;
  ret = av_samples_alloc(input_samples.get(), nullptr, output_channels,
                         frame_size,
                         output_stream_.audio_stream_->codec->sample_fmt, 0);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to allocate converted input samples");
  }

  ret = swr_convert(input_stream.audio_resampler_, input_samples.get(),
                    frame_size,
                    const_cast<const uint8_t**>(input_frame->extended_data),
                    frame_size);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to convert input samples");
  }

  ret = av_audio_fifo_realloc(
      input_stream.audio_fifo_,
      av_audio_fifo_size(input_stream.audio_fifo_) + frame_size);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to reallocate FIFO");
  }

  ret = av_audio_fifo_write(input_stream.audio_fifo_,
                            reinterpret_cast<void **>(input_samples.get()),
                            frame_size);
  if (ret < frame_size) {
    throw FFMpegRemuxerException("Failed to write data to FIFO");
  }

  int output_frame_size = output_stream_.audio_stream_->codec->frame_size;
  if (av_audio_fifo_size(input_stream.audio_fifo_) < output_frame_size) {
    return;
  }

  AVFramePtr output_frame(av_frame_alloc());
  if (output_frame == nullptr) {
    throw FFMpegRemuxerException("Failed to allocate frame");
  }

  output_frame->nb_samples = output_frame_size;
  output_frame->channel_layout = output_stream_.audio_stream_->codec
      ->channel_layout;
  output_frame->format = output_stream_.audio_stream_->codec->sample_fmt;
  output_frame->sample_rate = output_stream_.audio_stream_->codec->sample_rate;

  ret = av_frame_get_buffer(output_frame.get(), 0);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to allocate output frame samples");
  }

  ret = av_audio_fifo_read(input_stream.audio_fifo_,
                           reinterpret_cast<void **>(output_frame->data),
                           output_frame_size);
  if (ret < output_frame_size) {
    throw FFMpegRemuxerException("Failed to read data from FIFO");
  }

  CAVPacket encoded_packet;
  got_fFrame = 0;
  ret = avcodec_encode_audio2(output_stream_.audio_stream_->codec,
                              &encoded_packet, output_frame.get(), &got_fFrame);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to encode frame");
  }

  if (!got_fFrame) {
    return;
  }

  av_packet_rescale_ts(&encoded_packet,
                       input_stream.av_format_->streams[0]->time_base,
                       output_stream_.audio_stream_->time_base);
  encoded_packet.stream_index = output_stream_.audio_stream_->index;

  ret = av_interleaved_write_frame(output_stream_.av_format_, &encoded_packet);
  if (ret < 0) {
    throw FFMpegRemuxerException("Failed to remux packet");
  }
}

}  // namespace foscam_hd
