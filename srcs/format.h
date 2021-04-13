#ifndef FORMAT_H
#define FORMAT_H
#include "common.h"
#include "codec.h"

extern "C"
{
    #include <libavformat/avformat.h>
}
#include <string>
#include <iostream>
#include <vector>

/**
 * @brief Get the fctx from file
 * 
 * @param[in] filename name of input file
 * @param[out] fctx an instance that contains all info about media-file
 * @param[out] opts options with information for (de)compressing media-file
 * 
 * @return Zero on success, a negative value on error
 */
int get_fctx_from_file(const std::string &filename, AVFormatContext **fctx, AVDictionary **opts);

/**
 * @brief Get information about
 * 
 * @param[in] fctx format context must allocated before passing to the function
 *      (for example, it can be inialized in get_fctx_from_file)
 * @param[out] streamArr array with streams and their codec params
 *          that will be inialized inside function
 *        
 * @return Zero on success, a negative value on error
 */
int get_all_streams(AVFormatContext *fctx, std::vector<AVStream*> &streamArr);

/**
 * @brief Create a fctx object
 * 
 * @param[in] filename name of output file
 * @param[out] fctx format context will be allocated inside the function
 * @return Zero on success, a negative value on error
 */
int create_fctx(const std::string &filename, AVFormatContext **fctx);

/**
 * @brief Create a corresponding streams inside foramt context
 * 
 * @param[in] streamArr source array with streams and their codec params
 * @param[in/out] fctx dest format context(must be basicly initialized)
 * @return  Zero on success, a negative value on error
 */
int create_corresponding_basic_streams(std::vector<t_codec_info> &codecArr, AVFormatContext *fctx);

/**
 * @brief Open a AVIOContext
 * 
 * @param[in/out] fctx dest format context
 * @return int Zero on success, a negative value on openning error, One if fctx is not file
 */
int open_IOctx(AVFormatContext *fctx);

/**
 * @brief 
 * 
 * @param stream 
 * @param codec 
 * @param codec_ctx 
 * @return int 
 */
int fill_streams_codec_info(AVStream *stream, AVCodec **codec, AVCodecContext **codec_ctx);

/**
 * 
 * @param fctx format context which streams must to freed
 */
void free_all_stream_from_fctx(AVFormatContext *fctx);

#endif