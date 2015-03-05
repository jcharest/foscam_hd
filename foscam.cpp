#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <fstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <memory>
#include "foscam.h"

using namespace std;

const string CREATE_CONN_MESSAGE_START = "SERVERPUSH / HTTP/1.1\r\nHost: ";
const string CREATE_CONN_MESSAGE_END = "\r\nAccept:*/*\r\nConnection: Close\r\n\r\n";
const char HEADER_MAGIC[] = "FOSC";

enum class Command : uint32_t
{
	VIDEO_ON_REQUEST = 0x00,
	CLOSE_CONNECTION = 0x01,
    AUDIO_ON = 0x02,
    AUDIO_OFF = 0x03,
	VIDEO_ON_REPLY = 0x10,
	AUDIO_ON_REPLY = 0x12,
	VIDEO_DATA = 0x1a,
	AUDIO_DATA = 0x1b,

	// User to cam
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
};

struct __attribute__((__packed__)) Header
{
	Header()
    {
		memset(this, 0, sizeof(*this));
    }

	Header(Command in_Type, unsigned int in_unSize) : Type(in_Type), un32Size(in_unSize)
    {
        memcpy(aun8Magic, HEADER_MAGIC, sizeof(aun8Magic));
    }

	void Check()
	{
		if(memcmp(HEADER_MAGIC, aun8Magic, strlen(HEADER_MAGIC)) != 0)
		{
			throw FoscamException("Invalid header magic.");
		}
	}

	void Check(Command in_ExpectedType, int in_nExpectedSize)
	{
		Check();
		if(Type != in_ExpectedType)
		{
			throw FoscamException("Invalid header type.");
		}
		if(in_nExpectedSize > 0 && un32Size != static_cast<unsigned int>(in_nExpectedSize))
		{
			throw FoscamException("Invalid size.");
		}
	}

	void * GetData()
	{
		return &un32Size + 1;
	}

	Command Type;
    uint8_t aun8Magic[4];
    uint32_t un32Size;
};

struct __attribute__((__packed__)) CloseConnectionRequest : public Header
{
	enum class Videostream : uint8_t
	{
		MAIN = 0,
		SUB = 1
	};

	CloseConnectionRequest(const string & in_strUsername, const string & in_strPassword)
	 : Header(Command::CLOSE_CONNECTION, GetDataSize())
	{
		memset(aun8Reserved0, 0, sizeof(aun8Reserved0));
		memset(szUsername, 0, sizeof(szUsername));
		strncpy(szUsername, in_strUsername.c_str(), sizeof(szUsername) - 1);
		memset(szPassword, 0, sizeof(szPassword));
		strncpy(szPassword, in_strPassword.c_str(), sizeof(szPassword) - 1);
	}

	static constexpr unsigned int GetDataSize()
	{
		return sizeof(CloseConnectionRequest) - sizeof(Header);
	}

	uint8_t aun8Reserved0[1];
	char szUsername[64];
	char szPassword[64];
};

struct __attribute__((__packed__)) VideoOnRequest : public Header
{
	enum class Videostream : uint8_t
	{
		MAIN = 0,
		SUB = 1
	};

	VideoOnRequest(const string & in_strUsername, const string & in_strPassword, unsigned int in_unUID)
	 : Header(Command::VIDEO_ON_REQUEST, GetDataSize()), Stream(Videostream::MAIN), un32UID(in_unUID)
	{
		memset(szUsername, 0, sizeof(szUsername));
		strncpy(szUsername, in_strUsername.c_str(), sizeof(szUsername) - 1);
		memset(szPassword, 0, sizeof(szPassword));
		strncpy(szPassword, in_strPassword.c_str(), sizeof(szPassword) - 1);
		memset(aun8Reserved1, 0, sizeof(aun8Reserved1));
	}

	static constexpr unsigned int GetDataSize()
	{
		return sizeof(VideoOnRequest) - sizeof(Header);
	}

	Videostream Stream;
	char szUsername[64];
	char szPassword[64];
	uint32_t un32UID;
	uint8_t aun8Reserved1[28];
};
struct __attribute__((__packed__)) VideoOnReply : public Header
{
	VideoOnReply(const Header & in_Header)
	 : Header(in_Header), un8Failed(0)
	{
		memset(aun8Reserved0, 0, sizeof(aun8Reserved0));
	}

	static constexpr unsigned int GetDataSize()
	{
		return sizeof(VideoOnReply) - sizeof(Header);
	}

	void CheckHeader()
	{
		Header::Check(Command::VIDEO_ON_REPLY, GetDataSize());
	}

	void CheckData()
	{
		if(un8Failed != 0)
		{
			throw FoscamException("Failed to enable video.");
		}
	}

	uint8_t	un8Failed;
	uint8_t aun8Reserved0[35];
};

