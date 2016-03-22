#include <web_app.h>

#include <fstream>
#include <iostream>

#include <microhttpd.h>

namespace
{

const unsigned int PORT = 8888;

} // namespace

namespace foscam_hd {

WebAppException::WebAppException(const std::string & what) : what_("WebAppException: " + what)
{
}

const char* WebAppException::what() const noexcept
{
	return what_.c_str();
}

int HandleConnectionCallback(void * in_pvClass, MHD_Connection * in_pConnection, const char * in_pszUrl,
		const char * in_pszMethod, const char *in_pszVersion, const char *in_pszUploadData,
		size_t *in_UploadDataSize, void ** out_ppcConnClass)
{
	WebApp * pApp = reinterpret_cast<WebApp *>(in_pvClass);

	return pApp->HandleConnection(in_pConnection, in_pszUrl, in_pszMethod, in_pszVersion);
}

ssize_t HandleVideoStreamCallback(void * in_pvClass, uint64_t in_Pos, char * out_pn8Buffer, size_t in_MaxSize)
{
	WebApp * pApp = reinterpret_cast<WebApp *>(in_pvClass);

	return pApp->HandleVideoStream(reinterpret_cast<uint8_t *>(out_pn8Buffer), in_MaxSize);
}

WebApp::WebApp(std::shared_ptr<foscam_hd::IPCamInterface> cam)
 : cam_(cam), http_server_(nullptr)
{
	BufferFile("favicon.ico", favicon_);
	BufferFile("video_player.html", favicon_);

	cam_->VideoOn();
	cam_->AudioOn();

	http_server_ = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT, nullptr, nullptr,
			HandleConnectionCallback, this, MHD_OPTION_END);
	if(http_server_ == nullptr)
	{
		throw WebAppException("Failed to start HTTP server");
	}
}

WebApp::~WebApp()
{
	if(http_server_)
	{
		MHD_stop_daemon(http_server_);
	}
}
void WebApp::BufferFile(const std::string & in_strFilePath, std::vector<uint8_t> & out_Buffer)
{
	std::ifstream File(in_strFilePath.c_str(), std::ifstream::binary);
	File.seekg (0, File.end);
	int nFileLength = File.tellg();
	File.seekg (0, File.beg);
	out_Buffer.resize(nFileLength);
	File.read(reinterpret_cast<char *>(out_Buffer.data()), nFileLength);
	if(!File)
	{
		throw WebAppException("Failed to read " + in_strFilePath);
	}
}

int WebApp::HandleConnection(MHD_Connection * in_pConnection, const char * in_pszUrl,
    		const char * in_pszMethod, const char *in_pszVersion)
{
  std::cout << in_pszUrl << std::endl;

	// Check method type
	if(std::string(in_pszMethod) != MHD_HTTP_METHOD_GET)
	{
		return MHD_NO;
	}

	if(in_pszUrl == std::string("/"))
	{
		return HandleGetBuffer(in_pConnection, favicon_, "text/html");
	}
	else if(in_pszUrl == std::string("/favicon.ico"))
	{
		return HandleGetBuffer(in_pConnection, favicon_, "image/x-icon");
	}
	else if(in_pszUrl == std::string("/video_stream"))
	{
		return HandleGetVideoStream(in_pConnection);
	}


	return MHD_NO;
}

int WebApp::HandleGetBuffer(struct MHD_Connection * in_pConnection, const std::vector<uint8_t> & in_Buffer,
		const std::string & in_MimeType)
{
	MHD_Response * pResponse = MHD_create_response_from_buffer(in_Buffer.size(), const_cast<uint8_t *>(in_Buffer.data()), MHD_RESPMEM_PERSISTENT);
	MHD_add_response_header(pResponse, "Content-Type", in_MimeType.c_str());

	auto Ret = MHD_queue_response(in_pConnection, MHD_HTTP_OK, pResponse);
	MHD_destroy_response(pResponse);

	return Ret;
}

int WebApp::HandleGetVideoStream(struct MHD_Connection * in_pConnection)
{
	MHD_Response * pResponse = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, 1024, HandleVideoStreamCallback, this, nullptr);
	MHD_add_response_header(pResponse, "Content-Type", "video/mp4");

	auto Ret = MHD_queue_response(in_pConnection, MHD_HTTP_OK, pResponse);
	MHD_destroy_response(pResponse);

	return Ret;
}

ssize_t WebApp::HandleVideoStream(uint8_t * out_pun8Buffer, size_t in_MaxSize)
{
	return cam_->GetVideoStreamData(out_pun8Buffer, in_MaxSize);
}

} // namespace foscam_hd
