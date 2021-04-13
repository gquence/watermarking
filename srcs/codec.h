#ifndef CODEC_H
# define CODEC_H
# include "common.h"

extern "C"
{
    #include <libavformat/avformat.h>
}

# include <iostream>
# include <vector>
# include <tuple>

/**
 * Codec_info structure.
 * Contains all information about Codecs and Streams that using it
 */
typedef struct      s_codec_info
{
    int                 input_stream_index;
    AVFormatContext     *input_fctx;
    AVStream            *input_stream;
    int                 output_stream_index;
    AVFormatContext     *output_fctx;
    AVStream            *output_stream;
    AVCodecParameters   *codecpar;
    AVCodecContext      *CodecContext;
    AVCodec             *Codec;
}                   t_codec_info;

/**
 * @brief Get the video codecs from streams vector object
 * 
 * @param streamArr 
 * @param codecArr 
 * @return int Zero on success, a negative value on openning error, One if codec is not supported
 */
int get_video_codecs_from_streams_vector(const std::vector<AVStream*> &streamArr,
                                         std::vector<t_codec_info> &codecArr);

/**
 * @brief 
 * 
 * @param codecArr 
 */
void    free_codec_arr(std::vector<t_codec_info> &codecArr);

#if 0 //!!!!!!!!!!!!!!!!!!! AUDIO CODEC !!!!!!!!!!!!!!!!!!!
int get_audio_codecs_from_streams_vector(const std::vector<AVStream*> &streamArr, std::vector<AVCodec *> &codecArr)
{
    int func_ret;

    for (auto &stream : streamArr)
    {
        AVCodecParameters* cpar = stream->codecpar;
        switch (cpar->codec_type)
        {
        case AVMEDIA_TYPE_AUDIO:
            std::cout << "Audio Codec:" << cpar->channels <<
                " channels, sample rate "<< cpar->sample_rate << std::endl;
            break;
        default:
            continue;
        }
    }
    return 0; 
}
#endif

#endif