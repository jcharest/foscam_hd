#include "ffmpeg_remuxer.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <boost/shared_array.hpp>

namespace {

const size_t VIDEO_BUFFER_SIZE = 4 * 1024;
const size_t VIDEO_PROBE_SIZE = 256 * 1024;
const size_t AUDIO_BUFFER_SIZE = 4 * 1024;

} // namespace

namespace foscam_hd {

FFMpegRemuxerException::FFMpegRemuxerException(const std::string & in_strWhat) : mstrWhat("FFMpegRemuxerException: " + in_strWhat)
{
}

const char* FFMpegRemuxerException::what() const noexcept
{
	return mstrWhat.c_str();
}

FFMpegRemuxer::FFMpegRemuxer(std::unique_ptr<InDataFunctor> && in_VideoFunc, std::unique_ptr<InDataFunctor> && in_AudioFunc,
                             double in_dFramerate, std::unique_ptr<OutStreamFunctor> && in_OutStreamFunc)
 : mdFramerate(in_dFramerate), mfStartThread(false),
   mfStopThread(false), mThread(&FFMpegRemuxer::ThreadRun, this),
   mVideoInputStream(VIDEO_BUFFER_SIZE, move(in_VideoFunc)),
   mAudioInputStream(AUDIO_BUFFER_SIZE, move(in_AudioFunc)),
   mOutputStream(VIDEO_BUFFER_SIZE, move(in_OutStreamFunc))
{
    mfStartThread = true;
}

FFMpegRemuxer::~FFMpegRemuxer()
{
	mfStopThread = true;
	mThread.join();
}

FFMpegRemuxer::Registrator::Registrator()
{
	av_register_all();
}

static int ReadData(void * in_pvOpaque, uint8_t * out_aun8Buffer, int in_nBufferSize)
{
	InDataFunctor * pFunc = reinterpret_cast<InDataFunctor *>(in_pvOpaque);
	return (*pFunc)(out_aun8Buffer, in_nBufferSize);
}

FFMpegRemuxer::InputStreamContext::InputStreamContext(size_t in_BufferSize, std::unique_ptr<InDataFunctor> && in_DataFunc)
 : pAVFormat(nullptr), pAVAvio(nullptr), mDataFunc(move(in_DataFunc))
{
	try
	{
		pAVFormat = avformat_alloc_context();
		if(!pAVFormat)
		{
			throw FFMpegRemuxerException("Failed to allocate avformat context");
		}
		uint8_t * pAVBuffer = reinterpret_cast<uint8_t *>(av_malloc(in_BufferSize));
		if(!pAVBuffer)
		{
			throw FFMpegRemuxerException("Failed to allocate avformat buffer");
		}
		pAVAvio = avio_alloc_context(pAVBuffer, in_BufferSize, 0, mDataFunc.get(), ReadData, nullptr, nullptr);
		if(!pAVAvio)
		{
			throw FFMpegRemuxerException("Failed to allocate avformat avio context");
		}
		pAVFormat->pb = pAVAvio;
	}
	catch(std::exception & Ex)
	{
		Release();
		throw Ex;
	}
}

FFMpegRemuxer::InputStreamContext::~InputStreamContext()
{
	Release();
}

size_t FFMpegRemuxer::InputStreamContext::GetAvailableData()
{
	return mDataFunc->GetAvailableData();
}

void FFMpegRemuxer::InputStreamContext::Release()
{
	avformat_free_context(pAVFormat);
	if(pAVAvio)
	{
		av_freep(&pAVAvio->buffer);
		av_freep(&pAVAvio);
	}
}

FFMpegRemuxer::AudioInputStreamContext::AudioInputStreamContext(size_t in_BufferSize, std::unique_ptr<InDataFunctor> && in_DataFunc)
 : FFMpegRemuxer::InputStreamContext(in_BufferSize, move(in_DataFunc)), pAudioResampler(nullptr), pAudioFifo(nullptr)
{
}

FFMpegRemuxer::AudioInputStreamContext::~AudioInputStreamContext()
{
}

void FFMpegRemuxer::AudioInputStreamContext::Release()
{
	swr_free(&pAudioResampler);
	av_audio_fifo_free(pAudioFifo);
}

static int WriteData(void * in_pvOpaque, uint8_t * in_aun8Buffer, int in_nBufferSize)
{
	OutStreamFunctor * pFunc = reinterpret_cast<OutStreamFunctor *>(in_pvOpaque);
	return (*pFunc)(in_aun8Buffer, in_nBufferSize);
}

FFMpegRemuxer::OutputStreamContext::OutputStreamContext(size_t in_BufferSize, std::unique_ptr<OutStreamFunctor> && in_StreamFunc)
 : pAVFormat(nullptr), pAVAvio(nullptr), pVideoStream(nullptr), pAudioStream(nullptr), mStreamFunc(move(in_StreamFunc))
{
	avformat_alloc_output_context2(&pAVFormat, NULL, NULL, "test.mp4");
	if(!pAVFormat)
	{
		throw FFMpegRemuxerException("Failed to allocate avformat context");
	}

	uint8_t * pAVBuffer = reinterpret_cast<uint8_t *>(av_malloc(in_BufferSize));
	if(!pAVBuffer)
	{
		throw FFMpegRemuxerException("Failed to allocate avformat buffer");
	}
	pAVAvio = avio_alloc_context(pAVBuffer, in_BufferSize, 1, mStreamFunc.get(), nullptr, WriteData, nullptr);
	if(!pAVAvio)
	{
		throw FFMpegRemuxerException("Failed to allocate avformat avio context");
	}
	pAVFormat->pb = pAVAvio;
}

FFMpegRemuxer::OutputStreamContext::~OutputStreamContext()
{
	Release();
}

void FFMpegRemuxer::OutputStreamContext::Release()
{
	avformat_free_context(pAVFormat);

	if(pAVAvio)
	{
		av_freep(&pAVAvio->buffer);
		av_freep(&pAVAvio);
	}
}

void FFMpegRemuxer::ThreadRun()
{
	while(!mfStartThread)
	{
	  std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	bool fOutputHeaderWritten = false;
	while(!mfStopThread)
	{
		if(mVideoInputStream.GetAvailableData() == 0 && mAudioInputStream.GetAvailableData() == 0)
		{
		  std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		if(fOutputHeaderWritten)
		{
			RemuxVideoPacket(mVideoInputStream);
			TranscodeAudioPacket(mAudioInputStream);
		}
		else
		{
			if (mVideoInputStream.GetAvailableData() > VIDEO_PROBE_SIZE)
			{
				CreateVideoStream(mVideoInputStream);
				CreateAudioStream(mAudioInputStream);

				// Set fragmented mp4 options
				AVDictionary * Flags = nullptr;
				av_dict_set(&Flags, "movflags", "empty_moov+default_base_moof+frag_keyframe", 0);

				auto Ret = avformat_write_header(mOutputStream.pAVFormat, &Flags);
				if(Ret < 0)
				{
					throw FFMpegRemuxerException("Failed to write header");
				}
				fOutputHeaderWritten = true;
			}
		}
	}

	if(fOutputHeaderWritten)
	{
		av_write_trailer(mOutputStream.pAVFormat);
	}
}

void FFMpegRemuxer::CreateVideoStream(InputStreamContext & io_pInputStream)
{
	AVInputFormat * pformat = av_find_input_format("h264");
	AVDictionary * Options = nullptr;
	av_dict_set_int(&Options, "probesize2", VIDEO_PROBE_SIZE, 0);
	av_dict_set(&Options, "framerate", std::to_string(mdFramerate).c_str(), 0);

	auto Ret = avformat_open_input(&io_pInputStream.pAVFormat, nullptr, pformat, &Options);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to open avformat input");
	}

	Ret = avformat_find_stream_info(io_pInputStream.pAVFormat, nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to find video stream");
	}

	AVStream * pInStream = io_pInputStream.pAVFormat->streams[0];
	av_dump_format(io_pInputStream.pAVFormat, 0, "Video", 0);

	// Create output stream
	mOutputStream.pVideoStream = avformat_new_stream(mOutputStream.pAVFormat, pInStream->codec->codec);
	if(!mOutputStream.pVideoStream)
	{
		throw FFMpegRemuxerException("Failed to create output video stream");
	}

	Ret = avcodec_copy_context(mOutputStream.pVideoStream->codec, pInStream->codec);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to copy video stream");
	}

	mOutputStream.pVideoStream->codec->codec_tag = 0;
	mOutputStream.pVideoStream->time_base = pInStream->codec->time_base;
}

void FFMpegRemuxer::CreateAudioStream(AudioInputStreamContext & io_pInputStream)
{
	AVInputFormat * pformat = av_find_input_format("s16le");

	auto Ret = avformat_open_input(&io_pInputStream.pAVFormat, nullptr, pformat, nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to open avformat input");
	}

	Ret = avformat_find_stream_info(io_pInputStream.pAVFormat, nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to find audio stream");
	}

	AVStream * pInStream = io_pInputStream.pAVFormat->streams[0];

	// Open decoder
	pInStream->codec->sample_rate = 8000;
	pInStream->codec->channels = 1;
	pInStream->codec->channel_layout = av_get_default_channel_layout(pInStream->codec->channels);
	Ret = avcodec_open2(pInStream->codec, avcodec_find_decoder(pInStream->codec->codec_id), nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to open decoder");
	}

	av_dump_format(io_pInputStream.pAVFormat, 0, "Audio", 0);

	// Create output stream
	mOutputStream.pAudioStream = avformat_new_stream(mOutputStream.pAVFormat, nullptr);
	if(!mOutputStream.pAudioStream)
	{
		throw FFMpegRemuxerException("Failed to create output audio stream");
	}

	mOutputStream.pAudioStream->time_base = pInStream->time_base;
	mOutputStream.pAudioStream->codec->sample_rate = pInStream->codec->sample_rate;
	mOutputStream.pAudioStream->codec->sample_fmt = AV_SAMPLE_FMT_FLT;
	mOutputStream.pAudioStream->codec->channels = pInStream->codec->channels;
	mOutputStream.pAudioStream->codec->channel_layout = pInStream->codec->channel_layout;
	Ret = avcodec_open2(mOutputStream.pAudioStream->codec, avcodec_find_encoder(AV_CODEC_ID_AC3), nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to open encoder");
	}

	io_pInputStream.pAudioResampler = swr_alloc_set_opts(nullptr,
			mOutputStream.pAudioStream->codec->channel_layout,
			mOutputStream.pAudioStream->codec->sample_fmt,
			mOutputStream.pAudioStream->codec->sample_rate,
			pInStream->codec->channel_layout,
			pInStream->codec->sample_fmt,
			pInStream->codec->sample_rate,
			0, nullptr);
	if(io_pInputStream.pAudioResampler == nullptr)
	{
		throw FFMpegRemuxerException("Failed to allocate resampler");
	}

	Ret = swr_init(io_pInputStream.pAudioResampler);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to initialize resampler");
	}

