#include "foscam.h"

#include <endian.h>

#include <iostream>
#include <sstream>
#include <utility>
#include <vector>
#include <boost/fusion/include/define_struct.hpp>
#include <boost/fusion/include/for_each.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace baio = boost::asio;
namespace bpt = boost::property_tree;

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

using Magic = std::integral_constant<uint32_t, 0x43534f46>;  // FOSC

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
  CloseConnection,
  (foscam_api::Reserved<1>, reserved)
  (foscam_api::FixedString<64>, username)
  (foscam_api::FixedString<64>, password)
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

struct Reader {
  mutable baio::const_buffer buf_;

  explicit Reader(baio::const_buffer buf)
      : buf_(std::move(buf)) {
  }

  template<class T>
  auto operator()(T & val) const ->
  typename std::enable_if<std::is_integral<T>::value>::type {
    val = endian::ntoh(*baio::buffer_cast<T const*>(buf_));
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
  mutable baio::mutable_buffer buf_;

  explicit Writer(baio::mutable_buffer buf)
      : buf_(std::move(buf)) {
  }

  template<class T>
  auto operator()(T const& val) const ->
  typename std::enable_if<std::is_integral<T>::value>::type {
    T tmp = endian::hton(val);
    baio::buffer_copy(buf_, baio::buffer(&tmp, sizeof(T)));
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
T read(baio::const_buffer b) {
  Reader r(std::move(b));
  T res;
  r(res);
  return res;
}

template<typename T>
baio::mutable_buffer write(baio::mutable_buffer b, T const& val) {
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
  explicit ReadPacketFunc(foscam_hd::PipeBuffer & data_buffer)
      : data_buffer_(data_buffer) {
  }

  int operator()(uint8_t * buffer, int buffer_size) override {
    return data_buffer_.wait_and_pop(buffer, buffer_size,
                                     std::chrono::milliseconds(100));
  }

  size_t GetAvailableData() override {
    return data_buffer_.read_available();
  }

 private:
  foscam_hd::PipeBuffer & data_buffer_;
};

class VideoStreamFunc : public foscam_hd::OutStreamFunctor {
 public:
  explicit VideoStreamFunc(foscam_hd::PipeBuffer & data_buffer)
      : data_buffer_(data_buffer) {
  }

  void operator()(const uint8_t * buffer, int buffer_size) override {
    data_buffer_.push(buffer, buffer_size);
  }

 private:
  foscam_hd::PipeBuffer & data_buffer_;
};

template<typename T>
std::vector<uint8_t> PrepareLowLevelCommand(foscam_api::Command type,
    std::function<void(T &)> yield_command_func) {
  foscam_api::Header header;
  header.type = type;
  header.size = get_size<T>();
  T request;
  yield_command_func(request);

  auto header_size = get_size<foscam_api::Header>();
  std::vector<uint8_t> message_buf(header_size + header.size);
  write(baio::buffer(message_buf), header);
  write(baio::buffer(message_buf) + header_size, request);

  return message_buf;
}

void PrepareHTTPRequest(const std::string & method, const std::string & path,
                  const std::string & host, const std::string & port,
                  baio::streambuf & command) {
  std::ostream command_stream(&command);

  command_stream << method << " " << path << " HTTP/1.0\r\n";
  command_stream << "Host: " << host << ":" << port << "\r\n";
  command_stream << "Accept: */*\r\n";
  command_stream << "Connection: close\r\n\r\n";
}

bpt::ptree ReadCgiResponse(baio::ip::tcp::socket & socket)
{
  // Read status line
  baio::streambuf response;
  {
    baio::read_until(socket, response, "\r\n");
    std::istream response_stream(&response);

    std::string http_version;
    response_stream >> http_version;

    unsigned int status_code;
    response_stream >> status_code;

    std::string status_message;
    std::getline(response_stream, status_message);
    if (!response_stream || http_version.substr(0, 5) != "HTTP/")
    {
      throw foscam_hd::FoscamException("Invalid response");
    }
    if (status_code != 200)
    {
      throw foscam_hd::FoscamException("Response returned with status code " +
                                       std::to_string(status_code));
    }
  }

  // Skip headers

  std::stringstream data;
  {
    baio::read_until(socket, response, "\r\n\r\n");
    std::istream response_stream(&response);
    std::string header;
    while (std::getline(response_stream, header) && header != "\r");

    // Write whatever content we already have to output.
    if (response.size() > 0)
      data << &response;
  }

  // Read rest of data
  boost::system::error_code ec;
  do {
    baio::read(socket, response, baio::transfer_at_least(1), ec);
    if (response.size() > 0)
      data << &response;
  } while(!ec);

  // Parse data
  bpt::ptree response_tree;
  bpt::read_xml(data, response_tree);

  return response_tree;
}

bpt::ptree ExecuteCgiCommand(
    const std::string & method, const std::string & path,
    const std::string & host, const std::string & port,
    baio::io_service & io_service) {
  baio::ip::tcp::resolver resolver(io_service);
  baio::ip::tcp::socket socket(io_service);
  baio::connect(socket, resolver.resolve({host, port}));

  baio::streambuf request;
  PrepareHTTPRequest(method, path, host, port, request);

  baio::write(socket, request);
  return ReadCgiResponse(socket);
}

}  // namespace

namespace foscam_hd {

FoscamException::FoscamException(const std::string & what)
  : what_("FoscamException: " + what) {
}

const char* FoscamException::what() const noexcept {
  return what_.c_str();
}

Foscam::Stream::Stream(Foscam & parent, const int framerate)
    : parent_(parent),
      remuxer_(std::make_unique<ReadPacketFunc>(video_buffer_),
               std::make_unique<ReadPacketFunc>(audio_buffer_), framerate,
               std::make_unique<VideoStreamFunc>(video_stream_buffer_))
{
  parent_.active_streams_.insert(this);
}

Foscam::Stream::~Stream()
{
  parent_.active_streams_.erase(this);
}

unsigned int Foscam::Stream::GetVideoStreamData(uint8_t * data,
                                                size_t data_size) {
  return video_stream_buffer_.wait_and_pop(data, data_size,
                                           std::chrono::milliseconds(100));
}

Foscam::Foscam(const std::string & host, unsigned int port, unsigned int uid,
               const std::string & user, const std::string & password,
               baio::io_service & io_service)
    : io_service_(io_service), low_level_api_socket_(io_service),
      host_(host), port_(std::to_string(port)), uid_(uid), user_(user),
      password_(password), framerate_(0) {

  baio::ip::tcp::resolver resolver(io_service_);
  baio::connect(low_level_api_socket_, resolver.resolve({host_, port_}));

  baio::streambuf conn_command;
  PrepareHTTPRequest("SERVERPUSH", "/", host_, port_, conn_command);
  baio::write(low_level_api_socket_, conn_command);

  // Get stream type
  auto response = ExecuteCgiCommand(
      "GET",
      "/cgi-bin/CGIProxy.fcgi?cmd=getMainVideoStreamType&usr=" +
        user_ + "&pwd=" + password_,
      host_, port_, io_service_);
  auto stream_type = response.get<unsigned int>("CGI_Result.streamType");

  // Get framerate for stream type
  response = ExecuteCgiCommand(
      "GET",
      "/cgi-bin/CGIProxy.fcgi?cmd=getVideoStreamParam&usr=" +
        user_ + "&pwd=" + password_,
      host_, port_, io_service_);
  framerate_ = response.get<unsigned int>("CGI_Result.frameRate" +
                                          std::to_string(stream_type));
}

Foscam::~Foscam() {
}

void Foscam::Connect() {
  ReadHeader();
}

void Foscam::Disconnect() {
  auto message_buf = PrepareLowLevelCommand<foscam_api::CloseConnection>(
      foscam_api::Command::CLOSE_CONNECTION,
      [this](foscam_api::CloseConnection & request){
        strncpy(request.username.str, user_.c_str(),
                request.username.size);
        strncpy(request.password.str, password_.c_str(),
                request.password.size);
      });

  baio::write(low_level_api_socket_, baio::buffer(message_buf));
}

bool Foscam::VideoOn() {
  std::unique_lock<std::mutex> lock(reply_cond_mutex_);

  auto message_buf = PrepareLowLevelCommand<foscam_api::VideoOnRequest>(
      foscam_api::Command::VIDEO_ON_REQUEST,
      [this](foscam_api::VideoOnRequest & request){
        strncpy(request.username.str, user_.c_str(),
                request.username.size);
        strncpy(request.password.str, password_.c_str(),
                request.password.size);
        request.uid = uid_;
      });

  baio::write(low_level_api_socket_, baio::buffer(message_buf));

  video_on_reply_cond_.wait(lock);

  return true;
}

bool Foscam::AudioOn() {
  std::unique_lock<std::mutex> lock(reply_cond_mutex_);

  auto message_buf = PrepareLowLevelCommand<foscam_api::AudioOnRequest>(
      foscam_api::Command::AUDIO_ON_REQUEST,
      [this](foscam_api::AudioOnRequest & request){
        strncpy(request.username.str, user_.c_str(),
                request.username.size);
        strncpy(request.password.str, password_.c_str(),
                request.password.size);
      });

  baio::write(low_level_api_socket_, baio::buffer(message_buf));

  audio_on_reply_cond_.wait(lock);

  return true;
}

auto Foscam::CreateStream() -> std::unique_ptr<Stream>
{
  return std::make_unique<Stream>(*this, framerate_);
}

void Foscam::ReadHeader() {
  auto self(shared_from_this());
  auto header_size = get_size<foscam_api::Header>();
  auto header_buf = std::make_shared<std::vector<uint8_t> >(header_size);

  baio::async_read(
      low_level_api_socket_,
      baio::buffer(*header_buf),
      [this, self, header_buf](boost::system::error_code ec, std::size_t) {
        if (!ec) {
          foscam_api::Header header;
          baio::const_buffer buf_rest;
          header = read<foscam_api::Header>(baio::buffer(*header_buf));

          HandleEvent(header);
        } else {
          low_level_api_socket_.close();
        }
      });
}

void Foscam::HandleEvent(foscam_api::Header header) {
  auto self(shared_from_this());

  switch (header.type) {
    case foscam_api::Command::VIDEO_ON_REPLY: {
      auto reply_buf = std::make_shared<std::vector<uint8_t> >(header.size);

      baio::async_read(
          low_level_api_socket_,
          baio::buffer(*reply_buf),
          [this, self, header, reply_buf](boost::system::error_code ec,
                                          std::size_t) {
            if (!ec) {
              foscam_api::VideoOnReply reply;
              baio::const_buffer buf_rest;
              reply = read<foscam_api::VideoOnReply>(baio::buffer(*reply_buf));

              if (reply.failed) {
                throw FoscamException("Failed to enable video.");
              }

              video_on_reply_cond_.notify_one();

              // Ready for another event
              ReadHeader();
            } else {
              low_level_api_socket_.close();
            }
          });
      break;
    }

    case foscam_api::Command::AUDIO_ON_REPLY: {
      auto reply_buf = std::make_shared<std::vector<uint8_t> >(header.size);

      baio::async_read(
          low_level_api_socket_,
          baio::buffer(*reply_buf),
          [this, self, header, reply_buf](boost::system::error_code ec,
                                          std::size_t) {
            if (!ec) {
              foscam_api::AudioOnReply reply;
              reply = read<foscam_api::AudioOnReply>(baio::buffer(*reply_buf));

              if (reply.failed) {
                throw FoscamException("Failed to enable video.");
              }

              audio_on_reply_cond_.notify_one();

              // Ready for another event
              ReadHeader();
            } else {
              low_level_api_socket_.close();
            }
          });
      break;
    }

    case foscam_api::Command::VIDEO_DATA: {
      auto video_data_buf =
          std::make_shared<std::vector<uint8_t> >(header.size);

      baio::async_read(
          low_level_api_socket_,
          baio::buffer(*video_data_buf),
          [this, self, video_data_buf](boost::system::error_code ec,
                                       std::size_t) {
            if (!ec) {
              for (auto stream: active_streams_) {
                stream->video_buffer_.push(video_data_buf->data(),
                                          video_data_buf->size());
              }

              // Ready for another event
              ReadHeader();
            } else {
              low_level_api_socket_.close();
            }
          });
      break;
    }

    case foscam_api::Command::AUDIO_DATA: {
      auto audio_data_header_buf = std::make_shared<std::vector<uint8_t> >(
          get_size<foscam_api::AudioDataHeader>());
      size_t audio_data_size = header.size - audio_data_header_buf->size();

      baio::async_read(
          low_level_api_socket_,
          baio::buffer(*audio_data_header_buf),
          [this, self, audio_data_header_buf, audio_data_size](
              boost::system::error_code ec, std::size_t) {
            if (!ec) {
              foscam_api::AudioDataHeader audio_data_header;
              audio_data_header = read<foscam_api::AudioDataHeader>(
                  baio::buffer(*audio_data_header_buf));

              auto audio_data_buf =
                  std::make_shared<std::vector<uint8_t> >(audio_data_size);
              baio::async_read(low_level_api_socket_,
                               baio::buffer(*audio_data_buf),
                  [this, self, audio_data_buf](boost::system::error_code ec,
                                               std::size_t) {
                    if (!ec) {
                      for (auto stream: active_streams_) {
                        stream->audio_buffer_.push(audio_data_buf->data(),
                                                  audio_data_buf->size());
                      }

                      // Ready for another event
                      ReadHeader();
                    } else {
                      low_level_api_socket_.close();
                    }
                  });
            } else {
              low_level_api_socket_.close();
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

