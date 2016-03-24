#ifndef WEB_APP_H_
#define WEB_APP_H_

#include <memory>
#include <string>
#include <vector>

#include "ip_cam_interface.h"

struct MHD_Daemon;
struct MHD_Connection;

namespace foscam_hd {

class WebAppException : public std::exception {
 public:
  explicit WebAppException(const std::string & what);

  const char* what() const noexcept override;

 private:
  std::string what_;
};

class WebApp {
 public:
  explicit WebApp(std::shared_ptr<foscam_hd::IPCamInterface> cam);
  ~WebApp();

 private:
  void BufferFile(const std::string & file_path,
                  std::vector<uint8_t> & buffer);

  int HandleConnection(struct MHD_Connection * connection,
                       const char * url, const char * method,
                       const char * version);
  int HandleGetBuffer(struct MHD_Connection * connection,
                      const std::vector<uint8_t> & buffer,
                      const std::string & mime_type);
  int HandleGetVideoStream(struct MHD_Connection * connection);
  ssize_t HandleVideoStream(uint8_t * buffer, size_t max_size);

  friend int HandleConnectionCallback(
      void * callback_object, MHD_Connection * connection,
      const char * url, const char * method,
      const char * version, const char *, size_t *, void **);
  friend ssize_t HandleVideoStreamCallback(
      void * callback_object, uint64_t position, char * buffer,
      size_t max_size);

  std::shared_ptr<foscam_hd::IPCamInterface> cam_;
  struct MHD_Daemon * http_server_;
  std::vector<uint8_t> favicon_;
  std::vector<uint8_t> video_player_;

  WebApp(const WebApp &) = delete;
  WebApp(WebApp &&) = delete;
  WebApp & operator=(const WebApp &) = delete;
  WebApp & operator=(WebApp &&) = delete;
};

}  // namespace foscam_hd

#endif  // WEB_APP_H_
