#include "format.h"

int get_fctx_from_file(const std::string &filename, AVFormatContext **fctx, AVDictionary **opts)
{
    int func_ret;

    func_ret = avformat_open_input(fctx, filename.c_str(), nullptr, opts);
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
    bool video_exist = false;
    
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
                video_exist = true;
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
    if (!video_exist)
    {
        streamArr.clear();
        #if DEBUG == 1
        std::cerr << "There is no Video Stream in file = " << fctx->filename
            << ". Err_code = " << func_ret << std::endl;
        #endif
        return -2;
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

int create_corresponding_basic_streams(std::vector<t_codec_info> &codecArr, AVFormatContext *fctx)
{
    AVStream    *out_stream;
    int         func_ret;
    
    for (auto &codec_info : codecArr)
    {
        auto in_stream = codec_info.input_stream;
        out_stream = avformat_new_stream(fctx, nullptr);
        out_stream->id = fctx->nb_streams - 1;
        if (!out_stream)
        {
            #if DEBUG == 1
            std::cerr << "Error when allocating new stream. Index = " << in_stream->index << std::endl;
            #endif
            return -1;
        }
        func_ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        out_stream->time_base = in_stream->time_base;
        if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            fctx->oformat->video_codec = out_stream->codecpar->codec_id;
        }
        else if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            fctx->oformat->audio_codec = out_stream->codecpar->codec_id;
        }
        if (func_ret < 0)
        {
            #if DEBUG == 1
            std::cerr << "Failed to copy codec parameters. Index = " << in_stream->index << std::endl;
            #endif
            free_all_stream_from_fctx(fctx);
            return func_ret;
        }
        codec_info.output_stream = out_stream;
        codec_info.output_stream_index = out_stream->id;
    }
    return 0;
}

int open_IOctx(AVFormatContext *fctx)
{
    int func_ret;

    //av_dump_format(fctx, 0, filename.c_str(), 1);
    if (fctx->oformat->flags  & AVFMT_GLOBALHEADER)
        fctx->oformat->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (!(fctx->oformat->flags & AVFMT_NOFILE))
    {
        func_ret = avio_open(&(fctx->pb), (const char *)fctx->filename, AVIO_FLAG_WRITE);
        if (func_ret < 0)
        {
            #if DEBUG == 1
                std::cerr << "Error when openning AVIOContext" << std::endl;
            #endif
            return -1;
        }
        return 0;
    }
    #if DEBUG == 1
        std::cerr << "Error when openning AVIOContext" << std::endl;
    #endif
    return 1;
}

int fill_streams_codec_info(AVStream *stream, AVCodec **codec, AVCodecContext **codec_ctx)
{
    *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!*codec)
    {
        #if DEBUG == 1
            std::cerr << "failed to find the codec";
        #endif
        return -1;
    }

    *codec_ctx = avcodec_alloc_context3(*codec);
    if (!*codec_ctx)
    {
        
        #if DEBUG == 1
            std::cout << "failed to alloc memory for codec context";
        #endif
        return -1;}

    if (avcodec_parameters_to_context(*codec_ctx, stream->codecpar) < 0)
    {
        #if DEBUG == 1
            std::cout << "failed to fill codec context";
        #endif
        return -1;
    }

    if (avcodec_open2(*codec_ctx, *codec, NULL) < 0) 
    {
        #if DEBUG == 1
            std::cout << "failed to open codec";
        #endif
        return -1;
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
