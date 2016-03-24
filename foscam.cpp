#include "foscam.h"

#include <endian.h>

#include <iostream>
#include <utility>
#include <vector>
#include <boost/fusion/include/define_struct.hpp>
#include <boost/fusion/include/for_each.hpp>
#include <boost/shared_array.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;

namespace foscam_api {

enum class Command : uint32_t {
    VIDEO_ON_REQUEST = 0x00,
    CLOSE_CONNECTION = 0x01,
    AUDIO_ON_REQUEST = 0x02,
    VIDEO_ON_REPLY = 0x10,
    AUDIO_ON_REPLY = 0x12,
    VIDEO_DATA = 0x1a,
    AUDIO_DATA = 0x1b,

#if 0
// User to cam
AUDIO_OFF = 0x03,
SPEAKER_ON = 0x04,
SPEAKER_OFF = 0x05,

TALK_AUDIO_DATA = 0x06,
LOGIN_REQ = 0x0c,
LOGIN_CHECK = 0x0f,

// Cam to user
SPEAKER_ON_REPLY = 0x14,
SPEAKER_OFF_REPLY = 0x15,

LOGIN_CHECK_REPLY = 0x1d,
PTZ_INFO = 0x64,
PRESET_POINT_UNCHANGED = 0x6A,
CRUISES_LIST_CHANGED = 0x6B,
SHOW_MIRROR_FLIP = 0x6C,
SHOW_COLOR_ADJUST_VALUES = 0x6E,
MOTION_DETECTION_ALERT = 0x6F,
SHOWE_POWER_FREQ = 0x70,
STREAM_SELECT_REPLY = 0x71
#endif
};

using Magic = std::integral_constant<uint32_t, 0x43534f46>;
  // FOSC

enum class Videostream : uint8_t {
  MAIN = 0,
  SUB = 1
};

template<size_t N>
struct FixedString {
  static const size_t size = N;

  char str[N];
};

template<size_t N>
struct Reserved {
  static const size_t size = N;
};

}  // namespace foscam_api

BOOST_FUSION_DEFINE_STRUCT(
  (foscam_api), Header,
  (foscam_api::Command, type)
  (foscam_api::Magic, magic)
  (uint32_t, size)
)

BOOST_FUSION_DEFINE_STRUCT(
  (foscam_api),
  VideoOnRequest,
  (foscam_api::Videostream, stream)
  (foscam_api::FixedString<64>, username)
  (foscam_api::FixedString<64>, password)
  (uint32_t, uid)
  (foscam_api::Reserved<28>, reserved)
)

BOOST_FUSION_DEFINE_STRUCT(
  (foscam_api),
  CloseConnection,
  (foscam_api::Reserved<1>, reserved)
  (foscam_api::FixedString<64>, username)
  (foscam_api::FixedString<64>, password)
)

BOOST_FUSION_DEFINE_STRUCT(
  (foscam_api), VideoOnReply,
  (uint8_t, failed)
  (foscam_api::Reserved<35>, reserved)
)

BOOST_FUSION_DEFINE_STRUCT(
  (foscam_api),
  AudioOnRequest,
  (foscam_api::Reserved<1>, reserved0)
  (foscam_api::FixedString<64>, username)
  (foscam_api::FixedString<64>, password)
  (foscam_api::Reserved<32>, reserved1)
)

BOOST_FUSION_DEFINE_STRUCT(
  (foscam_api), AudioOnReply,
  (uint8_t, failed)
  (foscam_api::Reserved<35>, reserved)
)

BOOST_FUSION_DEFINE_STRUCT(
  (foscam_api), AudioDataHeader,
  (foscam_api::Reserved<36>, reserved)
)

namespace endian {

template<class T> T ntoh(T) = delete;
uint32_t ntoh(uint32_t v) { return le32toh(v); }
uint16_t ntoh(uint16_t v) { return le16toh(v); }
uint8_t ntoh(uint8_t v) { return v; }
int8_t ntoh(int8_t v) { return v; }
char ntoh(char v) { return v; }

template<class T> T hton(T) = delete;
uint32_t hton(uint32_t v) { return htole32(v); }
uint16_t hton(uint16_t v) { return htole16(v); }
uint8_t hton(uint8_t v) { return v; }
int8_t hton(int8_t v) { return v; }
char hton(char v) { return v; }

}  // namespace endian

namespace {

const std::string CREATE_CONN_MESSAGE_START =
    "SERVERPUSH / HTTP/1.1\r\nHost: ";
const std::string CREATE_CONN_MESSAGE_END =
  "\r\nAccept:*/*\r\nConnection: Close\r\n\r\n";
const unsigned int MAX_QUEUE_SIZE = 1024 * 1024;

struct Reader {
  mutable asio::const_buffer buf_;

