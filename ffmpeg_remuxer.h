#ifndef FFMPEG_REMUXER_H_
#define FFMPEG_REMUXER_H_

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

#include <atomic>
#include <memory>
#include <thread>

struct AVFilterContext;
struct AVFilterGraph;

namespace foscam_hd {

class FFMpegRemuxerException : public std::exception
{
public:
  explicit FFMpegRemuxerException(const std::string & in_strWhat);

	virtual const char* what() const noexcept;

private:
	std::string mstrWhat;
};

class InDataFunctor
{
public:
	InDataFunctor() = default;
	virtual ~InDataFunctor() = default;

	virtual int operator()(uint8_t * out_aun8Buffer, int in_nBufferSize) = 0;
	virtual size_t GetAvailableData() = 0;
};

class OutStreamFunctor
{
public:
	OutStreamFunctor() = default;
	virtual ~OutStreamFunctor() = default;

	virtual int operator()(const uint8_t * in_aun8Buffer, int in_nBufferSize) = 0;
};

class FFMpegRemuxer
{
public:
	FFMpegRemuxer(std::unique_ptr<InDataFunctor> && in_VideoFunc, std::unique_ptr<InDataFunctor> && in_AudioFunc,
	              double in_dFramerate, std::unique_ptr<OutStreamFunctor> && in_OutStreamFunc);
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
    	InputStreamContext(size_t in_BufferSize, std::unique_ptr<InDataFunctor> && in_DataFunc);
    	~InputStreamContext();

    	size_t GetAvailableData();

		AVFormatContext * pAVFormat;
		AVIOContext * pAVAvio;
	private:
		void Release();

		std::unique_ptr<InDataFunctor> mDataFunc;
	};
    class AudioInputStreamContext : public InputStreamContext
	{
    public:
    	AudioInputStreamContext(size_t in_BufferSize, std::unique_ptr<InDataFunctor> && in_DataFunc);
    	~AudioInputStreamContext();

    	SwrContext * pAudioResampler;
    	AVAudioFifo * pAudioFifo;

    private:
		void Release();
	};
    class OutputStreamContext
    {
    public:
    	OutputStreamContext(size_t in_BufferSize, std::unique_ptr<OutStreamFunctor> && in_StreamFunc);
    	~OutputStreamContext();

    	AVFormatContext * pAVFormat;
    	AVIOContext * pAVAvio;
    	AVStream * pVideoStream;
    	AVStream * pAudioStream;

    private:
    	void Release();

    	std::unique_ptr<OutStreamFunctor> mStreamFunc;
    };

    struct AVFrameDeleter {
		void operator()(AVFrame * p)
		{
			av_frame_free(&p);
		}
	};
	typedef std::unique_ptr<AVFrame, AVFrameDeleter> AVFramePtr;

    void ThreadRun();

    void CreateVideoStream(InputStreamContext & io_pInputStream);
    void CreateAudioStream(AudioInputStreamContext & io_pInputStream);

    void RemuxVideoPacket(InputStreamContext & io_pInputStream);
    void TranscodeAudioPacket(AudioInputStreamContext & io_pInputStream);
    void TranscodeAudioPacket(AudioInputStreamContext & io_pInputStream, AVFramePtr & in_ptrFrame);

    double mdFramerate;

    std::atomic<bool> mfStartThread;
    std::atomic<bool> mfStopThread;
    std::thread mThread;

    Registrator mRegistrator;
    InputStreamContext mVideoInputStream;
    AudioInputStreamContext mAudioInputStream;
    OutputStreamContext mOutputStream;

    FFMpegRemuxer(const FFMpegRemuxer & ffmpeg_remuxer) = delete;
    FFMpegRemuxer(FFMpegRemuxer && ffmpeg_remuxer) = delete;
    FFMpegRemuxer & operator=(const FFMpegRemuxer & ffmpeg_remuxer) = delete;
    FFMpegRemuxer & operator=(FFMpegRemuxer && ffmpeg_remuxer) = delete;
};

} // namespace foscam_hd

#endif // FFMPEG_REMUXER_H_