struct __attribute__((__packed__)) AudioOnRequest : public Header
{
	AudioOnRequest(const string & in_strUsername, const string & in_strPassword)
	 : Header(Command::AUDIO_ON, GetDataSize())
	{
		memset(aun8Reserved0, 0, sizeof(aun8Reserved0));
		memset(szUsername, 0, sizeof(szUsername));
		strncpy(szUsername, in_strUsername.c_str(), sizeof(szUsername) - 1);
		memset(szPassword, 0, sizeof(szPassword));
		strncpy(szPassword, in_strPassword.c_str(), sizeof(szPassword) - 1);
		memset(aun8Reserved1, 0, sizeof(aun8Reserved1));
	}

	static constexpr unsigned int GetDataSize()
	{
		return sizeof(AudioOnRequest) - sizeof(Header);
	}

	uint8_t aun8Reserved0[1];
	char szUsername[64];
	char szPassword[64];
	uint8_t aun8Reserved1[32];
};
struct __attribute__((__packed__)) AudioOnReply : public Header
{
	AudioOnReply(const Header & in_Header)
	 : Header(in_Header), un8Failed(0)
	{
		memset(aun8Reserved0, 0, sizeof(aun8Reserved0));
	}

	static constexpr unsigned int GetDataSize()
	{
		return sizeof(AudioOnReply) - sizeof(Header);
	}

	void CheckHeader()
	{
		Header::Check(Command::AUDIO_ON_REPLY, GetDataSize());
	}

	void CheckData()
	{
		if(un8Failed != 0)
		{
			throw FoscamException("Failed to enable audio.");
		}
	}

	uint8_t	un8Failed;
	uint8_t aun8Reserved0[35];
};
struct __attribute__((__packed__)) AudioData : public Header
{
	AudioData(const Header & in_Header)
	 : Header(in_Header), un32DataSize(0)
	{
	}

	static constexpr unsigned int GetDataSize()
	{
		return sizeof(AudioData) - sizeof(Header);
	}

	void CheckHeader()
	{
		Header::Check(Command::AUDIO_DATA, -1);
	}

	void CheckData()
	{
		//if(un32DataSize - sizeof(un32DataSize) != un32Size)
		//{
		//	throw FoscamException("Invalid audio data.");
		//}
	}

	uint32_t un32DataSize;
};

template<typename Data>
static int RevcLoop(int in_Sock, Data * in_pData, unsigned int in_unDataLength)
{
	unsigned int unDataRead = 0;
	while(unDataRead < in_unDataLength)
	{
		auto Ret = recv(in_Sock, reinterpret_cast<uint8_t *>(in_pData) + unDataRead, in_unDataLength - unDataRead, 0);
		if(Ret < 0)
		{
			return Ret;
		}
		unDataRead += Ret;
	}

	return unDataRead;
}

FoscamException::FoscamException(const std::string & in_strWhat) : mstrWhat(in_strWhat)
{

}

const char* FoscamException::what() const noexcept
{
	return ("FoscamException" + mstrWhat).c_str();
}

Foscam::Foscam(const string & in_strIPAddress, unsigned int in_unPort, unsigned int in_unUID,
		const std::string & in_strUser, const std::string & in_strPassword)
 : munUID(in_unUID), mstrUser(in_strUser), mstrPassword(in_strPassword), mfStartDataThread(false),
   mfStopDataThread(false), mDataThread(&Foscam::DataThread, this)
{
	struct AddrInfoDeleter {
		void operator()(struct addrinfo* p) {
			if(p)
			{
				freeaddrinfo(p);
			}
		}
	};
	unique_ptr<struct addrinfo, AddrInfoDeleter> ptrServinfo;

	struct addrinfo Hints;
	memset(&Hints, 0, sizeof(Hints));
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;

	// get ready to connect
	{
		struct addrinfo * pServInfo = nullptr;
		auto Status = getaddrinfo(in_strIPAddress.c_str(), to_string(in_unPort).c_str(), &Hints, &pServInfo);
		ptrServinfo.reset(pServInfo);
		pServInfo = nullptr;
		if(Status != 0)
		{
			throw FoscamException(string("Failed to get camera address info: ") + gai_strerror(Status));
		}
	}

    mSock = socket(ptrServinfo->ai_family, ptrServinfo->ai_socktype, ptrServinfo->ai_protocol);;
    if(mSock < 0)
    {
    	throw FoscamException(string("Failed to open operations socket: ") + strerror(errno));
    }

    if(connect(mSock, ptrServinfo->ai_addr, ptrServinfo->ai_addrlen) < 0)
    {
    	throw FoscamException(string("Failed to connect to camera: ") + strerror(errno));
    }

    string strConnMessage = CREATE_CONN_MESSAGE_START + in_strIPAddress + ":" + to_string(in_unPort) + CREATE_CONN_MESSAGE_END;
    if(send(mSock, strConnMessage.c_str(), strConnMessage.size(), 0) < 0 )
    {
    	throw FoscamException(string("Failed to connect to camera (http): ") + strerror(errno));
    }

    mfStartDataThread = true;
}

Foscam::~Foscam()
{
	mfStopDataThread = true;
	mDataThread.join();

	CloseConnectionRequest Request(mstrUser, mstrPassword);
	if(send(mSock, &Request, sizeof(Request), 0) < 0 )
	{
		cerr << "Failed to send close connection request" << endl;
	}

	close(mSock);
}

