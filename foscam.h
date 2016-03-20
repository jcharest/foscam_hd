#ifndef FOSCAM_H
#define FOSCAM_H

#include <string>
#include <mutex>
#include <condition_variable>
#include <ffmpeg_remuxer.h>
#include <ip_cam.h>

#include <boost/asio.hpp>
#include <boost/lockfree/spsc_queue.hpp>

class FoscamException : public std::exception
{
public:
	FoscamException(const std::string & in_strWhat);

	virtual const char* what() const noexcept override;

private:
	std::string mstrWhat;
};

namespace foscam_api
{
  struct Header;
}

class Foscam : public IPCam, public std::enable_shared_from_this<Foscam>
{
public:
  Foscam(boost::asio::io_service & io_service, const std::string & host, unsigned int port, unsigned int uid,
      const std::string & user, const std::string & password, const int framerate);
  ~Foscam();

  virtual void Connect() override;
  virtual void Disconnect() override;
  virtual bool VideoOn() override;
  virtual bool AudioOn() override;
  virtual unsigned int GetAvailableVideoStreamData() override;
  virtual unsigned int GetVideoStreamData(uint8_t * in_pData, unsigned int in_unDataLength) override;

private:
  void do_read_header();
  void do_handle_event(foscam_api::Header header);

private:
	boost::asio::io_service& io_service_;
	boost::asio::ip::tcp::socket socket_;
  unsigned int uid_;
  const std::string user_;
  const std::string password_;
  const int framerate_;
  std::mutex reply_cond_mutex_;
  std::condition_variable video_on_reply_cond_;
  std::condition_variable audio_on_reply_cond_;
  boost::lockfree::spsc_queue<uint8_t> video_buffer_;
  boost::lockfree::spsc_queue<uint8_t> audio_buffer_;
  boost::lockfree::spsc_queue<uint8_t> video_stream_buffer_;

  FFMpegRemuxer remuxer_;
};

#endif // FOSCAM_H