  explicit Reader(asio::const_buffer buf)
      : buf_(std::move(buf)) {
  }

  template<class T>
  auto operator()(T & val) const ->
  typename std::enable_if<std::is_integral<T>::value>::type {
    val = endian::ntoh(*asio::buffer_cast<T const*>(buf_));
    buf_ = buf_ + sizeof(T);
  }

  template<class T>
  auto operator()(T & val) const ->
  typename std::enable_if<std::is_enum<T>::value>::type {
    typename std::underlying_type<T>::type v;
    (*this)(v);
    val = static_cast<T>(v);
  }

  template<class T, T v>
  void operator()(std::integral_constant<T, v>) const {
    typedef std::integral_constant<T, v> type;
    typename type::value_type val;
    (*this)(val);
    if (val != type::value)
      throw foscam_hd::FoscamException("Invalid integral constant.");
  }

  template<size_t N>
  void operator()(foscam_api::Reserved<N>) const {
    buf_ = buf_ + N;
  }

  template<size_t N>
  void operator()(foscam_api::FixedString<N>& val) const {
    for (size_t idx = 0; idx < N; idx++) {
      char v;
      (*this)(v);
      val.str[idx] = v;
    }
  }

  template<class T>
  auto operator()(T & val) const ->
  typename std::enable_if<boost::fusion::traits::is_sequence<T>::value>::type {
    boost::fusion::for_each(val, *this);
  }
};

struct Writer {
  mutable asio::mutable_buffer buf_;

  explicit Writer(asio::mutable_buffer buf)
      : buf_(std::move(buf)) {
  }

  template<class T>
  auto operator()(T const& val) const ->
  typename std::enable_if<std::is_integral<T>::value>::type {
    T tmp = endian::hton(val);
    asio::buffer_copy(buf_, asio::buffer(&tmp, sizeof(T)));
    buf_ = buf_ + sizeof(T);
  }

  template<class T>
  auto operator()(T const& val) const ->
  typename std::enable_if<std::is_enum<T>::value>::type {
    using utype = typename std::underlying_type<T>::type;
    (*this)(static_cast<utype>(val));
  }

  template<class T, T v>
  void operator()(std::integral_constant<T, v>) const {
    typedef std::integral_constant<T, v> type;
    (*this)(type::value);
  }

  template<size_t N>
  void operator()(foscam_api::Reserved<N>) const {
    for (size_t idx = 0; idx < N; idx++) {
      (*this)(static_cast<uint8_t>(0));
    }
  }

  template<size_t N>
  void operator()(foscam_api::FixedString<N> const& val) const {
    for (size_t idx = 0; idx < N; idx++) {
      (*this)(val.str[idx]);
    }
  }

  template<class T>
  auto operator()(T const& val) const ->
  typename std::enable_if<boost::fusion::traits::is_sequence<T>::value>::type {
    boost::fusion::for_each(val, *this);
  }
};

struct Sizer {
  mutable size_t size_ = 0;

  template<class T>
  auto operator()(T const&) const ->
  typename std::enable_if<std::is_integral<T>::value>::type {
    size_ += sizeof(T);
  }

  template<class T>
  auto operator()(T const&) const ->
  typename std::enable_if<std::is_enum<T>::value>::type {
    typename std::underlying_type<T>::type v;
    (*this)(v);
  }

  template<class T, T v>
  void operator()(std::integral_constant<T, v>) const {
    typedef std::integral_constant<T, v> type;
    typename type::value_type val;
    (*this)(val);
  }

  template<size_t N>
  void operator()(foscam_api::Reserved<N>) const {
    size_ += N;
  }

  template<size_t N>
  void operator()(foscam_api::FixedString<N>) const {
    size_ += N;
  }

  template<class T>
  auto operator()(T const& val) const ->
  typename std::enable_if<boost::fusion::traits::is_sequence<T>::value>::type {
    boost::fusion::for_each(val, *this);
  }
};

template<typename T>
std::pair<T, asio::const_buffer> read(asio::const_buffer b) {
  Reader r(std::move(b));
  T res;
  r(res);
  return std::make_pair(res, r.buf_);
}

template<typename T>
asio::mutable_buffer write(asio::mutable_buffer b, T const& val) {
  Writer w(std::move(b));
  w(val);
  return w.buf_;
}

template<class T>
size_t get_size() {
  Sizer s;
  T v;
  s(v);
  return s.size_;
}

class ReadPacketFunc : public foscam_hd::InDataFunctor {
 public:
  explicit ReadPacketFunc(boost::lockfree::spsc_queue<uint8_t> & data_buffer)
      : data_buffer_(data_buffer) {
  }

