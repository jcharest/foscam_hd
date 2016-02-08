#ifndef FFMPEG_REMUXER_H
#define FFMPEG_REMUXER_H

#include <memory>
#include <atomic>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
}

struct AVFilterContext;
struct AVFilterGraph;

class FFMpegRemuxerException : std::exception
{
public:
	FFMpegRemuxerException(const std::string & in_strWhat);

	virtual const char* what() const noexcept;

private:
	std::string mstrWhat;
};

class DataFunctor
{
public:
	DataFunctor() = default;
	virtual ~DataFunctor() = default;

	virtual int operator()(uint8_t * out_aun8Buffer, int in_nBufferSize) = 0;
	virtual size_t GetAvailableData() = 0;
};

class FFMpegRemuxer
{
public:
	struct Framerate
	{
		int nNum;
		int nDen;
	};

	FFMpegRemuxer(std::unique_ptr<DataFunctor> && in_VideoFunc, std::unique_ptr<DataFunctor> && in_AudioFunc, Framerate in_Framerate);
    ~FFMpegRemuxer();

private:
    class Registrator
	{
	public:
    	Registrator();
	};

    class InputStreamContext
	{
	public:
    	InputStreamContext(size_t in_BufferSize, std::unique_ptr<DataFunctor> && in_DataFunc);
    	~InputStreamContext();

    	size_t GetAvailableData();

		AVFormatContext * pAVFormat;
		AVIOContext * pAVAvio;

	private:
		void Release();

		std::unique_ptr<DataFunctor> mDataFunc;
	};
    class AudioInputStreamContext : public InputStreamContext
	{
    public:
    	AudioInputStreamContext(size_t in_BufferSize, std::unique_ptr<DataFunctor> && in_DataFunc);
    	~AudioInputStreamContext();

		AVFilterContext * pBufferSink;
		AVFilterContext * pBufferSrc;
		AVFilterGraph * pFilterGraph;

    private:
		void Release();
	};
    class OutputStreamContext
    {
    public:
    	OutputStreamContext();
    	~OutputStreamContext();

    	AVFormatContext * pAVFormat;
    	AVStream * pVideoStream;
    	AVStream * pAudioStream;

    private:
    	void Release();
    };

    struct AVFrameDeleter {
		void operator()(AVFrame * p) {
			av_frame_free(&p);
		}
	};
	typedef std::unique_ptr<AVFrame, AVFrameDeleter> AVFramePtr;

    void ThreadRun();

    void CreateVideoStream(InputStreamContext & io_pInputStream);
    void CreateAudioStreamFilter(AudioInputStreamContext & io_pInputStream);
    void CreateAudioStream(AudioInputStreamContext & io_pInputStream);

    void RemuxVideoPacket(InputStreamContext & io_pInputStream);
    void TranscodeAudioPacketFilter(AudioInputStreamContext & io_pInputStream);
    void TranscodeAudioPacket(AudioInputStreamContext & io_pInputStream, AVFramePtr & in_ptrFrame);

    Framerate mFramerate;

    std::atomic<bool> mfStartThread;
    std::atomic<bool> mfStopThread;
    std::thread mThread;

    Registrator mRegistrator;
    InputStreamContext mVideoInputStream;
    AudioInputStreamContext mAudioInputStream;
    OutputStreamContext mOutputStream;
};

#endif // FFMPEG_REMUXER_H
