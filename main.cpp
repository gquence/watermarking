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
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, 
                         AVFrame *pFrame, t_stream_params *stream_params,
                         AVFormatContext *input_fctx, int stream_id,
                         const std::string message, int &mess_index);
// save a frame into a .pgm file
static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename);


static void set_watermark(unsigned char *buf_y, unsigned char *buf_cb, unsigned char *buf_cr, 
                            int wrap_y, int wrap_cb, int wrap_cr, int xsize, int ysize, bool is_one)
{
  int watermarksize = 200;
  #if 1
  uint32_t tmp_CB_index;
  unsigned char mark;

  if (is_one == true)
    mark = 0x3u;
  else
    mark = 0x0u;

  for (unsigned int i = 0; i < watermarksize; i++)
  {
    tmp_CB_index = (i / 2) * wrap_cb;
    for (unsigned int j = (xsize - watermarksize); j < xsize; j++)
    {
        uint32_t CB_index =  tmp_CB_index + (j / 2);
        unsigned char *p_cb = buf_cb + CB_index;
        unsigned char *p_cr = buf_cr + CB_index;
        *buf_y =  *buf_y  | mark;
        *p_cb =   *p_cb   | mark;
        *p_cr =   *p_cr   | mark;
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
}


int create_fctx(const std::string &filename, AVFormatContext **fctx)
{
    int func_ret;

    func_ret = avformat_alloc_output_context2(fctx, nullptr, nullptr, filename.c_str());
    if (func_ret  < 0)
    {
        #if DEBUG == 1
        std::cerr << "Error when opening file: "  << filename << ". Err_code = "
            << func_ret << std::endl;
        #endif
        return -1;
    }
    return 0;
}

int encode_video(t_stream_params *t_stream_params, AVFrame *input_frame, AVFormatContext *input_ctx, int stream_id)
{
    if (input_frame) input_frame->pict_type = AV_PICTURE_TYPE_NONE;

    AVPacket *output_packet = av_packet_alloc();
    AVStream *output_stream = t_stream_params->stream;
    AVStream *input_stream = input_ctx->streams[stream_id];
    int response;

    if (!output_packet)
    {
        if (DEBUG)
        {
            std::cout << "could not allocate memory for output packet";
        }
        return -1;
    }
    response = avcodec_send_frame(t_stream_params->codec_ctx, input_frame);
    //std::cout << !avcodec_is_open(codecInfo->out_CodecContext) || !av_codec_is_encoder(codecInfo->out_CodecContext->codec);
    //std::cout << av_codec_is_encoder(t_stream_params->codec);
    //print_codec_info(*codecInfo);

            
    while (response >= 0) {
        response = avcodec_receive_packet(t_stream_params->codec_ctx, output_packet);

        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            if (DEBUG){
                std::cout << "Error while receiving packet from encoder: " << response;
            }
            return -1;
        }

        output_packet->stream_index = output_stream->id;
        output_packet->duration = output_stream->time_base.den / output_stream->time_base.num / input_stream->avg_frame_rate.num * input_stream->avg_frame_rate.den;

        av_packet_rescale_ts(output_packet, input_stream->time_base, output_stream->time_base);
//        output_packet->pts = av_rescale_q_rnd(output_packet->pts, input_stream->time_base,
//                        output_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
//        output_packet->dts = av_rescale_q_rnd(output_packet->dts, input_stream->time_base,
//                        output_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        output_packet->pos = -1;
        output_packet->duration = 0;
        response = av_interleaved_write_frame(t_stream_params->fctx, output_packet);
        if (response != 0)
        {
            if (DEBUG)
                std::cout << "Error %d while receiving packet from decoder: " << response;
            return -1;
        }
    }
    av_packet_unref(output_packet);
    av_packet_free(&output_packet);
    return 0;
   
}

std::vector<t_stream_params> create_encode_stream_params(AVFormatContext *input_fctx, const std::string &out_filename, AVCodecContext *decoder_ctx)
{
  std::vector<t_stream_params>  res;
  AVFormatContext               *output_fctx;
  int                           func_res;
  func_res = create_fctx(out_filename, &output_fctx);
  if (func_res < 0) goto end_flag_cesp;

  
  for (size_t i = 0; i < input_fctx->nb_streams; i++)
  {
    t_stream_params stream_params;
    AVCodecParameters *pLocalCodecParameters =  NULL;
    AVCodec *pLocalCodec = NULL;
    AVCodecContext *pLocalCodecContext;
    AVStream *out_stream;

    pLocalCodecParameters = input_fctx->streams[i]->codecpar;
    stream_params.media_type = pLocalCodecParameters->codec_type;
    if ((pLocalCodecParameters->codec_type != AVMEDIA_TYPE_VIDEO) &&
        (pLocalCodecParameters->codec_type != AVMEDIA_TYPE_AUDIO) &&
        (pLocalCodecParameters->codec_type != AVMEDIA_TYPE_SUBTITLE))
    {
      logging("found not audio or video stream. Index = %d\n SKIPPED", i);
      continue;
    }
    out_stream = avformat_new_stream(output_fctx, nullptr);
    out_stream->start_time = input_fctx->streams[i]->start_time;
    stream_params.stream = out_stream;
    if (!out_stream)
    {
      logging("Error in stream initialization");
      goto end_flag_cesp;
    }
    logging("copying the codec params");
    func_res = avcodec_parameters_copy(out_stream->codecpar, pLocalCodecParameters);
    stream_params.codec_params = out_stream->codecpar;
    if (func_res < 0)
    {
      logging("Error in codec-params copy");
      goto end_flag_cesp;
    }
    logging("finding the proper encoder (CODEC)");
    pLocalCodec = avcodec_find_encoder(out_stream->codecpar->codec_id);
    stream_params.codec = pLocalCodec;
    if (pLocalCodec==NULL) {
      logging("ERROR unsupported codec!");
      goto end_flag_cesp;
      continue;
    }
    logging("get codec context for encoder");
    pLocalCodecContext = avcodec_alloc_context3(pLocalCodec);
    func_res = avcodec_parameters_to_context(pLocalCodecContext, stream_params.codec_params);
    stream_params.codec_ctx = pLocalCodecContext;
    if (func_res < 0)
    {
      logging("Error in codec-context initialization");
      goto end_flag_cesp;
    }
    if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) 
    {
      uint8_t *preset = nullptr, *tune = nullptr, *profile = nullptr;

      av_opt_set(pLocalCodecContext->priv_data, "preset", "slow", 0);
      av_opt_set(pLocalCodecContext->priv_data, "tune", "film", 0);
      av_opt_set(pLocalCodecContext->priv_data, "vprofile", "high", 0);  
      av_opt_set(pLocalCodecContext->priv_data, "crf", "18", 0);
      
      pLocalCodecContext->height = decoder_ctx->height;
      pLocalCodecContext->width = decoder_ctx->width;
      pLocalCodecContext->sample_aspect_ratio = decoder_ctx->sample_aspect_ratio;
      //if (pLocalCodec->pix_fmts)
      //  pLocalCodecContext->pix_fmt = pLocalCodec->pix_fmts[0];
      //else
        pLocalCodecContext->pix_fmt = decoder_ctx->pix_fmt;

      pLocalCodecContext->bit_rate = decoder_ctx->bit_rate;
      pLocalCodecContext->rc_buffer_size = decoder_ctx->rc_buffer_size;
      pLocalCodecContext->rc_max_rate = decoder_ctx->rc_max_rate;
      pLocalCodecContext->rc_min_rate = decoder_ctx->rc_min_rate;
    //pLocalCodecContext->bit_rate = 2 * 1000 * 1000;
    //pLocalCodecContext->rc_buffer_size = 4 * 1000 * 1000;
    //pLocalCodecContext->rc_max_rate = 2 * 1000 * 1000;
    //pLocalCodecContext->rc_min_rate = 2.5 * 1000 * 1000;
      pLocalCodecContext->time_base = av_inv_q(av_guess_frame_rate(input_fctx, input_fctx->streams[i], NULL));
      out_stream->time_base = pLocalCodecContext->time_base;
    }
    logging("openning the encoder");
    if (avcodec_open2(pLocalCodecContext, pLocalCodec, NULL) < 0)
    {
      logging("failed to open codec");
      goto end_flag_cesp;
    }
    stream_params.fctx = output_fctx;
    
    res.push_back(stream_params);
    logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name, pLocalCodec->id, stream_params.codec_params->bit_rate);
  }
  if (output_fctx->oformat->flags  & AVFMT_GLOBALHEADER)
    output_fctx->oformat->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  if (!(output_fctx->oformat->flags & AVFMT_NOFILE))
  {
      func_res = avio_open(&(output_fctx->pb), (const char *)output_fctx->filename, AVIO_FLAG_WRITE);
      if (func_res < 0)
      {
          #if DEBUG == 1
              std::cerr << "Error when openning AVIOContext" << std::endl;
          #endif
          goto end_flag_cesp;
      }
  }
  func_res = avformat_write_header(output_fctx, nullptr);
  if (func_res < 0) {
    fprintf(stderr, "Error occurred when opening output file\n");
    goto end_flag_cesp;
  }
