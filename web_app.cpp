#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <microhttpd.h>

#include <web_app.h>

const unsigned int PORT = 8888;

using namespace std;

WebAppException::WebAppException(const std::string & in_strWhat) : mstrWhat(in_strWhat)
{
}

const char* WebAppException::what() const noexcept
{
	return ("WebAppException: " + mstrWhat).c_str();
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

WebApp::WebApp(shared_ptr<IPCam> cam)
 : cam_(cam), mpHttpServer(nullptr)
{
	BufferFile("favicon.ico", mFavicon);
	BufferFile("video_player.html", mFavicon);

	cam_->VideoOn();
	cam_->AudioOn();

	mpHttpServer = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT, nullptr, nullptr,
			HandleConnectionCallback, this, MHD_OPTION_END);
	if(mpHttpServer == nullptr)
	{
		throw WebAppException("Failed to start HTTP server");
	}
}

WebApp::~WebApp()
{
	if(mpHttpServer)
	{
		MHD_stop_daemon(mpHttpServer);
	}
}
void WebApp::BufferFile(const string & in_strFilePath, vector<uint8_t> & out_Buffer)
{
	ifstream File(in_strFilePath.c_str(), ifstream::binary);
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
	cout << in_pszUrl << endl;

	// Check method type
	if(string(in_pszMethod) != MHD_HTTP_METHOD_GET)
	{
		return MHD_NO;
	}

	if(in_pszUrl == string("/"))
	{
		return HandleGetBuffer(in_pConnection, mFavicon, "text/html");
	}
	else if(in_pszUrl == string("/favicon.ico"))
	{
		return HandleGetBuffer(in_pConnection, mFavicon, "image/x-icon");
	}
	else if(in_pszUrl == string("/video_stream"))
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
