#include <iostream>

extern "C" {
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

#include "ffmpeg_remuxer.h"

using namespace std;

static const size_t VIDEO_BUFFER_SIZE = 4 * 1024;
static const size_t VIDEO_PROBE_SIZE = 256 * 1024;

static const size_t AUDIO_BUFFER_SIZE = 1 * 1024;


FFMpegRemuxerException::FFMpegRemuxerException(const std::string & in_strWhat) : mstrWhat(in_strWhat)
{
}

const char* FFMpegRemuxerException::what() const noexcept
{
	return ("FFMpegRemuxerException" + mstrWhat).c_str();
}

FFMpegRemuxer::FFMpegRemuxer(unique_ptr<DataFunctor> && in_VideoFunc, unique_ptr<DataFunctor> && in_AudioFunc, Framerate in_Framerate)
 : mFramerate(in_Framerate), mfStartThread(false),
   mfStopThread(false), mThread(&FFMpegRemuxer::ThreadRun, this),
   mVideoInputStream(VIDEO_BUFFER_SIZE, move(in_VideoFunc)),
   mAudioInputStream(AUDIO_BUFFER_SIZE, move(in_AudioFunc))
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
	avfilter_register_all();
}

static int ReadData(void * in_pvOpaque, uint8_t * out_aun8Buffer, int in_nBufferSize)
{
	DataFunctor * pFunc = reinterpret_cast<DataFunctor *>(in_pvOpaque);
	return (*pFunc)(out_aun8Buffer, in_nBufferSize);
}

FFMpegRemuxer::InputStreamContext::InputStreamContext(size_t in_BufferSize, std::unique_ptr<DataFunctor> && in_DataFunc)
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
	catch(exception & Ex)
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

FFMpegRemuxer::AudioInputStreamContext::AudioInputStreamContext(size_t in_BufferSize, std::unique_ptr<DataFunctor> && in_DataFunc)
 : FFMpegRemuxer::InputStreamContext(in_BufferSize, move(in_DataFunc)), pBufferSink(nullptr), pBufferSrc(nullptr), pFilterGraph(nullptr)
{
}

FFMpegRemuxer::AudioInputStreamContext::~AudioInputStreamContext()
{
}

void FFMpegRemuxer::AudioInputStreamContext::Release()
{
	avfilter_graph_free(&pFilterGraph);
}

FFMpegRemuxer::OutputStreamContext::OutputStreamContext()
 : pAVFormat(nullptr), pVideoStream(nullptr), pAudioStream(nullptr)
{
	avformat_alloc_output_context2(&pAVFormat, NULL, NULL, "test.mkv");
	if(!pAVFormat)
	{
		throw FFMpegRemuxerException("Failed to allocate avformat context");
	}

	// Set fragmented mp4 options
	av_opt_set(pAVFormat, "movflags", "+empty_moov", 0);
	av_opt_set(pAVFormat, "movflags", "+frag_keyframe", 0);

	auto Ret = avio_open(&pAVFormat->pb, "test.mkv", AVIO_FLAG_WRITE);
	if(Ret < 0)
	{
		Release();
		throw FFMpegRemuxerException("Failed to open output file");
	}
}

FFMpegRemuxer::OutputStreamContext::~OutputStreamContext()
{
	Release();
}

void FFMpegRemuxer::OutputStreamContext::Release()
{
	if(pAVFormat)
	{
		avio_closep(&pAVFormat->pb);
		avformat_free_context(pAVFormat);
	}
}

