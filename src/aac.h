/**************************************************
 * 
 *  基于libaac、libaad库的接口二次封装。
 * 
 **************************************************/
#ifndef _AAC_H_
#define _AAC_H_

#include <stdint.h>

//aac的头字段,共7字节
typedef struct
{
    uint32_t syncword;          //12 bit 同步字 '1111 1111 1111'，说明一个ADTS帧的开始
    uint32_t id;                //1 bit MPEG 标示符， 0 for MPEG-4，1 for MPEG-2
    uint32_t layer;             //2 bit 总是'00'
    uint32_t protectionAbsent;  //1 bit 1表示没有crc，0表示有crc
    uint32_t profile;           //2 bit 表示使用哪个级别的AAC
    uint32_t samplingFreqIndex; //4 bit 表示使用的采样频率
    uint32_t privateBit;        //1 bit
    uint32_t channelCfg;        //3 bit 表示声道数
    uint32_t originalCopy;      //1 bit
    uint32_t home;              //1 bit

    /*下面的为改变的参数即每一帧都不同*/
    uint32_t copyrightIdentificationBit;   //1 bit
    uint32_t copyrightIdentificationStart; //1 bit
    uint32_t aacFrameLength;               //13 bit 一个ADTS帧的长度包括ADTS头和AAC原始流
    uint32_t adtsBufferFullness;           //11 bit 0x7FF 说明是码率可变的码流

    /* number_of_raw_data_blocks_in_frame
     * 表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧
     * 所以说number_of_raw_data_blocks_in_frame == 0 
     * 表示说ADTS帧中有一个AAC数据块并不是说没有。(一个AAC原始帧包含一段时间内1024个采样及相关数据)
     */
    uint32_t numberOfRawDataBlockInFrame; //2 bit
} AacHeader;

extern int aac_freqList[13];

//返回0成功
int aac_parseHeader(uint8_t *in, AacHeader *res, uint8_t show);

//返回总长度, 7 + datLen
//codeRate: 该包aac数据解包成pcm的数据长度,典型值2048
int aac_createHeader(uint8_t *in, uint8_t chn, uint16_t freq, uint16_t codeRate, uint16_t datLen);

//------------------ faac, faad ------------------

#define FAAX_ENABLE 1

#if (FAAX_ENABLE)

//aac解码为pcm
//aacDec: 解码器句柄,值为NULL时自动初始化
//in: aac数据,建议读入数据长度2048
//inLen: aac数据长度
//out: 输出pcm数据长度,建议长度8192
//bytesConsumed: 已使用in数据长度,用于in数据偏移,返回0时表示缺少数据量
//返回: pcm数据长度, -1/解析aac头失败, 0/数据不足,bytesConsumed返回缺少数据量
int aac_decode(void **aacDec, uint8_t *in, int inLen, uint8_t *out, int *bytesConsumed, int *chn, int *freq);

//aac解码为pcm
//aacDec: 解码器句柄,值为NULL时自动初始化
//aacFile_fd: 已打开的aac文件句柄
//out: 返回数据缓冲区,要求大于等于8192
//返回: pcm数据长度, -1失败
int aac_decode2(void **aacDec, int aacFile_fd, uint8_t *out, int *chn, int *freq);

//文件
void aac_decodeToFile(char *aacFile, char *pcmFile);

//销毁解码器句柄
void aac_decodeRelease(void **aacDec);

//pcm编码为aac
//aacEnc: 解码器句柄,值为NULL时自动初始化
//in: 长度必须为2048*chn
//inLen: 2048*chn
//out: 长度大于等于4096
//outSize: 4096
int aac_encode(void **aacEnc, uint8_t *in, int inLen, uint8_t *out, uint32_t outSize, int chn, int freq);
//文件
void aac_encodeToFile(char *pcmFile, char *aacFile, int chn, int freq);
void aac_encodeToFile2(int pcmFile_fd, int aacFile_fd, int chn, int freq);
//销毁编码器句柄
void aac_encodeRelease(void **aacEnc);

#endif

#endif