bool Foscam::VideoOn()
{
	unique_lock<mutex> Lock(mDataThreadMutex);

	VideoOnRequest Request(mstrUser, mstrPassword, munUID);
	if(send(mSock, &Request, sizeof(Request), 0) < 0 )
	{
		throw FoscamException(string("Failed to send video on request: ") + strerror(errno));
	}

	mVideoOnReplyCond.wait(Lock);

	return true;
}

bool Foscam::AudioOn()
{
	unique_lock<mutex> Lock(mDataThreadMutex);

	AudioOnRequest Request(mstrUser, mstrPassword);
	if(send(mSock, &Request, sizeof(Request), 0) < 0 )
	{
		throw FoscamException(string("Failed to send audio on request: ") + strerror(errno));
	}

	mAudioOnReplyCond.wait(Lock);

	return true;
}


unsigned int Foscam::GetVideoDataAvailable()
{
	unique_lock<mutex> Lock(mDataThreadMutex);

	return mVideoBuffer.size();
}

unsigned int Foscam::GetVideoData(uint8_t * in_pData, unsigned int in_unDataLength)
{
	unique_lock<mutex> Lock(mDataThreadMutex);

	auto ReadSize = min(static_cast<unsigned int>(mVideoBuffer.size()), in_unDataLength);
	for(unsigned int unDataIdx; unDataIdx < ReadSize; unDataIdx++)
	{
		*in_pData++ = mVideoBuffer.front();
		mVideoBuffer.pop();
	}

	return ReadSize;
}

void Foscam::DataThread()
{
	while(!mfStartDataThread)
	{
		this_thread::sleep_for(chrono::milliseconds(100));
	}

	ofstream VideoFile("video_file");
	ofstream AudioFile("audio_file");

	while(!mfStopDataThread)
	{
		struct pollfd PollSock;
		memset(&PollSock, 0, sizeof(PollSock));
		PollSock.fd = mSock;
		PollSock.events = POLLIN;

		// Check for data to read
		auto Ret = poll(&PollSock, 1, 100);
		if(Ret < 0)
		{
			throw FoscamException(string("Failed to poll socket: ") + strerror(errno));
		}

		// No data to read
		if(!(PollSock.revents & POLLIN))
		{
			continue;
		}

		Header Event;
		if(RevcLoop(mSock, &Event, sizeof(Event)) < 0 )
		{
			throw FoscamException(string("Failed to receive event header: ") + strerror(errno));
		}
		Event.Check();

		switch(Event.Type)
		{
			case Command::VIDEO_ON_REPLY:
			{
				VideoOnReply Reply = Event;
				Reply.CheckHeader();

				if(RevcLoop(mSock, Reply.GetData(), Reply.un32Size) < 0 )
				{
					throw FoscamException(string("Failed to receive video on response: ") + strerror(errno));
				}
				Reply.CheckData();

				mVideoOnReplyCond.notify_one();
			} break;

			case Command::AUDIO_ON_REPLY:
			{
				AudioOnReply Reply = Event;
				Reply.CheckHeader();

				if(RevcLoop(mSock, Reply.GetData(), Reply.un32Size) < 0 )
				{
					throw FoscamException(string("Failed to receive audio on response: ") + strerror(errno));
				}
				Reply.CheckData();

				mAudioOnReplyCond.notify_one();
			} break;

			case Command::VIDEO_DATA:
			{
				vector<char> VideoData(Event.un32Size);
				if(RevcLoop(mSock, VideoData.data(), Event.un32Size) < 0 )
				{
					throw FoscamException(string("Failed to receive video data: ") + strerror(errno));
				}

				{
					unique_lock<mutex> Lock(mDataThreadMutex);
					for(auto Data: VideoData)
					{
						VideoFile.put(Data);
						mVideoBuffer.push(Data);
					}
				}
			} break;

			case Command::AUDIO_DATA:
			{
				AudioData AudioDataHeader = Event;
				AudioDataHeader.CheckHeader();

				if(RevcLoop(mSock, AudioDataHeader.GetData(), AudioDataHeader.GetDataSize()) < 0 )
				{
					throw FoscamException(string("Failed to receive audio data header: ") + strerror(errno));
				}
				AudioDataHeader.CheckData();

				auto DataSize = AudioDataHeader.un32Size - AudioDataHeader.GetDataSize();
				vector<char> AudioData(DataSize);
				if(RevcLoop(mSock, AudioData.data(), DataSize) < 0 )
				{
					throw FoscamException(string("Failed to receive audio data: ") + strerror(errno));
				}

				{
					unique_lock<mutex> Lock(mDataThreadMutex);
					for(auto Data: AudioData)
					{
						AudioFile.put(Data);
						mAudioBuffer.push(Data);
					}
				}
			} break;

			default:
			{
				cerr << "Unknown header received: " << hex << static_cast<unsigned int>(Event.Type) << endl;
				continue;
			}
		}
	}
}