void FFMpegRemuxer::ThreadRun()
{
	while(!mfStartThread)
	{
		this_thread::sleep_for(chrono::milliseconds(100));
	}

	bool fOutputHeaderWritten = false;
	while(!mfStopThread)
	{
		if(mVideoInputStream.GetAvailableData() == 0 && mAudioInputStream.GetAvailableData() == 0)
		{
			this_thread::sleep_for(chrono::milliseconds(10));
		}

		if(fOutputHeaderWritten)
		{
			RemuxVideoPacket(mVideoInputStream);
			TranscodeAudioPacketFilter(mAudioInputStream);
		}
		else
		{
			if (mVideoInputStream.GetAvailableData() > VIDEO_PROBE_SIZE)
			{
				CreateVideoStream(mVideoInputStream);
				CreateAudioStream(mAudioInputStream);

				auto Ret = avformat_write_header(mOutputStream.pAVFormat, nullptr);
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
	pInStream->skip_to_keyframe = true;
	pInStream->codec->time_base.num = mFramerate.nNum;
	pInStream->codec->time_base.den = mFramerate.nDen;

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
	mOutputStream.pVideoStream->time_base = pInStream->time_base;
}

void FFMpegRemuxer::CreateAudioStreamFilter(AudioInputStreamContext & io_pInputStream)
{
	struct AVFilterInOutDeleter {
		void operator()(AVFilterInOut * p) {
			if(p)
			{
				avfilter_inout_free(&p);
			}
		}
	};
	unique_ptr<AVFilterInOut, AVFilterInOutDeleter> ptrOutput(avfilter_inout_alloc());
	unique_ptr<AVFilterInOut, AVFilterInOutDeleter> ptrInput(avfilter_inout_alloc());
	if(ptrOutput == nullptr || ptrInput == nullptr)
	{
		throw FFMpegRemuxerException("Failed to allocate filter inouts");
	}

	io_pInputStream.pFilterGraph = avfilter_graph_alloc();
	if(io_pInputStream.pFilterGraph == nullptr)
	{
		throw FFMpegRemuxerException("Failed to allocate filter graph");
	}

	auto BufferSrc = avfilter_get_by_name("abuffer");
	auto BufferSink = avfilter_get_by_name("abuffersink");
	if(BufferSrc == nullptr || BufferSink == nullptr)
	{
		throw FFMpegRemuxerException("Filtering source or sink element not found");
	}

	AVStream * pInStream = io_pInputStream.pAVFormat->streams[0];

	char szArgs[512];
	snprintf(szArgs, sizeof(szArgs),
			"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
			pInStream->codec->time_base.num, pInStream->codec->time_base.den,
			pInStream->codec->sample_rate,
			av_get_sample_fmt_name(pInStream->codec->sample_fmt),
			pInStream->codec->channel_layout);

	auto Ret = avfilter_graph_create_filter(&io_pInputStream.pBufferSrc, BufferSrc, "in",
			szArgs, NULL, io_pInputStream.pFilterGraph);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Cannot create audio buffer source");
	}
	Ret = avfilter_graph_create_filter(&io_pInputStream.pBufferSink, BufferSink, "out",
			NULL, NULL, io_pInputStream.pFilterGraph);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Cannot create audio buffer sink");
	}

	Ret = av_opt_set_bin(io_pInputStream.pBufferSink, "sample_fmts",
			(uint8_t*)&mOutputStream.pAudioStream->codec->sample_fmt,
			sizeof(mOutputStream.pAudioStream->codec->sample_fmt), AV_OPT_SEARCH_CHILDREN);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Cannot set output sample format");
	}
	Ret = av_opt_set_bin(io_pInputStream.pBufferSink, "channel_layouts",
			(uint8_t*)&mOutputStream.pAudioStream->codec->channel_layout,
			sizeof(mOutputStream.pAudioStream->codec->channel_layout), AV_OPT_SEARCH_CHILDREN);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Cannot set output channel layout");
	}
	Ret = av_opt_set_bin(io_pInputStream.pBufferSink, "sample_rates",
			(uint8_t*)&mOutputStream.pAudioStream->codec->sample_rate,
			sizeof(mOutputStream.pAudioStream->codec->sample_rate),	AV_OPT_SEARCH_CHILDREN);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Cannot set output sample rate");
	}

	ptrOutput->name       = av_strdup("in");
	ptrOutput->filter_ctx = io_pInputStream.pBufferSrc;
	ptrOutput->pad_idx    = 0;
	ptrOutput->next       = NULL;
	ptrInput->name       = av_strdup("out");
	ptrInput->filter_ctx = io_pInputStream.pBufferSink;
	ptrInput->pad_idx    = 0;
	ptrInput->next       = NULL;
	if(ptrOutput->name == nullptr || ptrInput->name == nullptr)
	{
		throw FFMpegRemuxerException("Cannot dup name strings");
	}

	AVFilterInOut * pOutput = ptrOutput.get();
	AVFilterInOut * pInput = ptrInput.get();
	Ret = avfilter_graph_parse_ptr(io_pInputStream.pFilterGraph, "anull", &pInput, &pOutput, nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to parse filter graph");
	}
	ptrOutput.release();
	ptrInput.release();

	Ret = avfilter_graph_config(io_pInputStream.pFilterGraph, nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to configure filter graph");
	}
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
	pInStream->codec->channel_layout = AV_CH_LAYOUT_MONO;
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
	mOutputStream.pAudioStream->codec->sample_rate = 8000;
	mOutputStream.pAudioStream->codec->sample_fmt = AV_SAMPLE_FMT_FLT;
	mOutputStream.pAudioStream->codec->channels = 1;
	mOutputStream.pAudioStream->codec->channel_layout = AV_CH_LAYOUT_MONO;
	Ret = avcodec_open2(mOutputStream.pAudioStream->codec, avcodec_find_encoder(AV_CODEC_ID_AC3), nullptr);
	if(Ret < 0)
	{
		throw FFMpegRemuxerException("Failed to open encoder");
	}

	CreateAudioStreamFilter(io_pInputStream);
}

