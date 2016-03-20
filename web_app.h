#ifndef WEB_APP_H
#define WEB_APP_H

#include <string>
#include <fstream>
#include <vector>
#include <ip_cam.h>

class WebAppException : std::exception
{
public:
	WebAppException(const std::string & in_strWhat);

	virtual const char* what() const noexcept;

private:
	std::string mstrWhat;
};


struct MHD_Daemon;
struct MHD_Connection;
class WebApp
{
public:
	WebApp(std::shared_ptr<IPCam> cam);
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

    std::shared_ptr<IPCam> cam_;
    struct MHD_Daemon * mpHttpServer;
    std::vector<uint8_t> mFavicon;
    std::vector<uint8_t> mVideoPlayer;
};

#endif // WEB_APP_H