	io_pInputStream.pAudioFifo = av_audio_fifo_alloc(
			mOutputStream.pAudioStream->codec->sample_fmt, mOutputStream.pAudioStream->codec->channels, 1);
	if(io_pInputStream.pAudioFifo == nullptr)
	{
		throw FFMpegRemuxerException("Failed to allocate fifo");
	}
}

struct CAVPacket : public AVPacket
{
	CAVPacket()
	{
		av_init_packet(this);
		data = nullptr;
		size = 0;
	}

	~CAVPacket()
	{
		av_packet_unref(this);
	}
};

void FFMpegRemuxer::RemuxVideoPacket(InputStreamContext & io_pInputStream)
{
	CAVPacket Pkt;
	auto Ret = av_read_frame(io_pInputStream.pAVFormat, &Pkt);
	if (Ret < 0)
	{
		return;
	}

	av_packet_rescale_ts(&Pkt,
			io_pInputStream.pAVFormat->streams[0]->time_base,
			mOutputStream.pVideoStream->time_base);
	Pkt.stream_index = mOutputStream.pVideoStream->index;

	Ret = av_interleaved_write_frame(mOutputStream.pAVFormat, &Pkt);
	if (Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to remux packet");
	}
}

struct AVInputSamplesDeleter {
	void operator()(uint8_t ** p)
	{
		if(p)
		{
			if(&p[0])
			{
				av_freep(&p[0]);
			}

			free(p);
		}
	}
};
typedef std::unique_ptr<uint8_t *, AVInputSamplesDeleter> AVInputSamplesPtr;

