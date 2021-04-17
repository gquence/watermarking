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
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame, t_stream_params *stream_params, AVFormatContext *input_fctx, int stream_id);
// save a frame into a .pgm file
static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename);


static void set_watermark(unsigned char *buf_y, unsigned char *buf_cb, unsigned char *buf_cr, 
                            int wrap_y, int wrap_cb, int wrap_cr, int xsize, int ysize)
{
    int watermarksize = 50;
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
      av_opt_set(pLocalCodecContext->priv_data, "preset", "fast", 0);
      
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


  output_streams =  create_encode_stream_params(pFormatContext, "lala.mp4", pCodecContext);
  if (output_streams.empty())
  {  return -1;}

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

  // fill the Packet with data from the Stream
  // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61
  while (av_read_frame(pFormatContext, pPacket) >= 0)
  {
    // if it's the video stream
    if (pPacket->stream_index == video_stream_index) {
    logging("AVPacket->pts %" PRId64, pPacket->pts);
      for (int i = 0; i < output_streams.size(); i++){
        if (output_streams[i].media_type != AVMEDIA_TYPE_VIDEO) 
          continue;
        response = decode_packet(pPacket, pCodecContext, pFrame, &(output_streams[i]), pFormatContext, video_stream_index);
      }
      if (response < 0)
        break;
      // stop it, otherwise we'll be saving hundreds of frames
      //if (--how_many_packets_to_process <= 0) break;
    }
    // https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html#ga63d5a489b419bd5d45cfd09091cbcbc2
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

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame, t_stream_params *stream_params, AVFormatContext *input_fctx, int stream_id)
{
  // Supply raw packet data as input to a decoder
  // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
  int response = avcodec_send_packet(pCodecContext, pPacket);

  if (response < 0) {
    logging("Error while sending a packet to the decoder: %d", response);
    return response;
  }

  while (response >= 0)
  {
    // Return decoded output data (into a frame) from a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
    response = avcodec_receive_frame(pCodecContext, pFrame);
    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
      break;
    } else if (response < 0) {
      logging("Error while receiving a frame from the decoder: %d", response);
      return response;
    }

    if (response >= 0) {
      //logging(
      //    "Frame %d (type=%c, size=%d bytes, format=%d) pts %d key_frame %d [DTS %d]",
      //    pCodecContext->frame_number,
      //    av_get_picture_type_char(pFrame->pict_type),
      //    pFrame->pkt_size,
      //    pFrame->format,
      //    pFrame->pts,
      //    pFrame->key_frame,
      //    pFrame->coded_picture_number
      //);

      char frame_filename[1024];
      snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
      // Check if the frame is a planar YUV 4:2:0, 12bpp
      // That is the format of the provided .mp4 file
      // RGB formats will definitely not give a gray image
      // Other YUV image may do so, but untested, so give a warning
      if (pFrame->format != AV_PIX_FMT_YUV420P)
      {
        logging("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
      }
      // save a grayscale frame into a .pgm file
      //save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);

      set_watermark(pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->width, pFrame->height);

      encode_video(stream_params, pFrame, input_fctx, stream_id);
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