end_flag_cesp:
  return res;
}

uint64_t frame_count;
bool next_is_one = false;
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

    // print its name, id and bitrate
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


  output_streams =  create_encode_stream_params(pFormatContext, "lala.mp4", pCodecContext);
  if (output_streams.empty())
  {  return -1;}

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
  std::string message = "0";
  int index = 0;

  while (av_read_frame(pFormatContext, pPacket) >= 0)
  {
    if (pPacket->stream_index == video_stream_index) {
      for (int i = 0; i < output_streams.size(); i++){
        if (output_streams[i].media_type != AVMEDIA_TYPE_VIDEO) 
          continue;
        response = decode_packet(pPacket, pCodecContext, pFrame, &(output_streams[i]), pFormatContext, video_stream_index, message, index);
      }
      if (response < 0)
        break;
    }
    av_packet_unref(pPacket);
  }
  response =  av_write_trailer(output_streams[0].fctx);
  if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
      std::cerr << "something goes wrong with writing in file";
  }
  avio_closep(&(output_streams[0].fctx->pb));
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

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, 
                         AVFrame *pFrame, t_stream_params *stream_params,
                         AVFormatContext *input_fctx, int stream_id,
                         const std::string message, int &mess_index)
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
      //save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);

      if (frame_count % 10 == 0)
      {
        bool is_one;
        std::cout << message[mess_index] << std::flush;
        if (message[mess_index] == '1')
        {
          is_one = true;
        }
        else
        {
          is_one = false;
        }
        set_watermark(pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->width, pFrame->height, is_one);
        mess_index++;
        if (message.length() <= mess_index)
        {
          mess_index = 0;
        }
        if (message[mess_index] == '1')
          next_is_one = true;
        else
          next_is_one = false;
      }
      else{
        set_watermark(pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->width, pFrame->height, !next_is_one);
      }
      encode_video(stream_params, pFrame, input_fctx, stream_id);
      frame_count++;
    }
  }
  return 0;
}

static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}
