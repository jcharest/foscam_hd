#ifndef FOSCAM_H_
#define FOSCAM_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include <boost/asio.hpp>

#include "ffmpeg_wrapper.h"
#include "pipe_buffer.h"

namespace foscam_api {
  struct Header;
}

namespace foscam_hd {

class FoscamException : public std::exception {
 public:
  explicit FoscamException(const std::string & what);

  const char* what() const noexcept override;

 private:
  std::string what_;
};

class Foscam : public std::enable_shared_from_this<Foscam> {
 public:
  class Stream {
   public:
    Stream(Foscam & parent, const int framerate, bool audio_on);
    ~Stream();

    unsigned int GetVideoStreamData(uint8_t * data, size_t data_length);

   private:
    friend class Foscam;

    Foscam & parent_;

    PipeBuffer video_buffer_;
    PipeBuffer audio_buffer_;
    PipeBuffer video_stream_buffer_;

    ffmpeg_wrapper::FFMpegWrapper remuxer_;
  };

  Foscam(const std::string & host, unsigned int port, unsigned int uid,
         const std::string & user, const std::string & password,
         boost::asio::io_service & io_service);
  virtual ~Foscam();

  void Connect();
  void Disconnect();
  bool VideoOn();
  bool AudioOn();

  std::unique_ptr<Stream> CreateStream();

 private:
  void ReadHeader();
  void HandleEvent(foscam_api::Header header);

  boost::asio::io_service & io_service_;
  boost::asio::ip::tcp::socket low_level_api_socket_;
  const std::string host_;
  const std::string port_;
  unsigned int uid_;
  const std::string user_;
  const std::string password_;
  int framerate_;
  std::mutex reply_cond_mutex_;
  std::condition_variable video_on_reply_cond_;
  std::condition_variable audio_on_reply_cond_;
  bool audio_on_;

  std::unordered_set<Stream *> active_streams_;

  Foscam(const Foscam &) = delete;
  Foscam(Foscam &&) = delete;
  Foscam & operator=(const Foscam &) = delete;
  Foscam & operator=(Foscam &&) = delete;
};

}  // namespace foscam_hd

#endif  // FOSCAM_H_
