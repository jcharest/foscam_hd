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

	virtual const char* what() const noexcept override;

private:
	std::string what_;
};

class WebApp {
public:
	explicit WebApp(std::shared_ptr<foscam_hd::IPCamInterface> cam);
  ~WebApp();

private:
    void BufferFile(const std::string & in_strFilePath, std::vector<uint8_t> & out_Buffer);

    int HandleConnection(struct MHD_Connection * in_pConnection, const char * in_pszUrl,
    		const char * in_pszMethod, const char *in_pszVersion);
    int HandleGetBuffer(struct MHD_Connection * in_pConnection, const std::vector<uint8_t> & in_Buffer, const std::string & in_MimeType);
    int HandleGetVideoStream(struct MHD_Connection * in_pConnection);
    ssize_t HandleVideoStream(uint8_t * out_pun8Buffer, size_t in_MaxSize);

    friend int HandleConnectionCallback(void * in_pvClass, MHD_Connection * in_pConnection, const char * in_pszUrl,
    		const char * in_pszMethod, const char *in_pszVersion, const char *in_pszUploadData,
    		size_t *in_UploadDataSize, void ** out_ppcConnClass);
    friend ssize_t HandleVideoStreamCallback(void * in_pvClass, uint64_t in_Pos, char * out_pn8Buffer, size_t in_MaxSize);

    std::shared_ptr<foscam_hd::IPCamInterface> cam_;
    struct MHD_Daemon * http_server_;
    std::vector<uint8_t> favicon_;
    std::vector<uint8_t> video_player_;

    WebApp(const WebApp & web_app) = delete;
    WebApp(WebApp && web_app) = delete;
    WebApp & operator=(const WebApp & web_app) = delete;
    WebApp & operator=(WebApp && web_app) = delete;
};

} // namespace foscam_hd

#endif // WEB_APP_H_
