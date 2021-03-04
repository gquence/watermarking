#ifndef FORMAT_H
#define FORMAT_H
#include "common.h"

extern "C"
{
    #include <libavformat/avformat.h>
}
#include <string>
#include <iostream>
#include <vector>
#include <utility>

/**
 * @brief Get the fctx from file
 * 
 * @param[in] filename name of input file
 * @param[out] fctx an instance that contains all info about media-file
 * @return Zero on success, a negative value on error
 */
int get_fctx_from_file(const std::string &filename, AVFormatContext **fctx);

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
 * @brief Create a corresponding streams object
 * 
 * @param[in] streamArr source array with streams and their codec params
 * @param[in/out] fctx dest format context(must be basicly initialized)
 * @return  Zero on success, a negative value on error
 */
int create_corresponding_basic_streams(const std::vector<AVStream*> &streamArr, AVFormatContext *fctx);

/**
 * 
 * @param fctx 
 */
void free_all_stream_from_fctx(AVFormatContext *fctx);

#endif