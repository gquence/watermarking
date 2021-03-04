#include "format.h"

int get_fctx_from_file(const std::string &filename, AVFormatContext **fctx)
{
    int func_ret;

    func_ret = avformat_open_input(fctx, filename.c_str(), nullptr, nullptr);
    if (func_ret != 0)
    {
        #if DEBUG == 1
        std::cerr << "Error when opening file: "  << filename << ". Err_code = "
            << func_ret << std::endl;
        #endif
        return -1;
    }
    return 0;
}

int get_all_streams(AVFormatContext *fctx, std::vector<AVStream*> &streamArr)
{
    int func_ret;

    func_ret = avformat_find_stream_info(fctx, nullptr);
    if (func_ret != 0)
    {
        #if DEBUG == 1
        std::cerr << "Error find stream info. Err_code = "
            << func_ret << std::endl;
        #endif
        return -1;
    }

    for (size_t i = 0u; i < fctx->nb_streams; i++)
    {
        AVStream *stream;
        switch (fctx->streams[i]->codecpar->codec_type)
        {
            case AVMEDIA_TYPE_VIDEO:
                stream = fctx->streams[i];
                break ;
            case AVMEDIA_TYPE_AUDIO:
                stream = fctx->streams[i];
                break ;
            default:
                #if DEBUG == 1
                std::cout << "Found not audio or video stream. Index = " << i << std::endl;
                #endif
                continue;
        }
        streamArr.push_back(stream);
    }
    return 0;
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

int create_corresponding_basic_streams(const std::vector<AVStream*> &streamArr, AVFormatContext *fctx)
{
    AVStream    *out_stream;
    int         func_ret;

    for (auto i : streamArr)
    {
        out_stream = avformat_new_stream(fctx, nullptr);
        if (!out_stream)
        {
            #if DEBUG == 1
            std::cerr << "Error when allocating new stream. Index = " << i->index << std::endl;
            #endif
            return -1;
        }
        func_ret = avcodec_parameters_copy(out_stream->codecpar, i->codecpar);
        if (func_ret < 0)
        {
            #if DEBUG == 1
            std::cerr << "Failed to copy codec parameters. Index = " << i->index << std::endl;
            #endif
            free_all_stream_from_fctx(fctx);
            return func_ret;
        }
    }
    return 0;
}

void free_all_stream_from_fctx(AVFormatContext *fctx)
{
    for (int i = fctx->nb_streams - 1; i >= 0; i--)
    {
          av_freep(fctx->streams[i]);
    }
}
