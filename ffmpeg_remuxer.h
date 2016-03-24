#ifndef FFMPEG_REMUXER_H_
#define FFMPEG_REMUXER_H_

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

#include <atomic>
#include <memory>
#include <string>
#include <thread>

struct AVFilterContext;
struct AVFilterGraph;

namespace foscam_hd {

class FFMpegRemuxerException : public std::exception {
 public:
  explicit FFMpegRemuxerException(const std::string & what);

  const char* what() const noexcept override;

 private:
  std::string what_;
};

class InDataFunctor {
 public:
  InDataFunctor() = default;
  virtual ~InDataFunctor() = default;

  virtual int operator()(uint8_t * buffer, int buffer_size) = 0;
  virtual size_t GetAvailableData() = 0;
};

class OutStreamFunctor {
 public:
  OutStreamFunctor() = default;
  virtual ~OutStreamFunctor() = default;

  virtual int operator()(const uint8_t * buffer, int buffer_size) = 0;
};

class FFMpegRemuxer {
 public:
  FFMpegRemuxer(std::unique_ptr<InDataFunctor> && video_func,
                std::unique_ptr<InDataFunctor> && audio_func,
                double framerate,
                std::unique_ptr<OutStreamFunctor> && output_stream_func);
  ~FFMpegRemuxer();

 private:
  class Registrator {
   public:
    Registrator();
  };

  class InputStreamContext {
   public:
    InputStreamContext(size_t buffer_size,
                       std::unique_ptr<InDataFunctor> && data_func);
    ~InputStreamContext();

    size_t GetAvailableData();

    AVFormatContext * av_format_;
    AVIOContext * av_avio_;
   private:
    void Release();

    std::unique_ptr<InDataFunctor> data_func_;
  };

  class AudioInputStreamContext : public InputStreamContext {
   public:
    AudioInputStreamContext(size_t buffer_size,
                            std::unique_ptr<InDataFunctor> && data_func);
    ~AudioInputStreamContext();

    SwrContext * audio_resampler_;
    AVAudioFifo * audio_fifo_;

   private:
    void Release();
  };

  class OutputStreamContext {
   public:
    OutputStreamContext(size_t buffer_size,
                        std::unique_ptr<OutStreamFunctor> && stream_func);
    ~OutputStreamContext();

    AVFormatContext * av_format_;
    AVIOContext * av_avio_;
    AVStream * video_stream_;
    AVStream * audio_stream_;

   private:
    void Release();

    std::unique_ptr<OutStreamFunctor> stream_func_;
  };

  struct AVFrameDeleter {
    void operator()(AVFrame * p) {
      av_frame_free(&p);
    }
  };
  typedef std::unique_ptr<AVFrame, AVFrameDeleter> AVFramePtr;

  void ThreadRun();

  void CreateVideoStream(InputStreamContext & input_stream);
  void CreateAudioStream(AudioInputStreamContext & input_stream);

  void RemuxVideoPacket(InputStreamContext & input_stream);
  void TranscodeAudioPacket(AudioInputStreamContext & input_stream);
  void TranscodeAudioPacket(AudioInputStreamContext & input_stream,
                            AVFramePtr & frame);

  double framerate_;

  std::atomic_bool start_thread_;
  std::atomic_bool stop_thread_;
  std::thread thread_;

  Registrator registrator_;
  InputStreamContext video_input_stream_;
  AudioInputStreamContext audio_input_stream_;
  OutputStreamContext output_stream_;

  FFMpegRemuxer(const FFMpegRemuxer &) = delete;
  FFMpegRemuxer(FFMpegRemuxer &&) = delete;
  FFMpegRemuxer & operator=(const FFMpegRemuxer &) = delete;
  FFMpegRemuxer & operator=(FFMpegRemuxer &&) = delete;
};

}  // namespace foscam_hd

#endif  // FFMPEG_REMUXER_H_
