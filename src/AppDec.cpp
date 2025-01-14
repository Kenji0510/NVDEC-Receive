/*
* Copyright 2017-2024 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

//---------------------------------------------------------------------------
//! \file AppDec.cpp
//! \brief Source file for AppDec sample
//!
//! This sample application illustrates the demuxing and decoding of a media file followed by resize and crop of the output frames.
//! The application supports both planar (YUV420P and YUV420P16) and non-planar (NV12 and P016) output formats.
//---------------------------------------------------------------------------

#include <iostream>
#include <algorithm>
#include <thread>
#include <cuda.h>
#include "NvDecoder/NvDecoder.h"
#include "../Utils/NvCodecUtils.h"
#include "../Utils/FFmpegDemuxer.h"
#include "../Common/AppDecUtils.h"

#include <libavcodec/avcodec.h>

#include "AppDec.hpp"
#include "globals.h"

#define MAX_VPACKET_SIZE 300000

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();


void ConvertSemiplanarToPlanar(uint8_t *pHostFrame, int nWidth, int nHeight, int nBitDepth) {
    if (nBitDepth == 8) {
        // nv12->iyuv
        YuvConverter<uint8_t> converter8(nWidth, nHeight);
        converter8.UVInterleavedToPlanar(pHostFrame);
    } else {
        // p016->yuv420p16
        YuvConverter<uint16_t> converter16(nWidth, nHeight);
        converter16.UVInterleavedToPlanar((uint16_t *)pHostFrame);
    }
}

/**
*   @brief  Function to decode media file and write raw frames into an output file.
*   @param  cuContext     - Handle to CUDA context
*   @param  szInFilePath  - Path to file to be decoded
*   @param  szOutFilePath - Path to output file into which raw frames are stored
*   @param  bOutPlanar    - Flag to indicate whether output needs to be converted to planar format
*   @param  cropRect      - Cropping rectangle coordinates
*   @param  resizeDim     - Resizing dimensions for output
*   @param  opPoint       - Select an operating point of an AV1 scalable bitstream
*   @param  bDispAllLayers - Output all decoded frames of an AV1 scalable bitstream
*   @param  bExtractUserSEIMessage - Output unregistered user SEI messages in display order
*/

/*
AV_CODEC_ID_H264    // H.264
AV_CODEC_ID_HEVC    // HEVC (H.265)
AV_CODEC_ID_VP9     // VP9
AV_CODEC_ID_AV1     // AV1
*/

