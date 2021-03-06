/*
 * http://ffmpeg.org/doxygen/trunk/index.html
 *
 * Main components
 *
 * Format (Container) - a wrapper, providing sync, metadata and muxing for the streams.
 * Stream - a continuous stream (audio or video) of data over time.
 * Codec - defines how data are enCOded (from Frame to Packet)
 *        and DECoded (from Packet to Frame).
 * Packet - are the data (kind of slices of the stream data) to be decoded as raw frames.
 * Frame - a decoded raw frame (to be encoded or filtered).
 */
extern "C"
{
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavutil/opt.h>
}
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <string>
#include <iostream>
#include <vector>

#define DEBUG 0

typedef struct {
  AVCodec           *codec;
  AVCodecContext    *codec_ctx;
  AVCodecParameters *codec_params;
  AVStream          *stream;
  AVFormatContext   *fctx;
  AVMediaType       media_type;
}                   t_stream_params;
// print out the steps and errors
static void logging(const char *fmt, ...);
// decode packets into frames
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame);
// save a frame into a .pgm file
static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename);


static unsigned int get_watermark(unsigned char *buf_y, unsigned char *buf_cb, unsigned char *buf_cr, 
                            int wrap_y, int wrap_cb, int wrap_cr, int xsize, int ysize)
{
  int           watermarksize = 75;
  unsigned int  amount_of_marked_pixel = 0;
  uint32_t      tmp_CB_index;
  bool          res;

  for (unsigned int i = (ysize - watermarksize); i < ysize; i++)
  {
    tmp_CB_index = (i / 2) * wrap_cb;
    for (unsigned int j = (xsize - watermarksize), k = 0; j < xsize && k < watermarksize; j++, k++)
    {
        uint64_t arr_index = i * watermarksize + k;
        uint32_t CB_index =  tmp_CB_index + (j / 2);

        //res = (*buf_y  & 0x3u) >= 0x2u ? true : false;
        //res = (*buf_y  & 0x3u) >= 0x2u ? true : false;
        res = (*(buf_cb + CB_index) & 0x3u) >= 0x2u ? true : false;
        //res &= (*(buf_cr + CB_index) & 0x3u) >= 0x2u ? true : false;
        if (res == true)
          amount_of_marked_pixel++;

        buf_y++;
    }
  }
  return amount_of_marked_pixel;
}

uint64_t frame_count;
std::vector<unsigned int> frame_marks;
std::string out_s;