  int operator()(uint8_t * buffer, int buffer_size) override {
    return data_buffer_.pop(buffer, buffer_size);
  }

  size_t GetAvailableData() override {
    return data_buffer_.read_available();
  }

 private:
  boost::lockfree::spsc_queue<uint8_t> & data_buffer_;
};

class VideoStreamFunc : public foscam_hd::OutStreamFunctor {
 public:
  explicit VideoStreamFunc(boost::lockfree::spsc_queue<uint8_t> & data_buffer)
      : data_buffer_(data_buffer) {
  }

  int operator()(const uint8_t * buffer, int buffer_size) override {
    return data_buffer_.push(buffer, buffer_size);
  }

 private:
  boost::lockfree::spsc_queue<uint8_t> & data_buffer_;
};

}  // namespace

namespace foscam_hd {

FoscamException::FoscamException(const std::string & what)
  : what_("FoscamException: " + what) {
}

const char* FoscamException::what() const noexcept {
  return what_.c_str();
}

Foscam::Foscam(const std::string & host, unsigned int port, unsigned int uid,
               const std::string & user, const std::string & password,
               const int framerate, asio::io_service & io_service)
  : io_service_(io_service),
    socket_(io_service),
    uid_(uid),
    user_(user),
    password_(password),
    framerate_(framerate),
    video_buffer_(MAX_QUEUE_SIZE),
    audio_buffer_(MAX_QUEUE_SIZE),
    video_stream_buffer_(MAX_QUEUE_SIZE),
    remuxer_(std::make_unique<ReadPacketFunc>(video_buffer_),
             std::make_unique<ReadPacketFunc>(audio_buffer_), framerate,
             std::make_unique<VideoStreamFunc>(video_stream_buffer_)) {

  tcp::resolver resolver(io_service_);
  std::string port_str = std::to_string(port);
  asio::connect(socket_, resolver.resolve({ host, port_str }));

  std::string conn_message = CREATE_CONN_MESSAGE_START + host + ":" + port_str
      + CREATE_CONN_MESSAGE_END;
  asio::write(socket_, asio::buffer(conn_message));
}

Foscam::~Foscam() {
}

void Foscam::Connect() {
  ReadHeader();
}

void Foscam::Disconnect() {
  foscam_api::Header header;
  header.type = foscam_api::Command::CLOSE_CONNECTION;
  header.size = get_size<foscam_api::CloseConnection>();
  foscam_api::CloseConnection request;
  strncpy(request.username.str, user_.c_str(), request.username.size);
  strncpy(request.password.str, password_.c_str(), request.password.size);

  auto header_size = get_size<foscam_api::Header>();
  std::vector<uint8_t> message_buf(header_size + header.size);
  write(asio::buffer(message_buf), header);
  write(asio::buffer(message_buf) + header_size, request);

  asio::write(socket_, asio::buffer(message_buf));
}

bool Foscam::VideoOn() {
  std::unique_lock<std::mutex> lock(reply_cond_mutex_);

  foscam_api::Header header;
  header.type = foscam_api::Command::VIDEO_ON_REQUEST;
  header.size = get_size<foscam_api::VideoOnRequest>();
  foscam_api::VideoOnRequest request;
  request.stream = foscam_api::Videostream::MAIN;
  strncpy(request.username.str, user_.c_str(), request.username.size);
  strncpy(request.password.str, password_.c_str(), request.password.size);
  request.uid = uid_;

  auto header_size = get_size<foscam_api::Header>();
  std::vector<uint8_t> message_buf(header_size + header.size);
  write(asio::buffer(message_buf), header);
  write(asio::buffer(message_buf) + header_size, request);

  asio::write(socket_, asio::buffer(message_buf));

  video_on_reply_cond_.wait(lock);

  return true;
}

bool Foscam::AudioOn() {
  std::unique_lock<std::mutex> lock(reply_cond_mutex_);

  foscam_api::Header header;
  header.type = foscam_api::Command::AUDIO_ON_REQUEST;
  header.size = get_size<foscam_api::AudioOnRequest>();
  foscam_api::AudioOnRequest request;
  strncpy(request.username.str, user_.c_str(), request.username.size);
  strncpy(request.password.str, password_.c_str(), request.password.size);

  auto header_size = get_size<foscam_api::Header>();
  std::vector<uint8_t> message_buf(header_size + header.size);
  write(asio::buffer(message_buf), header);
  write(asio::buffer(message_buf) + header_size, request);

  asio::write(socket_, asio::buffer(message_buf));

  audio_on_reply_cond_.wait(lock);

  return true;
}

unsigned int Foscam::GetAvailableVideoStreamData() {
  return video_stream_buffer_.read_available();
}

unsigned int Foscam::GetVideoStreamData(uint8_t * data,
                                        unsigned int data_size) {
  return video_stream_buffer_.pop(data, data_size);
}

void Foscam::ReadHeader() {
  auto self(shared_from_this());
  auto header_size = get_size<foscam_api::Header>();
  auto header_buf = std::make_shared<std::vector<uint8_t> >(header_size);

  asio::async_read(
      socket_,
      asio::buffer(*header_buf),
      [this, self, header_buf](boost::system::error_code ec, std::size_t) {
        if (!ec) {
          foscam_api::Header header;
          asio::const_buffer buf_rest;
          std::tie(header, buf_rest) =
              read<foscam_api::Header>(asio::buffer(*header_buf));

          HandleEvent(header);
        } else {
          socket_.close();
        }
      });
}

void Foscam::HandleEvent(foscam_api::Header header) {
  auto self(shared_from_this());

  switch (header.type) {
    case foscam_api::Command::VIDEO_ON_REPLY: {
      auto reply_buf = std::make_shared<std::vector<uint8_t> >(header.size);

      asio::async_read(
          socket_,
          asio::buffer(*reply_buf),
          [this, self, header, reply_buf](boost::system::error_code ec,
                                          std::size_t) {
            if (!ec) {
              foscam_api::VideoOnReply reply;
              asio::const_buffer buf_rest;
              std::tie(reply, buf_rest) =
                  read<foscam_api::VideoOnReply>(asio::buffer(*reply_buf));

              if (reply.failed) {
                throw FoscamException("Failed to enable video.");
              }

              video_on_reply_cond_.notify_one();

              // Ready for another event
              ReadHeader();
            } else {
              socket_.close();
            }
          });
      break;
    }

    case foscam_api::Command::AUDIO_ON_REPLY: {
      auto reply_buf = std::make_shared<std::vector<uint8_t> >(header.size);

      asio::async_read(
          socket_,
          asio::buffer(*reply_buf),
          [this, self, header, reply_buf](boost::system::error_code ec,
                                          std::size_t) {
            if (!ec) {
              foscam_api::AudioOnReply reply;
              asio::const_buffer buf_rest;
              std::tie(reply, buf_rest) =
                  read<foscam_api::AudioOnReply>(asio::buffer(*reply_buf));

              if (reply.failed) {
                throw FoscamException("Failed to enable video.");
              }

              audio_on_reply_cond_.notify_one();

              // Ready for another event
              ReadHeader();
            } else {
              socket_.close();
            }
          });
      break;
    }


    case foscam_api::Command::VIDEO_DATA: {
      auto video_data_buf =
          std::make_shared<std::vector<uint8_t> >(header.size);

      asio::async_read(
          socket_,
          asio::buffer(*video_data_buf),
          [this, self, video_data_buf](boost::system::error_code ec,
                                       std::size_t) {
            if (!ec) {
              video_buffer_.push(video_data_buf->data(),
                                 video_data_buf->size());

              // Ready for another event
              ReadHeader();
            } else {
              socket_.close();
            }
          });
      break;
    }

    case foscam_api::Command::AUDIO_DATA: {
      auto audio_data_header_buf = std::make_shared<std::vector<uint8_t> >(
          get_size<foscam_api::AudioDataHeader>());
      size_t audio_data_size = header.size - audio_data_header_buf->size();

      asio::async_read(
          socket_,
          asio::buffer(*audio_data_header_buf),
          [this, self, audio_data_header_buf, audio_data_size](
              boost::system::error_code ec, std::size_t) {
            if (!ec) {
              foscam_api::AudioDataHeader audio_data_header;
              asio::const_buffer buf_rest;
              std::tie(audio_data_header, buf_rest) =
                  read<foscam_api::AudioDataHeader>(
                      asio::buffer(*audio_data_header_buf));

              auto audio_data_buf =
                  std::make_shared<std::vector<uint8_t> >(audio_data_size);
              asio::async_read(socket_,
                  asio::buffer(*audio_data_buf),
                  [this, self, audio_data_buf](boost::system::error_code ec,
                                               std::size_t) {
                    if (!ec) {
                      audio_buffer_.push(audio_data_buf->data(),
                                         audio_data_buf->size());

                      // Ready for another event
                      ReadHeader();
                    } else {
                      socket_.close();
                    }
                  });
            } else {
              socket_.close();
            }
          });
      break;
    }

    default: {
      std::cerr << "Unknown header received: " << std::hex
                << static_cast<unsigned int>(header.type) << std::endl;
    }
  }
}

}  // namespace foscam_hd

