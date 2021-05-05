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

#define DEBUG 1

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
  int watermarksize = 150;
  bool res_arr[watermarksize * watermarksize], *p_res_arr = (bool *)res_arr;
  #if 1
  uint32_t tmp_CB_index;

  for (unsigned int i = 0; i < watermarksize; i++)
  {
    tmp_CB_index = (i / 2) * wrap_cb;
    for (unsigned int j = (xsize - watermarksize), k = 0; j < xsize && k < watermarksize; j++, k++)
    {
        uint64_t arr_index = i * watermarksize + k;
        uint32_t CB_index =  tmp_CB_index + (j / 2);

        res_arr[arr_index] = (*buf_y  & 0x3u) >= 0x2u ? true : false;
        res_arr[arr_index] &= (*(buf_cb + CB_index) & 0x3u) >= 0x2u ? true : false;
        res_arr[arr_index] &= (*(buf_cr + CB_index) & 0x3u) >= 0x2u ? true : false;


        buf_y++;
    }
  }   
  #else
    for (int i = 0; i < watermarksize; i++)
    {
        for (int j = (xsize - watermarksize); j < xsize; j++)
        {
            uint64_t Y_index = i * wrap_y + j;
            uint64_t CB_index = (i/2) * wrap_cb +(j/2);

            uint8_t y_values =  *(buf_y + Y_index)      | 0x3u;
            uint8_t cb_values = *(buf_cb + CB_index)    | 0x3u;
            uint8_t cr_values = *(buf_cr + CB_index)    | 0x3u;
            *(buf_y + Y_index)  = y_values;
            *(buf_cb + CB_index) = cb_values;
            *(buf_cr + CB_index) = cr_values;
        }
    } 
  #endif     

  unsigned int amount_of_marked_pixel = 0;
  for (unsigned int i = 0 ; i < watermarksize * watermarksize; i++)
  {
    if (res_arr[i] == true)
        amount_of_marked_pixel++;
  }
  return amount_of_marked_pixel;
}

int main(int argc, const char *argv[])
{
  if (argc < 2) {
    printf("You need to specify a media file.\n");
    return -1;
  }
  std::vector<t_stream_params>  output_streams;
  
  logging("initializing all the containers, codecs and protocols.");

  // AVFormatContext holds the header information from the format (Container)
  // Allocating memory for this component
  // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (!pFormatContext) {
    logging("ERROR could not allocate memory for Format Context");
    return -1;
  }

  logging("opening the input file (%s) and loading format (container) header", argv[1]);
  // Open the file and read its header. The codecs are not opened.
  // The function arguments are:
  // AVFormatContext (the component we allocated memory for),
  // url (filename),
  // AVInputFormat (if you pass NULL it'll do the auto detect)
  // and AVDictionary (which are options to the demuxer)
  // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga31d601155e9035d5b0e7efedc894ee49
  if (avformat_open_input(&pFormatContext, argv[1], NULL, NULL) != 0) {
    logging("ERROR could not open the file");
    return -1;
  }

  // now we have access to some information about our file
  // since we read its header we can say what format (container) it's
  // and some other information related to the format itself.
  logging("format %s, duration %lld us, bit_rate %lld", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

  logging("finding stream info from format");
  // read Packets from the Format to get stream information
  // this function populates pFormatContext->streams
  // (of size equals to pFormatContext->nb_streams)
  // the arguments are:
  // the AVFormatContext
  // and options contains options for codec corresponding to i-th stream.
  // On return each dictionary will be filled with options that were not found.
  // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gad42172e27cddafb81096939783b157bb
  if (avformat_find_stream_info(pFormatContext,  NULL) < 0) {
    logging("ERROR could not get the stream info");
    return -1;
  }

    
  // the component that knows how to enCOde and DECode the stream
  // it's the codec (audio or video)
  // http://ffmpeg.org/doxygen/trunk/structAVCodec.html
  AVCodec *pCodec = NULL;
  // this component describes the properties of a codec used by the stream i
  // https://ffmpeg.org/doxygen/trunk/structAVCodecParameters.html
  AVCodecParameters *pCodecParameters =  NULL;
  int video_stream_index = -1;

  // loop though all the streams and print its main information
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

    // finds the registered decoder for a codec ID
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga19a0ca553277f019dd5b0fec6e1f9dca
    pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

    if (pLocalCodec==NULL) {
      logging("ERROR unsupported codec!");
      // In this example if the codec is not found we just skip it
      continue;
    }

    // when the stream is a video we store its index, codec parameters and codec
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

    // print its name, id and bitrate
    logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
  }

  if (video_stream_index == -1) {
    logging("File %s does not contain a video stream!", argv[1]);
    return -1;
  }

  // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
  AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
  if (!pCodecContext)
  {
    logging("failed to allocated memory for AVCodecContext");
    return -1;
  }

  // Fill the codec context based on the values from the supplied codec parameters
  // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#gac7b282f51540ca7a99416a3ba6ee0d16
  if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
  {
    logging("failed to copy codec params to codec context");
    return -1;
  }

  // Initialize the AVCodecContext to use the given AVCodec.
  // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#ga11f785a188d7d9df71621001465b0f1d
  if (avcodec_open2(pCodecContext, pCodec, NULL) < 0)
  {
    logging("failed to open codec through avcodec_open2");
    return -1;
  }

  // https://ffmpeg.org/doxygen/trunk/structAVFrame.html
  AVFrame *pFrame = av_frame_alloc();
  if (!pFrame)
  {
    logging("failed to allocated memory for AVFrame");
    return -1;
  }
  // https://ffmpeg.org/doxygen/trunk/structAVPacket.html
  AVPacket *pPacket = av_packet_alloc();
  if (!pPacket)
  {
    logging("failed to allocated memory for AVPacket");
    return -1;
  }

  int response = 0;
  int how_many_packets_to_process = 8;

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

  avformat_close_input(&pFormatContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecContext);
  return 0;
}

static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf( stderr, "LOG: " );
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
    fprintf( stderr, "\n" );
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame)
{
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

      char frame_filename[1024];
      snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
      if (pFrame->format != AV_PIX_FMT_YUV420P)
      {
        logging("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
      }

      unsigned int ans = get_watermark(pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->width, pFrame->height);
      std::cout << ans << ", " ;

    }
  }
  return 0;
}

static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}