void FFMpegRemuxer::RemuxVideoPacket(InputStreamContext & io_pInputStream)
{
	AVStream * pInStream = io_pInputStream.pAVFormat->streams[0];

	AVPacket Pkt;
	auto Ret = av_read_frame(io_pInputStream.pAVFormat, &Pkt);
	if (Ret < 0)
	{
		return;
	}

	av_packet_rescale_ts(&Pkt, pInStream->codec->time_base, mOutputStream.pVideoStream->time_base);
	Pkt.stream_index = mOutputStream.pVideoStream->index;

	Ret = av_interleaved_write_frame(mOutputStream.pAVFormat, &Pkt);
	if (Ret < 0)
	{
		av_free_packet(&Pkt);
		throw FFMpegRemuxerException("Failed to remux packet");
	}

	av_free_packet(&Pkt);
}

void FFMpegRemuxer::TranscodeAudioPacketFilter(AudioInputStreamContext & io_pInputStream)
{
	AVStream * pInStream = io_pInputStream.pAVFormat->streams[0];

	AVPacket DecPkt;
	memset(&DecPkt, 0, sizeof(DecPkt));

	auto Ret = av_read_frame(io_pInputStream.pAVFormat, &DecPkt);
	if (Ret < 0)
	{
		return;
	}

	AVFramePtr pFrame(av_frame_alloc());
	if(pFrame == nullptr)
	{
		throw FFMpegRemuxerException("Failed to allocate frame");
	}
	AVFramePtr pFiltFrame;

	try
	{
		int nGotFrame;
		Ret = avcodec_decode_audio4(pInStream->codec, pFrame.get(), &nGotFrame, &DecPkt);
		if (Ret < 0)
		{
			char szError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(Ret, szError, AV_ERROR_MAX_STRING_SIZE);
			throw FFMpegRemuxerException(string("Failed to decode frame: ") + szError);
		}
		if(nGotFrame)
		{
			auto Ret = av_buffersrc_add_frame_flags(io_pInputStream.pBufferSrc, pFrame.get(), 0);
			if(Ret < 0)
			{
				throw FFMpegRemuxerException("Error while feeding the filtergraph");
			}

			while(true)
			{
				pFiltFrame.reset(av_frame_alloc());
				if(pFiltFrame == nullptr)
				{
					throw FFMpegRemuxerException("Failed to allocate frame");
				}

				Ret = av_buffersink_get_frame(io_pInputStream.pBufferSink, pFiltFrame.get());
				if(Ret < 0)
				{
					if (Ret != AVERROR(EAGAIN) && Ret != AVERROR_EOF)
					{
						throw FFMpegRemuxerException("Error while reading the filtergraph");
					}

					break;
				}

				pFiltFrame->pict_type = AV_PICTURE_TYPE_NONE;
				TranscodeAudioPacket(io_pInputStream, pFiltFrame);
			}
		}
	}
	catch(exception & Ex)
	{
		av_free_packet(&DecPkt);
		throw Ex;
	}

	av_free_packet(&DecPkt);
}

void FFMpegRemuxer::TranscodeAudioPacket(AudioInputStreamContext & io_pInputStream, AVFramePtr & in_ptrFrame)
{
	AVPacket EncPkt;
	memset(&EncPkt, 0, sizeof(EncPkt));

	try
	{
		av_init_packet(&EncPkt);
		int nGotPacket;
		auto Ret = avcodec_encode_audio2(mOutputStream.pAudioStream->codec, &EncPkt, in_ptrFrame.get(), &nGotPacket);
		if (Ret < 0)
		{
			char szError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(Ret, szError, AV_ERROR_MAX_STRING_SIZE);
			throw FFMpegRemuxerException(string("Failed to encode packet") + szError);
		}
		if(nGotPacket)
		{
			EncPkt.stream_index = mOutputStream.pAudioStream->index;
			av_packet_rescale_ts(&EncPkt, mOutputStream.pAudioStream->codec->time_base, mOutputStream.pAudioStream->time_base);

			Ret = av_interleaved_write_frame(mOutputStream.pAVFormat, &EncPkt);
			if (Ret < 0)
			{
				char szError[AV_ERROR_MAX_STRING_SIZE];
				av_strerror(Ret, szError, AV_ERROR_MAX_STRING_SIZE);
				throw FFMpegRemuxerException(string("Failed to mux packet") + szError);
			}
		}
	}
	catch(exception & Ex)
	{
		av_free_packet(&EncPkt);
		throw Ex;
	}

	av_free_packet(&EncPkt);
}
