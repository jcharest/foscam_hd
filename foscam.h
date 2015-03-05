#ifndef FOSCAM_H
#define FOSCAM_H

#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <ip_cam.h>

class FoscamException : std::exception
{
public:
	FoscamException(const std::string & in_strWhat);

	virtual const char* what() const noexcept;

private:
	std::string mstrWhat;
};

class Foscam : public IPCam
{
public:
    Foscam(const std::string & in_strIPAddress, unsigned int in_unPort, unsigned int in_unUID,
    		const std::string & in_strUser, const std::string & in_strPassword);
    ~Foscam();

    virtual bool VideoOn() override;
    virtual bool AudioOn() override;
    unsigned int GetVideoDataAvailable();
    unsigned int GetVideoData(uint8_t * in_pData, unsigned int in_unDataLength);

private:
    void DataThread();

    int mSock;
    unsigned int munUID;
    const std::string mstrUser;
    const std::string mstrPassword;
    std::mutex mDataThreadMutex;
    std::condition_variable mVideoOnReplyCond;
    std::condition_variable mAudioOnReplyCond;
    std::atomic<bool> mfStartDataThread;
    std::atomic<bool> mfStopDataThread;
    std::thread mDataThread;
    std::queue<uint8_t> mVideoBuffer;
    std::queue<uint8_t> mAudioBuffer;
};

#endif // FOSCAM_H
