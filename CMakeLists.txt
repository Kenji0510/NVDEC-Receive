cmake_minimum_required(VERSION 3.7)

project(AppDec)

set(APP_SOURCES
${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
${CMAKE_CURRENT_SOURCE_DIR}/src/globals.c
${CMAKE_CURRENT_SOURCE_DIR}/src/read_data_from_shm.cpp
 ${CMAKE_CURRENT_SOURCE_DIR}/src/AppDec.cpp
)

# NVIDIA Video Codec SDKのパスを指定
set(NV_CODEC_SDK_PATH "/home/kenji/Video_Codec_SDK_12.2.72")

# インクルードディレクトリを設定
include_directories(
    ${CUDA_INCLUDE_DIRS}
    ${NV_CODEC_SDK_PATH}/Interface  # NVENCのヘッダファイルディレクトリ
    ${NV_CODEC_SDK_PATH}/Samples/NvCodec  # NvEncoderCuda.hのあるディレクトリ
    ${NV_CODEC_SDK_PATH}/Samples/Utils
)
set(NV_DEC_DIR /home/kenji/Video_Codec_SDK_12.2.72/Samples/NvCodec/NvDecoder)
set(NVCODEC_PUBLIC_INTERFACE_DIR /home/kenji/Video_Codec_SDK_12.2.72/Interface)
set(NVCODEC_UTILS_DIR /home/kenji/Video_Codec_SDK_12.2.72/Samples/Utils)
set(NV_APPDEC_COMMON_DIR /home/kenji/Video_Codec_SDK_12.2.72/Samples/AppDecode/Common)

set(NV_DEC_SOURCES
 ${NV_DEC_DIR}/NvDecoder.cpp
)

set(NV_DEC_HDRS
 ${NV_DEC_DIR}/NvDecoder.h
 ${NVCODEC_PUBLIC_INTERFACE_DIR}/cuviddec.h
 ${NVCODEC_PUBLIC_INTERFACE_DIR}/nvcuvid.h
 ${NVCODEC_UTILS_DIR}/NvCodecUtils.h
 ${NVCODEC_UTILS_DIR}/FFmpegDemuxer.h
 ${NV_APPDEC_COMMON_DIR}/AppDecUtils.h
)

if(WIN32)
    set(NV_FFMPEG_HDRS ${FFMPEG_DIR}/include)
endif()

source_group( "headers" FILES ${NV_DEC_HDRS} )
source_group( "sources" FILES ${APP_SOURCES} ${NV_DEC_SOURCES} )

find_package(CUDA)

set(CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER})

set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS};-gencode arch=compute_50,code=\"sm_50,compute_50\")
if ( CMAKE_COMPILER_IS_GNUCC )
    if(NOT "${CUDA_NVCC_FLAGS}" MATCHES "-std=c\\+\\+11" )
        list(APPEND CUDA_NVCC_FLAGS -std=c++11)
    endif()
endif()

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

cuda_add_executable(${PROJECT_NAME}  ${APP_SOURCES} ${NV_DEC_SOURCES} ${NV_DEC_HDRS})

set_target_properties(${PROJECT_NAME} PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

target_include_directories(${PROJECT_NAME} PUBLIC ${CUDA_INCLUDE_DIRS}
 ${NVCODEC_PUBLIC_INTERFACE_DIR}
 ${NVCODEC_UTILS_DIR}
 ${NV_DEC_DIR}
 ${NV_APPDEC_COMMON_DIR}
 ${NV_FFMPEG_HDRS}
 ${NV_CODEC_DIR}
)

if(WIN32)
    Set(AVCODEC_LIB ${FFMPEG_DIR}/lib/avcodec.lib)
    Set(AVFORMAT_LIB ${FFMPEG_DIR}/lib/avformat.lib)
    Set(AVUTIL_LIB ${FFMPEG_DIR}/lib/avutil.lib)
    Set(SWRESAMPLE_LIB ${FFMPEG_DIR}/lib/swresample.lib)
    #copy dll's
    install(DIRECTORY ${FFMPEG_DLL_DIR} DESTINATION ${NVCODEC_SAMPLES_INSTALL_DIR} FILES_MATCHING PATTERN "*.dll")
    install(DIRECTORY ${FFMPEG_LIB_DIR} DESTINATION ${NVCODEC_SAMPLES_INSTALL_DIR} FILES_MATCHING PATTERN "*.lib")
endif()

target_link_libraries(${PROJECT_NAME} ${CUDA_CUDA_LIBRARY} ${CMAKE_DL_LIBS} ${NVENCODEAPI_LIB} ${CUVID_LIB} ${AVCODEC_LIB}
 ${AVFORMAT_LIB} ${AVUTIL_LIB} ${SWRESAMPLE_LIB} /home/kenji/Video_Codec_SDK_12.2.72/Lib/linux/stubs/x86_64/libnvcuvid.so
 /usr/lib/x86_64-linux-gnu/libavcodec.so /usr/lib/x86_64-linux-gnu/libavformat.so /usr/lib/x86_64-linux-gnu/libavutil.so /usr/lib/x86_64-linux-gnu/libswresample.so)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${NVCODEC_SAMPLES_INSTALL_DIR})
if (MSVC)
    set_target_properties( ${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${NVCODEC_SAMPLES_INSTALL_DIR}/$<CONFIG>/ )
    add_dependencies(${PROJECT_NAME} copyFFMPEGFiles)
    #copy pdb's
    install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION ${NVCODEC_SAMPLES_INSTALL_DIR}  CONFIGURATIONS Debug)
endif()