int main(int argc, const char *argv[])
{
  frame_count = 0;
  if (argc < 2) {
    printf("You need to specify a media file.\n");
    return -1;
  }
  std::vector<t_stream_params>  output_streams;
  
  logging("initializing all the containers, codecs and protocols.");


  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (!pFormatContext) {
    logging("ERROR could not allocate memory for Format Context");
    return -1;
  }

  logging("opening the input file (%s) and loading format (container) header", argv[1]);

  if (avformat_open_input(&pFormatContext, argv[1], NULL, NULL) != 0) {
    logging("ERROR could not open the file");
    return -1;
  }

  logging("format %s, duration %lld us, bit_rate %lld", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

  logging("finding stream info from format");

  if (avformat_find_stream_info(pFormatContext,  NULL) < 0) {
    logging("ERROR could not get the stream info");
    return -1;
  }


  AVCodec *pCodec = NULL;

  AVCodecParameters *pCodecParameters =  NULL;
  int video_stream_index = -1;

  for (int i = 0; i < pFormatContext->nb_streams; i++)
  {
    AVCodecParameters *pLocalCodecParameters =  NULL;
    pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
    logging("AVStream->time_base before open coded %d/%d", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
    logging("AVStream->r_frame_rate before open coded %d/%d", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
    logging("AVStream->start_time %" PRId64, pFormatContext->streams[i]->start_time);
    logging("AVStream->duration %" PRId64, pFormatContext->streams[i]->duration);

    logging("finding the proper decoder (CODEC)");

    AVCodec *pLocalCodec = NULL;

    pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

    if (pLocalCodec==NULL) {
      logging("ERROR unsupported codec!");
      continue;
    }

    if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (video_stream_index == -1) {
        video_stream_index = i;
        pCodec = pLocalCodec;
        pCodecParameters = pLocalCodecParameters;
      }

      logging("Video Codec: resolution %d x %d", pLocalCodecParameters->width, pLocalCodecParameters->height);
    } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      logging("Audio Codec: %d channels, sample rate %d", pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);
    }

    logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
  }

  if (video_stream_index == -1) {
    logging("File %s does not contain a video stream!", argv[1]);
    return -1;
  }

  AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
  if (!pCodecContext)
  {
    logging("failed to allocated memory for AVCodecContext");
    return -1;
  }

  if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
  {
    logging("failed to copy codec params to codec context");
    return -1;
  }

  if (avcodec_open2(pCodecContext, pCodec, NULL) < 0)
  {
    logging("failed to open codec through avcodec_open2");
    return -1;
  }

  AVFrame *pFrame = av_frame_alloc();
  if (!pFrame)
  {
    logging("failed to allocated memory for AVFrame");
    return -1;
  }
  AVPacket *pPacket = av_packet_alloc();
  if (!pPacket)
  {
    logging("failed to allocated memory for AVPacket");
    return -1;
  }

  int response = 0;
  int how_many_packets_to_process = 8;


  frame_marks = std::vector<unsigned int>();
  while (av_read_frame(pFormatContext, pPacket) >= 0)
  {
    if (pPacket->stream_index == video_stream_index) {
      response = decode_packet(pPacket, pCodecContext, pFrame);
      if (response < 0)
        break;
    }
    av_packet_unref(pPacket);
  }
  std::cout << std::endl;
  logging("releasing all the resources");
  #if DEBUG == 1
    std::cout << out_s;
  #endif
  avformat_close_input(&pFormatContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecContext);
  return 0;
}

static void logging(const char *fmt, ...)
{
  #if DEBUG == 1
    va_list args;
    fprintf( stderr, "LOG: " );
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
    fprintf( stderr, "\n" );
  #endif
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame)
{
  uint64_t first_skipped_frames = 10;
  int response = avcodec_send_packet(pCodecContext, pPacket);

  if (response < 0) {
    logging("Error while sending a packet to the decoder: %d", response);
    return response;
  }

  while (response >= 0)
  {
    response = avcodec_receive_frame(pCodecContext, pFrame);
    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
      break;
    } else if (response < 0) {
      logging("Error while receiving a frame from the decoder: %d", response);
      return response;
    }

    if (response >= 0) {
      uint64_t frame_key = 14;
      uint64_t half_key = frame_key / 2;
      uint64_t key_0_1 = frame_key / 10 + ((frame_key % 10 >= 5) ? 1 : 0);
      uint64_t first_period_start  = key_0_1;
      uint64_t first_period_end    = half_key  - key_0_1;
      uint64_t second_period_start = half_key  + key_0_1;
      uint64_t second_period_end   = frame_key - key_0_1;
      
      if (pFrame->format != AV_PIX_FMT_YUV420P)
      {
        logging("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
      }

      unsigned int ans = get_watermark(pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->width, pFrame->height);
      frame_marks.push_back(ans);
      if (frame_marks.size() == frame_key)
      {
        unsigned int first_half_block_sum = 0, second_half_block_sum = 0;
        uint64_t i = first_period_start;

        for (; i < first_period_end; i++) { first_half_block_sum += frame_marks[i]; }
        first_half_block_sum /= (half_key - key_0_1);

        for (; i < second_period_start; i++) {}
        for (; i < second_period_end; i++) { second_half_block_sum += frame_marks[i]; }
        second_half_block_sum /= (half_key - key_0_1);

        if (first_half_block_sum > second_half_block_sum) std::cout << "0";
        else                                              std::cout << "1";

        frame_marks.clear();
      }
      out_s.append(std::to_string(ans) + ", ");

    }
  }
  return 0;
}