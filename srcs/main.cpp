#include "format.h"
#include "codec.h"

extern "C"
{
    #include <libavutil/timestamp.h>
    #include <libavcodec/avcodec.h>
}

#include <string>
#include <vector>
#include <iostream>

#include <unistd.h>

typedef struct StreamingContext {
  AVFormatContext *fctx;
  AVCodec *video_codec;
  AVStream *video_stream;
  AVCodecContext *video_codec_ctx;
  int video_stream_index;
  //AVCodec *audio_avc;
  //AVStream *audio_avs;
  //AVCodecContext *audio_avcc;
  //int audio_index;
  std::string filename;
} StreamingContext;



static int decode_packet(AVPacket *pPacket, AVFrame *pFrame, t_codec_info *codecInfo);
// save a frame into a .pgm file
static void save_gray_frame(unsigned char *buf_y, unsigned char *buf_cb, unsigned char *buf_cr, 
                            int wrap_y, int wrap_cb, int wrap_cr, int xsize, int ysize, char *filename);

static void set_watermark(unsigned char *buf_y, unsigned char *buf_cb, unsigned char *buf_cr, 
                            int wrap_y, int wrap_cb, int wrap_cr, int xsize, int ysize);


int encode_video(t_codec_info *input_codecInfo, AVFrame *input_frame,
                 AVStream *input_stream, AVStream *output_stream,
                 AVFormatContext *output_ctx);

int prepare_decoder(StreamingContext *sc)
{
    for (int i = 0; i < sc->fctx->nb_streams; i++) {
        if (sc->fctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            sc->video_stream = sc->fctx->streams[i];
            sc->video_stream_index = i;

            if (fill_streams_codec_info(sc->video_stream, &sc->video_codec, &sc->video_codec_ctx))
            {
                return -1;
            }
        }
        //else if (sc->avfc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        //    sc->audio_avs = sc->avfc->streams[i];
        //    sc->audio_index = i;
        //    if (fill_stream_info(sc->audio_avs, &sc->audio_avc, &sc->audio_avcc))
        //    {
        //        return -1;
        //    }
        //}
        else 
        {
            #if DEBUG == 1
                std::cerr << "skipping streams other than video" << std::endl;
            #endif
        }
    }
    return 0;
}

