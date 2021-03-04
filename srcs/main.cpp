#include "format.h"

#include <string>
#include <vector>
#include <iostream>

#include <unistd.h>

int main(int ac, char **av)
{
    int func_res = 0;
    std::string filename;
    AVFormatContext *input_fctx = nullptr;
    AVFormatContext *output_fctx = nullptr;
    std::vector<AVStream*> streamArr;

    if (ac == 2) {   filename = av[1];   }
    else {
        std::cerr << "usage: test_prog 'filename'" << std::endl;    
        return -1;
    }

    func_res = get_fctx_from_file(filename, &input_fctx);
    if (func_res < 0) goto ret_flag;

    func_res = get_all_streams(input_fctx, streamArr);
    if (func_res < 0) goto ret_flag;

    for (auto &stream : streamArr)
    {
        AVCodecParameters* cpar = stream->codecpar;
        switch (cpar->codec_type)
        {
        case AVMEDIA_TYPE_VIDEO:
            std::cout << "Video Codec: resolution" <<  cpar->width << "x" <<
                cpar->height <<  std::endl;
            break;
        case AVMEDIA_TYPE_AUDIO:
            std::cout << "Audio Codec:" << cpar->channels <<
                " channels, sample rate "<< cpar->sample_rate << std::endl;
            break;
        default:
            continue;
        }
    }

    func_res = create_fctx("lala.mp4", &output_fctx);
    if (func_res < 0) goto ret_flag;
    func_res = create_corresponding_basic_streams(streamArr, output_fctx);
    if (func_res < 0) goto ret_flag;
    
ret_flag:
    if (output_fctx != nullptr)
        avformat_free_context(output_fctx);
    if (input_fctx != nullptr)
        avformat_close_input(&input_fctx);
    return func_res;
}