void DecodeMediaFile(
    CUcontext cuContext, const char *szInFilePath, const char *szOutFilePath, bool bOutPlanar,
    const Rect &cropRect, const Dim &resizeDim, const unsigned int opPoint, const bool bDispAllLayers, const bool bExtractUserSEIMessage,
    thread_args* thd_args
    )
{
    std::ofstream fpOut(szOutFilePath, std::ios::out | std::ios::binary);
    if (!fpOut)
    {
        std::ostringstream err;
        err << "Unable to open output file: " << szOutFilePath << std::endl;
        throw std::invalid_argument(err.str());
    }

    std::vector<uint8_t> vPacketData(MAX_VPACKET_SIZE);

    /*
    FFmpegDemuxer demuxer(szInFilePath);
    NvDecoder dec(cuContext, false, FFmpeg2NvCodecId(demuxer.GetVideoCodec()), false, false, &cropRect, &resizeDim, bExtractUserSEIMessage);
    */
    // With HEVC
    // AV_CODEC_ID_HEVC = 173
    NvDecoder dec(cuContext, false, FFmpeg2NvCodecId(AV_CODEC_ID_HEVC), false, false, &cropRect, &resizeDim, bExtractUserSEIMessage);

    /* Set operating point for AV1 SVC. It has no impact for other profiles or codecs
     * PFNVIDOPPOINTCALLBACK Callback from video parser will pick operating point set to NvDecoder  */
    dec.SetOperatingPoint(opPoint, bDispAllLayers);

    int nVideoBytes = 0, nFrameReturned = 0, nFrame = 0;
    uint8_t *pVideo = NULL, *pFrame;
    bool bDecodeOutSemiPlanar = false;
    size_t total_data_size = 0;
    do {
        sem_wait(&(thd_args->sem_write_thread));
        std::cout << "aaa" << std::endl;
        pthread_mutex_lock(&(thd_args->mutex));
        //pthread_cond_wait(&(thd_args->cond), &(thd_args->mutex));

        nVideoBytes = int(thd_args->frame_data_info->data_size);
        memcpy(vPacketData.data(), thd_args->data, thd_args->frame_data_info->data_size);
        std::cout << "AppDec: Data size: " << thd_args->frame_data_info->data_size << std::endl;

        pthread_mutex_unlock(&(thd_args->mutex));
        sem_post(&(thd_args->sem_read_thread));

        if (nVideoBytes == 999999) {
            std::cout << "AppDec: End of file" << std::endl;
            break;
        }

        total_data_size += thd_args->frame_data_info->data_size;
        std::cout << "Total data size: " << total_data_size << std::endl;

        //demuxer.Demux(&pVideo, &nVideoBytes);
        //nVideoBytes = vPacketData.size();
        //nFrameReturned = dec.Decode(pVideo, nVideoBytes);
        nFrameReturned = dec.Decode(vPacketData.data(), nVideoBytes);
        if (!nFrame && nFrameReturned)
            LOG(INFO) << dec.GetVideoInfo();
        
        bDecodeOutSemiPlanar = (dec.GetOutputFormat() == cudaVideoSurfaceFormat_NV12) || (dec.GetOutputFormat() == cudaVideoSurfaceFormat_P016);
        
        for (int i = 0; i < nFrameReturned; i++) {
            pFrame = dec.GetFrame();
            if (bOutPlanar && bDecodeOutSemiPlanar) {
                ConvertSemiplanarToPlanar(pFrame, dec.GetWidth(), dec.GetHeight(), dec.GetBitDepth());
            }
            // dump YUV to disk
            if (dec.GetWidth() == dec.GetDecodeWidth())
            {
                fpOut.write(reinterpret_cast<char*>(pFrame), dec.GetFrameSize());
            }
            else
            {
                // 4:2:0 output width is 2 byte aligned. If decoded width is odd , luma has 1 pixel padding
                // Remove padding from luma while dumping it to disk
                // dump luma
                for (auto i = 0; i < dec.GetHeight(); i++)
                {
                    fpOut.write(reinterpret_cast<char*>(pFrame), dec.GetDecodeWidth()*dec.GetBPP());
                    pFrame += dec.GetWidth() * dec.GetBPP();
                }
                // dump Chroma
                fpOut.write(reinterpret_cast<char*>(pFrame), dec.GetChromaPlaneSize());
            }
            // Added for debugging
            std::cout << "pFrame[0]" << static_cast<int>(pFrame[0]) << std::endl;
            std::cout << "pFrame[1]" << static_cast<int>(pFrame[1]) << std::endl;
        }
        nFrame += nFrameReturned;
        std::cout << "nVideoBytes: " << nVideoBytes << std::endl;
    //} while (nVideoBytes);
    } while (nVideoBytes != 999999);

    std::vector <std::string> aszDecodeOutFormat = { "NV12", "P016", "YUV444", "YUV444P16" };
    if (bOutPlanar) {
        aszDecodeOutFormat[0] = "iyuv";   aszDecodeOutFormat[1] = "yuv420p16";
    }
    std::cout << "Total frame decoded: " << nFrame << std::endl
            << "Saved in file " << szOutFilePath << " in "
            << aszDecodeOutFormat[dec.GetOutputFormat()]
            << " format" << std::endl;
    fpOut.close();
}

void ShowHelpAndExit(const char *szBadOption = NULL)
{
    bool bThrowError = false;
    std::ostringstream oss;
    if (szBadOption) 
    {
        bThrowError = true;
        oss << "Error parsing \"" << szBadOption << "\"" << std::endl;
    }
    oss << "Options:" << std::endl
        << "-i             Input file path" << std::endl
        << "-o             Output file path" << std::endl
        << "-outplanar     Convert output to planar format" << std::endl
        << "-gpu           Ordinal of GPU to use" << std::endl
        << "-crop l,t,r,b  Crop rectangle in left,top,right,bottom (ignored for case 0)" << std::endl
        << "-resize WxH    Resize to dimension W times H (ignored for case 0)" << std::endl
        << "-oppoint n     Select an operating point of an AV1 scalable bitstream" << std::endl
        << "-alllayers     Output all decoded frames of an AV1 scalable bitstream" << std::endl
        << "-extractUserSEIMessage Output unregistered user SEI messages in display order" << std::endl
        ;
    oss << std::endl;
    if (bThrowError)
    {
        throw std::invalid_argument(oss.str());
    }
    else
    {
        std::cout << oss.str();
        ShowDecoderCapability();
        exit(0);
    }
}

