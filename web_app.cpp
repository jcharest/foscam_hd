#include <web_app.h>

#include <fstream>
#include <iostream>

#include <microhttpd.h>

namespace {

const unsigned int PORT = 8888;

void BufferFile(const std::string & file_path, std::vector<uint8_t> & buffer) {
  std::ifstream file(file_path.c_str(), std::ifstream::binary);
  file.seekg(0, file.end);
  int file_size = file.tellg();
  file.seekg(0, file.beg);
  buffer.resize(file_size);
  file.read(reinterpret_cast<char *>(buffer.data()), file_size);
  if (!file) {
    throw foscam_hd::WebAppException("Failed to read " + file_path);
  }
}

}  // namespace

namespace foscam_hd {

WebAppException::WebAppException(const std::string & what)
    : what_("WebAppException: " + what) {
}

const char* WebAppException::what() const noexcept {
  return what_.c_str();
}

int HandleConnectionCallback(
    void * callback_object, MHD_Connection * connection, const char * url,
    const char * method, const char * version,
    const char *, size_t *, void **) {
  auto app = reinterpret_cast<WebApp *>(callback_object);

  return app->HandleConnection(connection, url, method,
                                version);
}

static ssize_t HandleVideoStreamCallback(void * callback_object, uint64_t position,
                                  char * buffer, size_t max_size) {
  auto stream = reinterpret_cast<Foscam::Stream *>(callback_object);

  return stream->GetVideoStreamData(reinterpret_cast<uint8_t *>(buffer),
                                    max_size);
}

static void FreeVideoStreamCallback(void * callback_object) {
  delete reinterpret_cast<Foscam::Stream *>(callback_object);
}

WebApp::WebApp(std::shared_ptr<Foscam> cam)
    : cam_(cam), http_server_(nullptr) {
  BufferFile("favicon.ico", favicon_);
  BufferFile("video_player.html", video_player_);

  cam_->VideoOn();
  cam_->AudioOn();

  http_server_ = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT, nullptr,
                                  nullptr, HandleConnectionCallback, this,
                                  MHD_OPTION_END);
  if (http_server_ == nullptr) {
    throw WebAppException("Failed to start HTTP server");
  }
}

WebApp::~WebApp() {
  if (http_server_) {
    MHD_stop_daemon(http_server_);
  }
}

int WebApp::HandleConnection(MHD_Connection * connection, const char * url,
                             const char * method, const char * version) {
  // Check method type
  if (std::string(method) != MHD_HTTP_METHOD_GET) {
    return MHD_NO;
  }

  if (url == std::string("/")) {
    return HandleGetBuffer(connection, video_player_, "text/html");
  } else if (url == std::string("/favicon.ico")) {
    return HandleGetBuffer(connection, favicon_, "image/x-icon");
  } else if (url == std::string("/video_stream")) {
    return HandleGetVideoStream(connection);
  }

  return MHD_NO;
}

int WebApp::HandleGetBuffer(struct MHD_Connection * connection,
                            const std::vector<uint8_t> & buffer,
                            const std::string & mime_type) {
  MHD_Response * response = MHD_create_response_from_buffer(
      buffer.size(), const_cast<uint8_t *>(buffer.data()),
      MHD_RESPMEM_PERSISTENT);
  MHD_add_response_header(response, "Content-Type", mime_type.c_str());

  auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);

  return ret;
}

int WebApp::HandleGetVideoStream(struct MHD_Connection * connection) {

  auto stream = cam_->CreateStream();

  MHD_Response * response = MHD_create_response_from_callback(
      MHD_SIZE_UNKNOWN, 16 * 1024, HandleVideoStreamCallback,
      stream.release(), FreeVideoStreamCallback);
  MHD_add_response_header(response, "Content-Type", "video/mp4");

  auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);

  return ret;
}

}  // namespace foscam_hd