int main(int ac, char **av)
{
    int             func_res = 0;
    std::string     filename;
    std::string     out_filename;
    AVFormatContext *input_fctx = nullptr;
    AVFormatContext *output_fctx = nullptr;
    AVDictionary    *opts = nullptr;
    std::vector<AVStream*>      streamArr;
    std::vector<t_codec_info>   codecArr;
    AVFrame *pFrame;
    AVPacket *pPacket;
    int frame_count = 0;
    int response = 0;
    int how_many_packets_to_process = 8;
    AVDictionary* muxer_opts = nullptr;
    StreamingContext decoder, encoder;

    

    if (ac == 3) {
        filename = av[1];
        out_filename = av[2];
        encoder.filename = out_filename;
        decoder.filename = filename;
    }
    else {
        std::cerr << "usage: test_prog 'input_filename' 'output_filename'" << std::endl;    
        return -1;
    }


    func_res = get_fctx_from_file(filename, &input_fctx, &opts);
    if (func_res < 0) goto ret_flag;
    
    func_res = get_all_streams(input_fctx, streamArr);
    if (func_res < 0) goto ret_flag;

    func_res = get_video_codecs_from_streams_vector(streamArr, codecArr);
    if (func_res < 0) goto ret_flag;
    for (auto &i : codecArr)
    {
        i.input_fctx = input_fctx;
    }
    func_res = create_fctx(out_filename, &output_fctx);
    if (func_res < 0) goto ret_flag;
    
    for (auto &i : codecArr)
    {
        i.input_fctx = input_fctx;
        i.output_fctx = output_fctx;
    }

    func_res = create_corresponding_basic_streams(codecArr, output_fctx);
    if (func_res < 0) goto ret_flag;

    func_res = open_IOctx(output_fctx);
    if (func_res < 0) goto ret_flag;

    if (avformat_write_header(output_fctx, &muxer_opts) < 0)
    {
        #if DEBUG == 1
            std::cout << "an error occurred when opening output file";
        #endif
        goto ret_flag;
    }

    pFrame = av_frame_alloc();
    if (!pFrame)
    {
        std::cout << "failed to allocated memory for AVFrame";
        goto ret_flag;
    }
    pPacket = av_packet_alloc();
    if (!pPacket)
    {
        std::cout << "failed to allocated memory for AVPacket";
        goto ret_flag;
    }
    t_codec_info *videoCodecInfo;
    AVStream *out_video_stream;
    for (auto  codec_info : codecArr)
    {
        if (codec_info.codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoCodecInfo = &codec_info;
            break;
        }
    }

    while (av_read_frame(input_fctx, pPacket) >= 0)
    {
        if (input_fctx->streams[pPacket->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            #if DEBUG == 1
                std::cout << "AVPacket->pts " <<  pPacket->pts << std::endl;
            #endif
            response = decode_packet(pPacket, pFrame, videoCodecInfo);
            
            if (response != 0)
            {
                std::cerr << "Something goes wrong" << std::endl;
                goto cycle_end;
            }
            // stop it, otherwise we'll be saving hundreds of frames
            if (how_many_packets_to_process <= 0)
            {
                av_packet_unref(pPacket);
                break;
            }
        }
cycle_end:
        av_packet_unref(pPacket);
    }
    //func_res =  av_write_trailer(output_fctx);
    //if (func_res == AVERROR(EAGAIN) || func_res == AVERROR_EOF) {
    //    std::cerr << "something goes wrong with writing in file";
    //}
        
    
ret_flag:
    if (codecArr.size() != 0)
        free_codec_arr(codecArr);
    if (output_fctx && !(output_fctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_fctx->pb);
    if (output_fctx != nullptr)
        avformat_free_context(output_fctx);
    if (input_fctx != nullptr)
        avformat_close_input(&input_fctx);
    return func_res;
}

int encode_video(t_codec_info *input_codecInfo, AVFrame *input_frame,
                 AVStream *input_stream, AVStream *output_stream,
                 AVFormatContext *output_ctx)
{
    if (input_frame) input_frame->pict_type = AV_PICTURE_TYPE_NONE;

    AVPacket *output_packet = av_packet_alloc();
    if (!output_packet)
    {
        if (DEBUG)
        {
            std::cout << "could not allocate memory for output packet";
        }
        return -1;
    }

    int response = avcodec_send_frame(input_codecInfo->CodecContext, input_frame);

    while (response >= 0) {
        response = avcodec_receive_packet(input_codecInfo->CodecContext, output_packet);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            if (DEBUG){
                std::cout << "Error while receiving packet from encoder: " << response;
            }
            return -1;
        }

        output_packet->stream_index = input_codecInfo->output_stream_index;
        output_packet->duration = output_stream->time_base.den / output_stream->time_base.num / input_stream->avg_frame_rate.num * input_stream->avg_frame_rate.den;

        av_packet_rescale_ts(output_packet, input_stream->time_base, output_stream->time_base);
        response = av_write_frame(output_ctx, output_packet);
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


static int decode_packet(AVPacket *pPacket, AVFrame *pFrame, t_codec_info *codecInfo)
{
    // Supply raw packet data as input to a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
    int response = avcodec_send_packet(codecInfo->CodecContext, pPacket);
    
    if (response < 0) {
        std::cerr << "Error while sending a packet to the decoder " << std::endl;
        return response;
    }

    while (response >= 0)
    {
        // Return decoded output data (into a frame) from a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
        response = avcodec_receive_frame(codecInfo->CodecContext, pFrame);
        
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            std::cerr << "Error while receiving a frame from the decoder: " << std::endl;
            return response;
        }
        if (response >= 0) {
            // Check if the frame is a planar YUV 4:2:0, 12bpp
            // That is the format of the provided .mp4 file
            // RGB formats will definitely not give a gray image
            // Other YUV image may do so, but untested, so give a warning
            // save a grayscale frame into a .ppm file 
            if (pFrame->format != AV_PIX_FMT_YUV420P)
            {
              std::cerr << "Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB" << std::endl;
            }

            set_watermark(pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->width, pFrame->height);
            if (DEBUG)
            {
                std::cout 
                << "Frame " << codecInfo->CodecContext->frame_number
                << ",(type="<< av_get_picture_type_char(pFrame->pict_type)
                << ", size=" << pFrame->pkt_size
                << " bytes, format=" << pFrame->format
                << ") pts " << pFrame->pts 
                << " key_frame " << pFrame->key_frame
                << " [DTS " << pFrame->coded_picture_number << "]" 
                << " colorspace = " << av_frame_get_colorspace(pFrame)
                << " colorrange = " << av_frame_get_color_range(pFrame) << std::endl;
                char frame_filename[1024];
                snprintf(frame_filename, sizeof(frame_filename), "%s-%d.ppm", "frame", codecInfo->CodecContext->frame_number);
                save_gray_frame(pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->width, pFrame->height, frame_filename);
            }
            //response = encode_video(codecInfo, pFrame, input_stream, output_stream, output_ctx);
            if (response < 0)
                return response;
            

        }
        //av_frame_unref(pFrame);
    }
  return 0;
}
#include <algorithm> 
#if 1
#define CLIP(X) ( (X) > 255 ? 255 : (X) < 0 ? 0 : X)

// RGB -> YUV
#define RGB2Y(R, G, B) CLIP(( (  66 * (R) + 129 * (G) +  25 * (B) + 128) >> 8) +  16)
#define RGB2U(R, G, B) CLIP(( ( -38 * (R) -  74 * (G) + 112 * (B) + 128) >> 8) + 128)
#define RGB2V(R, G, B) CLIP(( ( 112 * (R) -  94 * (G) -  18 * (B) + 128) >> 8) + 128)

// YUV -> RGB
#define C(Y) ( (Y) - 16  )
#define D(U) ( (U) - 128 )
#define E(V) ( (V) - 128 )

#define YUV2R(Y, U, V) CLIP(( 298 * C(Y)              + 409 * E(V) + 128) >> 8)
#define YUV2G(Y, U, V) CLIP(( 298 * C(Y) - 100 * D(U) - 208 * E(V) + 128) >> 8)
#define YUV2B(Y, U, V) CLIP(( 298 * C(Y) + 516 * D(U)              + 128) >> 8)

// RGB -> YCbCr
#define CRGB2Y(R, G, B) CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16)
#define CRGB2Cb(R, G, B) CLIP((36962 * (B - CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16) ) >> 16) + 128)
#define CRGB2Cr(R, G, B) CLIP((46727 * (R - CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16) ) >> 16) + 128)

// YCbCr -> RGB
#define CYCbCr2R(Y, Cb, Cr) CLIP( Y + ( 91881 * Cr >> 16 ) - 179 )
#define CYCbCr2G(Y, Cb, Cr) CLIP( Y - (( 22544 * Cb + 46793 * Cr ) >> 16) + 135)
#define CYCbCr2B(Y, Cb, Cr) CLIP( Y + (116129 * Cb >> 16 ) - 226 )
#endif 

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

static void save_gray_frame(unsigned char *buf_y, unsigned char *buf_cb, unsigned char *buf_cr, 
                            int wrap_y, int wrap_cb, int wrap_cr, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P6\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
    {
        for (int j = 0; j < xsize; j++)
        {
            uint64_t Y_index = i * wrap_y + j;
            uint64_t CB_index = (i/2) * wrap_cb +(j/2);
            uint64_t CR_index = (i/2) * wrap_cr +(j/2);

            int32_t Y_b  = (((int32_t)*(buf_y + Y_index)));
            int32_t Cb_b = (((int32_t)*(buf_cb + CB_index)));
            int32_t Cr_b = (((int32_t)*(buf_cr + CR_index)));

            unsigned char r_c = (unsigned char)CYCbCr2R(Y_b, Cb_b, Cr_b);
            unsigned char g_c = (unsigned char)CYCbCr2G(Y_b, Cb_b, Cr_b);
            unsigned char b_c = (unsigned char)CYCbCr2B(Y_b, Cb_b, Cr_b);
//            fprintf(f, "%u ", r_c);
//            fprintf(f, "%u ", g_c);
//            fprintf(f, "%u\n", b_c);

            fwrite(&r_c, 1, 1, f);
            fwrite(&g_c, 1, 1, f);
            fwrite(&b_c, 1, 1, f);
        }
    }
    fclose(f);
}