void ParseCommandLine(int argc, char *argv[], char *szInputFileName, char *szOutputFileName,
    bool &bOutPlanar, int &iGpu, Rect &cropRect, Dim &resizeDim, unsigned int &opPoint, bool &bDispAllLayers, bool &bExtractUserSEIMessage)
{
    std::ostringstream oss;
    int i;
    bDispAllLayers = false;
    opPoint = 0;
    for (i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "-h")) {
            ShowHelpAndExit();
        }
        if (!_stricmp(argv[i], "-i")) {
            if (++i == argc) {
                ShowHelpAndExit("-i");
            }
            sprintf(szInputFileName, "%s", argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-o")) {
            if (++i == argc) {
                ShowHelpAndExit("-o");
            }
            sprintf(szOutputFileName, "%s", argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-outplanar")) {
            bOutPlanar = true;
            continue;
        }
        if (!_stricmp(argv[i], "-gpu")) {
            if (++i == argc) {
                ShowHelpAndExit("-gpu");
            }
            iGpu = atoi(argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-crop")) {
            if (++i == argc || 4 != sscanf(
                    argv[i], "%d,%d,%d,%d",
                    &cropRect.l, &cropRect.t, &cropRect.r, &cropRect.b)) {
                ShowHelpAndExit("-crop");
            }
            if ((cropRect.r - cropRect.l) % 2 == 1 || (cropRect.b - cropRect.t) % 2 == 1) {
                std::cout << "Cropping rect must have width and height of even numbers" << std::endl;
                exit(1);
            }
            continue;
        }
        if (!_stricmp(argv[i], "-resize")) {
            if (++i == argc || 2 != sscanf(argv[i], "%dx%d", &resizeDim.w, &resizeDim.h)) {
                ShowHelpAndExit("-resize");
            }
            if (resizeDim.w % 2 == 1 || resizeDim.h % 2 == 1) {
                std::cout << "Resizing rect must have width and height of even numbers" << std::endl;
                exit(1);
            }
            continue;
        }
        if (!_stricmp(argv[i], "-oppoint")) {
            if (++i == argc ) {
                ShowHelpAndExit("-oppoint");
            }
            opPoint = atoi(argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-alllayers")) {
            
            bDispAllLayers = true;
            continue;
        }
        if (!_stricmp(argv[i], "-extractUserSEIMessage")) {
            bExtractUserSEIMessage = true;
            continue;
        }
        ShowHelpAndExit(argv[i]);
    }
}

void* nvdec(void* args) 
{   
    thread_args* thd_args = (thread_args*)args;

    char szInFilePath[256] = "", szOutFilePath[256] = "";
    bool bOutPlanar = false;
    int iGpu = 0;
    Rect cropRect = {};
    Dim resizeDim = {};
    unsigned int opPoint = 0;
    bool bDispAllLayers = false;
    bool bExtractUserSEIMessage = false;
    try
    {
        ParseCommandLine(thd_args->argc, thd_args->argv, szInFilePath, szOutFilePath, bOutPlanar, iGpu, cropRect, resizeDim, opPoint, bDispAllLayers, bExtractUserSEIMessage);
        CheckInputFile(szInFilePath);

        if (!*szOutFilePath) {
            sprintf(szOutFilePath, bOutPlanar ? "out.planar" : "out.native");
        }

        ck(cuInit(0));
        int nGpu = 0;
        ck(cuDeviceGetCount(&nGpu));
        if (iGpu < 0 || iGpu >= nGpu) {
            std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
            //return 1;
            return NULL;
        }

        CUcontext cuContext = NULL;
        createCudaContext(&cuContext, iGpu, 0);

        std::cout << "Decode with demuxing." << std::endl;
        DecodeMediaFile(cuContext, szInFilePath, szOutFilePath, bOutPlanar, cropRect, resizeDim, opPoint, bDispAllLayers, bExtractUserSEIMessage, thd_args);
    }
    catch (const std::exception& ex)
    {
        std::cout << ex.what();
        exit(1);
    }

    //return 0;
    return NULL;
}


/*
int main(int argc, char **argv) 
{
    char szInFilePath[256] = "", szOutFilePath[256] = "";
    bool bOutPlanar = false;
    int iGpu = 0;
    Rect cropRect = {};
    Dim resizeDim = {};
    unsigned int opPoint = 0;
    bool bDispAllLayers = false;
    bool bExtractUserSEIMessage = false;
    try
    {
        ParseCommandLine(argc, argv, szInFilePath, szOutFilePath, bOutPlanar, iGpu, cropRect, resizeDim, opPoint, bDispAllLayers, bExtractUserSEIMessage);
        CheckInputFile(szInFilePath);

        if (!*szOutFilePath) {
            sprintf(szOutFilePath, bOutPlanar ? "out.planar" : "out.native");
        }

        ck(cuInit(0));
        int nGpu = 0;
        ck(cuDeviceGetCount(&nGpu));
        if (iGpu < 0 || iGpu >= nGpu) {
            std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
            return 1;
        }

        CUcontext cuContext = NULL;
        createCudaContext(&cuContext, iGpu, 0);

        std::cout << "Decode with demuxing." << std::endl;
        DecodeMediaFile(cuContext, szInFilePath, szOutFilePath, bOutPlanar, cropRect, resizeDim, opPoint, bDispAllLayers, bExtractUserSEIMessage);
    }
    catch (const std::exception& ex)
    {
        std::cout << ex.what();
        exit(1);
    }

    return 0;
}
*/