void FFMpegRemuxer::TranscodeAudioPacket(AudioInputStreamContext & io_pInputStream)
{
	// Read packet
	CAVPacket DecPkt;
	auto Ret = av_read_frame(io_pInputStream.pAVFormat, &DecPkt);
	if (Ret < 0)
	{
		return;
	}

	// Decode
	AVFramePtr ptrInputFrame(av_frame_alloc());
	if(ptrInputFrame == nullptr)
	{
		throw FFMpegRemuxerException("Failed to allocate frame");
	}
	int nGotFrame = 0;
	Ret = avcodec_decode_audio4(io_pInputStream.pAVFormat->streams[0]->codec, ptrInputFrame.get(), &nGotFrame, &DecPkt);
	if (Ret < 0)
	{
		char szError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(Ret, szError, AV_ERROR_MAX_STRING_SIZE);
		throw FFMpegRemuxerException(std::string("Failed to decode frame: ") + szError);
	}
	if(!nGotFrame)
	{
		return;
	}

	// Allocate Input samples
	int nOutputChannels = mOutputStream.pAudioStream->codec->channels;

	AVInputSamplesPtr ptrInputSamples;
	ptrInputSamples.reset(reinterpret_cast<AVInputSamplesPtr::element_type *>(calloc(nOutputChannels, sizeof(*ptrInputSamples.get()))));
	if(!ptrInputSamples)
	{
		throw FFMpegRemuxerException("Failed to allocate converted input sample pointers");
	}

	int nFrameSize = ptrInputFrame->nb_samples;
	Ret = av_samples_alloc(ptrInputSamples.get(), nullptr, nOutputChannels, nFrameSize,
			mOutputStream.pAudioStream->codec->sample_fmt, 0);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to allocate converted input samples");
	}

	Ret = swr_convert(io_pInputStream.pAudioResampler, ptrInputSamples.get(), nFrameSize,
			const_cast<const uint8_t**>(ptrInputFrame->extended_data), nFrameSize);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to convert input samples");
	}

	Ret = av_audio_fifo_realloc(io_pInputStream.pAudioFifo, av_audio_fifo_size(io_pInputStream.pAudioFifo) + nFrameSize);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to reallocate FIFO");
	}

	Ret = av_audio_fifo_write(io_pInputStream.pAudioFifo, reinterpret_cast<void **>(ptrInputSamples.get()), nFrameSize);
	if(Ret < nFrameSize)
	{
		throw FFMpegRemuxerException("Failed to write data to FIFO");
	}

	int nOutputFrameSize = mOutputStream.pAudioStream->codec->frame_size;
	if(av_audio_fifo_size(io_pInputStream.pAudioFifo) < nOutputFrameSize)
	{
		return;
	}

	AVFramePtr ptrOutputFrame(av_frame_alloc());
	if(ptrOutputFrame == nullptr)
	{
		throw FFMpegRemuxerException("Failed to allocate frame");
	}

	ptrOutputFrame->nb_samples     = nOutputFrameSize;
	ptrOutputFrame->channel_layout = mOutputStream.pAudioStream->codec->channel_layout;
	ptrOutputFrame->format         = mOutputStream.pAudioStream->codec->sample_fmt;
	ptrOutputFrame->sample_rate    = mOutputStream.pAudioStream->codec->sample_rate;

	Ret = av_frame_get_buffer(ptrOutputFrame.get(), 0);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to allocate output frame samples");
	}

	Ret = av_audio_fifo_read(io_pInputStream.pAudioFifo, reinterpret_cast<void **>(ptrOutputFrame->data), nOutputFrameSize);
	if(Ret < nOutputFrameSize)
	{
		throw FFMpegRemuxerException("Failed to read data from FIFO");
	}

	CAVPacket EncPkt;
	nGotFrame = 0;
	Ret = avcodec_encode_audio2(mOutputStream.pAudioStream->codec, &EncPkt,
			ptrOutputFrame.get(), &nGotFrame);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to encode frame");
	}

	if(!nGotFrame)
	{
		return;
	}

	av_packet_rescale_ts(&EncPkt,
			io_pInputStream.pAVFormat->streams[0]->time_base,
			mOutputStream.pAudioStream->time_base);
	EncPkt.stream_index = mOutputStream.pAudioStream->index;

	Ret = av_interleaved_write_frame(mOutputStream.pAVFormat, &EncPkt);
	if (Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to remux packet");
	}
}

} // namespace foscam_hd
