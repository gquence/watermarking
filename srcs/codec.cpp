#include "codec.h"

int get_video_codecs_from_streams_vector(const std::vector<AVStream*> &streamArr,
                                         std::vector<t_codec_info> &codecArr)
{
    int func_ret;
    int func_res = 0;

    for (auto &stream : streamArr)
    {
        AVCodec         *plocalCodec;
        AVCodecContext  *CodecContext;
        t_codec_info    CInfo;

        CInfo.input_stream_index = stream->index;
        CInfo.input_stream = stream;
        CInfo.codecpar = stream->codecpar;
        
        switch (CInfo.codecpar->codec_type)
        {
        case AVMEDIA_TYPE_VIDEO:
            plocalCodec = avcodec_find_decoder(CInfo.codecpar->codec_id);
            if (plocalCodec == NULL)
            {
                #if DEBUG == 1
                std::cerr << "Error unsupported codec! Codec index = " << 
                    CInfo.codecpar->codec_id << std::endl;
                #endif
                func_res = 1;
                continue;
            }

            CodecContext = avcodec_alloc_context3(plocalCodec);
            if (CodecContext == NULL)
            {
                #if DEBUG == 1
                std::cerr << "Error in allocating avcodec context" << std::endl;
                #endif
                return -1;
            }

            func_ret = avcodec_parameters_to_context(CodecContext, CInfo.codecpar);
            if (func_ret != 0)
            {
                #if DEBUG == 1
                std::cerr << "Error on getting codec context from codec parameters. Err_code = "
                    << func_ret << std::endl;
                #endif
                avcodec_free_context(&CodecContext);
                return -1;
            }
            
            func_ret = avcodec_open2(CodecContext, plocalCodec, NULL);
            if (func_ret < 0)
            {
                #if DEBUG == 1
                std::cerr << "Error on openning codec context. Err_code = "
                    << func_ret << std::endl;
                #endif
                avcodec_free_context(&CodecContext);
                return -1;
            }
            CInfo.Codec = plocalCodec;
            CInfo.CodecContext = CodecContext;
            codecArr.push_back(CInfo);
            break;
        default:
            continue;
        }
    }
    return func_res;
}

void    free_codec_arr(std::vector<t_codec_info> &codecArr)
{
    for (auto i : codecArr)
    {
        avcodec_free_context(&i.CodecContext);
    }
    codecArr.clear();
}