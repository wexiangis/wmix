/**************************************************
 * 
 *  基于libaac、libaad库的接口二次封装。
 * 
 **************************************************/
#ifndef _AACTYPE_H_
#define _AACTYPE_H_

#include <stdint.h>

//接收来自Makefile的传参,没有定义则自己定义
#ifndef MAKE_AAC
#define MAKE_AAC 1
#endif

//aac的头字段,共7字节,注意每字节中低位数据放在在前面(实际bit流高位在先)
typedef struct
{
    //byte 1
    uint8_t syncwordH : 8; //8 bit 同步字高位 0xFF,说明一个ADTS帧的开始
    //byte 2
    uint8_t protectionAbsent : 1; //1 bit 1表示没有crc,0表示有crc
    uint8_t layer : 2;            //2 bit 总是'00'
    uint8_t id : 1;               //1 bit MPEG 标示符, 0 for MPEG-4,1 for MPEG-2
    uint8_t syncwordL : 4;        //4 bit 同步字低位 0xF,说明一个ADTS帧的开始
    //byte 3
    uint8_t chnH : 1;              //1 bit 表示声道数(高位)
    uint8_t privateBit : 1;        //1 bit
    uint8_t samplingFreqIndex : 4; //4 bit 表示使用的采样频率
    uint8_t profile : 2;           //2 bit 表示使用哪个级别的AAC
    //byte 4
    uint8_t aacFrameLengthH : 2;              //2 bit 帧长度高位(一个ADTS帧的长度包括ADTS头和AAC原始流)
    uint8_t copyrightIdentificationStart : 1; //1 bit
    uint8_t copyrightIdentificationBit : 1;   //1 bit
    uint8_t home : 1;                         //1 bit
    uint8_t originalCopy : 1;                 //1 bit
    uint8_t chnL : 2;                         //2 bit 表示声道数(低位)
    //byte 5
    uint8_t aacFrameLengthM : 8; //8 bit 帧长度中位(一个ADTS帧的长度包括ADTS头和AAC原始流)
    //byte 6
    uint8_t adtsBufferFullnessH : 5; //5 bit 0x7 说明是码率可变的码流高位
    uint8_t aacFrameLengthL : 3;     //3 bit 帧长度低位(一个ADTS帧的长度包括ADTS头和AAC原始流)
    //byte 7
    uint8_t numberOfRawDataBlockInFrame : 2; //2 bits 表示ADTS帧中有 该值+1个AAC原始帧,一般为0
    uint8_t adtsBufferFullnessL : 6;         //6 bit 0xFF 说明是码率可变的码流低位
} AacHeader;

//返回0成功
int aac_parseHeader(AacHeader *head, uint8_t *chn, uint16_t *freq, uint16_t *frameLen, uint8_t show);

//返回总长度, 7 + datLen
//codeRate: 该包aac数据解包成pcm的数据长度,典型值2048
int aac_createHeader(AacHeader *head, uint8_t chn, uint16_t freq, uint16_t codeRate, uint16_t datLen);

//------------------ faac, faad ------------------

#if (MAKE_AAC)

/*
 *  aac解码为pcm
 *  参数:
 *      aacDec: 解码器句柄,值为NULL时自动初始化
 *      in: aac数据,建议读入数据长度2048
 *      inLen: aac数据长度
 *      out: 输出pcm数据长度,建议长度8192
 *      bytesConsumed: 已使用in数据长度,用于in数据偏移,返回0时表示缺少数据量
 *  返回: pcm数据长度, -1/解析aac头失败, 0/数据不足,bytesConsumed返回缺少数据量
 */
int aac_decode(void **aacDec, uint8_t *in, int inLen, uint8_t *out, int *bytesConsumed, int *chn, int *freq);

/*
 *  aac解码为pcm
 *  参数:
 *      aacDec: 解码器句柄,值为NULL时自动初始化
 *      aacFile_fd: 已打开的aac文件句柄
 *      out: 返回数据缓冲区,要求大于等于8192
 *  返回: pcm数据长度, -1失败
 */
int aac_decode2(void **aacDec, int aacFile_fd, uint8_t *out, int *chn, int *freq);

//文件
void aac_decodeToFile(char *aacFile, char *pcmFile);

//销毁解码器句柄
void aac_decodeRelease(void **aacDec);

/*
 *  pcm编码为aac
 *  参数:
 *      aacEnc: 解码器句柄,值为NULL时自动初始化
 *      in: 长度必须为2048*chn
 *      inLen: 2048*chn
 *      out: 长度大于等于4096
 *      outSize: 4096
 *  返回: 实际写入ouy的数据量
 */
int aac_encode(void **aacEnc, uint8_t *in, int inLen, uint8_t *out, uint32_t outSize, int chn, int freq);
//文件
void aac_encodeToFile(char *pcmFile, char *aacFile, int chn, int freq);
void aac_encodeToFile2(int pcmFile_fd, int aacFile_fd, int chn, int freq);
//销毁编码器句柄
void aac_encodeRelease(void **aacEnc);

#endif // #if (MAKE_AAC)

#endif // end of file
