#include <stdio.h>
#include "aac.h"

int aac_freqList[13] = {96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};

int aac_createHeader(uint8_t *in, uint8_t chn, uint16_t freq, uint16_t codeRate, uint16_t datLen)
{
    datLen += 7;
    
    //byte 1
    *in++ = 0xFF;//syncword[8]
    //byte 2
    *in++ = 0xF1;//syncword[4],id[1],layer[2],protectionAbsent[1]
    //byte 3
    *in = (0x1<<6);//profile[2]
    if(freq == 96000) *in |= (0x9<<2);//samplingFreqIndex[4]
    else if(freq == 88200) *in |= (0x1<<2);
    else if(freq == 64000) *in |= (0x2<<2);
    else if(freq == 48000) *in |= (0x3<<2);
    else if(freq == 44100) *in |= (0x4<<2);
    else if(freq == 32000) *in |= (0x5<<2);
    else if(freq == 24000) *in |= (0x6<<2);
    else if(freq == 22050) *in |= (0x7<<2);
    else if(freq == 16000) *in |= (0x8<<2);
    else if(freq == 12000) *in |= (0x9<<2);
    else if(freq == 11025) *in |= (0xa<<2);
    else if(freq == 8000) *in |= (0xb<<2);
    else if(freq == 7350) *in |= (0xc<<2);
    else *in |= (0x8<<2);
    *in |= (0x0<<1);//privateBit[1]
    *in++ |= (chn>>2);//channelCfg[1]
    //byte 4
    *in = ((chn&0x3)<<6);//channelCfg[2]
    *in |= (0x0<<5);//originalCopy[1]
    *in |= (0x0<<4);//home[1]
    *in |= (0x0<<3);//copyrightIdentificationBit[1]
    *in |= (0x0<<2);//copyrightIdentificationStart[1]
    *in++ |= (datLen>>11);//aacFrameLength[2]
    //byte 5
    *in++ = ((datLen>>3)&0xFF);//aacFrameLength[8]
    //byte 6
    *in = ((datLen&0x7)<<5);//aacFrameLength[3]
    *in++ |= (codeRate>>6);//adtsBufferFullness[5]
    //byte 7
    *in = ((codeRate&0x3F)<<2);//adtsBufferFullness[6]
    *in++ |= (0x0&0x3);//numberOfRawDataBlockInFrame[2]
    //
    return datLen;
}

int aac_parseHeader(uint8_t *in, AacHeader *res, uint8_t show)
{
    static int frame_number = 0;
    memset(res,0,sizeof(*res));

    if ((in[0] == 0xFF)&&((in[1] & 0xF0) == 0xF0))
    {
        res->id = ((unsigned int) in[1] & 0x08) >> 3;
        res->layer = ((unsigned int) in[1] & 0x06) >> 1;
        res->protectionAbsent = (unsigned int) in[1] & 0x01;
        res->profile = ((unsigned int) in[2] & 0xc0) >> 6;
        res->samplingFreqIndex = ((unsigned int) in[2] & 0x3c) >> 2;
        res->privateBit = ((unsigned int) in[2] & 0x02) >> 1;
        res->channelCfg = ((((unsigned int) in[2] & 0x01) << 2) | (((unsigned int) in[3] & 0xc0) >> 6));
        res->originalCopy = ((unsigned int) in[3] & 0x20) >> 5;
        res->home = ((unsigned int) in[3] & 0x10) >> 4;
        res->copyrightIdentificationBit = ((unsigned int) in[3] & 0x08) >> 3;
        res->copyrightIdentificationStart = (unsigned int) in[3] & 0x04 >> 2;
        res->aacFrameLength = (((((unsigned int) in[3]) & 0x03) << 11) |
                                (((unsigned int)in[4] & 0xFF) << 3) |
                                    ((unsigned int)in[5] & 0xE0) >> 5) ;
        res->adtsBufferFullness = (((unsigned int) in[5] & 0x1f) << 6 |
                                        ((unsigned int) in[6] & 0xfc) >> 2);
        res->numberOfRawDataBlockInFrame = ((unsigned int) in[6] & 0x03);

        if(show)
        {
            printf("adts:id  %d\n", res->id);
            printf( "adts:layer  %d\n", res->layer);
            printf( "adts:protection_absent  %d\n", res->protectionAbsent);
            printf( "adts:profile  %d\n", res->profile);
            printf( "adts:sf_index  %dHz\n", aac_freqList[res->samplingFreqIndex]);
            printf( "adts:pritvate_bit  %d\n", res->privateBit);
            printf( "adts:channel_configuration  %d\n", res->channelCfg);
            printf( "adts:original  %d\n", res->originalCopy);
            printf( "adts:home  %d\n", res->home);
            printf( "adts:copyright_identification_bit  %d\n", res->copyrightIdentificationBit);
            printf( "adts:copyright_identification_start  %d\n", res->copyrightIdentificationStart);
            printf( "adts:aac_frame_length  %d\n", res->aacFrameLength);
            printf( "adts:adts_buffer_fullness  %d\n", res->adtsBufferFullness);
            printf( "adts:no_raw_data_blocks_in_frame  %d\n", res->numberOfRawDataBlockInFrame);
        }

        return 0;
    }
    else
    {
        printf("failed to parse adts header\n");
        return -1;
    }
}

//------------------ faac, faad ------------------

#if(FAAX_ENABLE)

#include "unistd.h"
#include "fcntl.h"
#include "sys/types.h"

#include "faad.h"
#include "faac.h"

//aac解码为pcm
//aacDec: 解码器句柄,值为NULL时自动初始化
//in: aac数据,建议读入数据长度2048
//inLen: aac数据长度
//out: 输出pcm数据长度,建议长度8192
//bytesConsumed: 已使用in数据长度,用于in数据偏移,返回0时表示缺少数据量
//返回: pcm数据长度, -1/解析aac头失败, 0/数据不足,bytesConsumed返回缺少数据量
int aac_decode(void **aacDec, uint8_t* in, int inLen, uint8_t *out, int *bytesConsumed, int *chn, int *freq)
{
    if(!aacDec)
        return -1;
    //找到aac头
    int count = 0;
    AacHeader aacHead = {.aacFrameLength = 0};
    for(; count < inLen-7; count++)
    {
        if(in[0] == 0xFF && (in[1]&0xF0) == 0xF0)
        {
            if(aac_parseHeader(in, &aacHead, 0) == 0)
            {
                if(count > 0)
                    printf("break at %d\n", count);
                break;
            }
        }
        in++;
    }
    //检查是否足够一包数据
    if(aacHead.aacFrameLength == 0)
        return -1;
    else if(inLen - count < aacHead.aacFrameLength)
    {
        *bytesConsumed = aacHead.aacFrameLength - (inLen - count);
        return 0;
    }
    //第一次初始化解码器句柄
    NeAACDecHandle hDecoder = *((NeAACDecHandle*)aacDec);
    if(!hDecoder)
    {
        hDecoder = NeAACDecOpen();
        //初始化解码器
        NeAACDecInit(hDecoder, in, aacHead.aacFrameLength, freq, chn);
        //
        *aacDec = hDecoder;
    }
    //解码
    NeAACDecFrameInfo hInfo;
    uint8_t *ret;
    ret = (uint8_t*)NeAACDecDecode(hDecoder, &hInfo, in, aacHead.aacFrameLength);
    if(!ret || hInfo.error > 0)
    {
        printf("aac_decode: err %d [%s]\n", 
            hInfo.error, NeAACDecGetErrorMessage(hInfo.error));
        return -1;
    }
    //拷贝数据
    memcpy(out, ret, hInfo.samples*hInfo.channels);
    //参数返回
    *bytesConsumed = hInfo.bytesconsumed + count;
    *chn = hInfo.channels;
    *freq = hInfo.samplerate;
    //
    return hInfo.samples*hInfo.channels;
}

//aac解码为pcm
//aacDec: 解码器句柄,值为NULL时自动初始化
//aacFile_fd: 已打开的aac文件句柄
//out: 返回数据缓冲区,要求大于等于8192
//返回: pcm数据长度, -1失败
int aac_decode2(void **aacDec, int aacFile_fd, uint8_t *out, int *chn, int *freq)
{
    if(!aacDec)
        return -1;
    //
    AacHeader aacHead;
    uint8_t in[2048];
    //找到aac头
    do{
        if(read(aacFile_fd, in, 2) != 2)
            return -1;
        if(in[0] == 0xFF && (in[1]&0xF0) == 0xF0)
        {
            if(read(aacFile_fd, &in[2], 5) != 5)
                return -1;
            if(aac_parseHeader(in, &aacHead, 0) == 0)
                break;
        }
    }while(1);
    //
    if(read(aacFile_fd, &in[7], aacHead.aacFrameLength-7) != aacHead.aacFrameLength-7)
        return -1;
    //第一次初始化解码器句柄
    NeAACDecHandle hDecoder = *((NeAACDecHandle*)aacDec);
    if(!hDecoder)
    {
        hDecoder = NeAACDecOpen();
        //初始化解码器
        NeAACDecInit(hDecoder, in, aacHead.aacFrameLength, freq, chn);
        //
        *aacDec = hDecoder;
    }
    //解码
    NeAACDecFrameInfo hInfo;
    uint8_t *retp;
    retp = (uint8_t*)NeAACDecDecode(hDecoder, &hInfo, in, aacHead.aacFrameLength);
    if(!retp || hInfo.error > 0)
    {
        printf("aac_decode: err %d [%s]\n", 
            hInfo.error, NeAACDecGetErrorMessage(hInfo.error));
        return -1;
    }
    //拷贝数据
    size_t ret = hInfo.samples*hInfo.channels;
    if(ret > 0)
        memcpy(out, retp, ret);
    //参数返回
    *chn = hInfo.channels;
    *freq = hInfo.samplerate;
    //
    return ret;
}

void aac_decodeToFile(char *aacFile, char *pcmFile)
{
    void *aacDec = NULL;
    uint8_t *out = NULL;
    int ret = 0, chn = 0 , freq = 0;
    size_t totalBytes = 0;
    //
    remove(pcmFile);
    int fw = open(pcmFile, O_WRONLY|O_CREAT, 0666);
    if(fw < 1)
        return;
    //
    int fr = open(aacFile, O_RDONLY);
    if(fr < 1)
        return;
    //循环取数据
    out = (uint8_t*)malloc(8192);
    do{
        ret = aac_decode2(&aacDec, fr, out, &chn, &freq);
        if(ret > 0 && ret <= 8192)
        {
            totalBytes += ret;
            write(fw, out, ret);
        }
    }while(ret >= 0);
    free(out);
    //
    if(totalBytes < 0x100000)
        printf("aac_decodeToFile: final %.1fKb chn/%d freq/%d\n", 
            (float)totalBytes/1024, chn, freq);
    else
        printf("aac_decodeToFile: final %.1fMb chn/%d freq/%d\n", 
            (float)totalBytes/1024/1024, chn, freq);
    //
    if(aacDec)
        aac_decodeRelease(&aacDec);
    //
    close(fr);
    close(fw);
}

//销毁解码器句柄
void aac_decodeRelease(void **aacDec)
{
    if(!aacDec)
        return;
    NeAACDecClose(*aacDec);
    *aacDec = NULL;
}

//pcm编码为aac
//aacEnc: 解码器句柄,值为NULL时自动初始化
//in: 长度必须为2048*chn
//inLen: 2048*chn
//out: 长度大于等于4096
//outSize: 4096
int aac_encode(void **aacEnc, uint8_t* in, int inLen, uint8_t *out, uint32_t outSize, int chn, int freq)
{
    uint32_t nPCMBitSize = 16;
    uint32_t nInputSamples = 0;
    uint32_t nMaxOutputBytes = 0;
    faacEncHandle hEncoder;
    //
    if(!aacEnc)
        return -1;
    //第一次初始化编码器
    hEncoder = *((faacEncHandle*)aacEnc);
    if(hEncoder == NULL)
    {
        hEncoder = faacEncOpen(freq, chn, &nInputSamples, &nMaxOutputBytes);
        if(!hEncoder)
        {
            fprintf(stderr, "aac_encode: faacEncOpen err chn/%d, freq/%d\n", chn, freq);
            return -1;
        }
        //
        faacEncConfigurationPtr pConfiguration = faacEncGetCurrentConfiguration(hEncoder);
        pConfiguration->inputFormat = FAAC_INPUT_16BIT;
        faacEncSetConfiguration(hEncoder, pConfiguration);
        //
        *aacEnc = hEncoder;
    }
    //
    return faacEncEncode(hEncoder, (int32_t*)in, inLen/2, out, outSize);
}

//文件
void aac_encodeToFile2(int pcmFile, char *aacFile_fd, int chn, int freq)
{
    void *aacEnc = NULL;
    uint8_t *in, *out;
    int ret, rLen = 2048*chn;
    size_t totalBytes = 0;
    //
    int fw = aacFile_fd;
    if(fw < 1)
        return;
    //
    int fr = open(pcmFile, O_RDONLY);
    if(fr > 0)
    {
        in = (uint8_t*)malloc(4096);
        out = (uint8_t*)malloc(4096);
        do
        {
            ret = read(fr, in, rLen);
            if(ret < rLen)
                break;
            //
            ret = aac_encode(&aacEnc, in, rLen, out, 4096, chn, freq);
            if(ret > 0)
            {
                totalBytes += ret;
                write(fw, out, ret);
            }
        }while(ret >= 0);
        //
        free(in);
        free(out);
        //
        if(totalBytes < 0x100000)
            printf("aac_encodeToFile: final %.1fKb chn/%d freq/%d\n", 
                (float)totalBytes/1024, chn, freq);
        else
            printf("aac_encodeToFile: final %.1fMb chn/%d freq/%d\n", 
                (float)totalBytes/1024/1024, chn, freq);
        //
        if(aacEnc)
            aac_encodeRelease(&aacEnc);
        //
        close(fr);
    }
}

void aac_encodeToFile(char *pcmFile, char *aacFile, int chn, int freq)
{
    remove(aacFile);
    int fw = open(aacFile, O_WRONLY|O_CREAT, 0666);
    if(fw < 1)
        return;
    //
    aac_encodeToFile2(pcmFile, fw, chn, freq);
    close(fw);
}

//销毁编码器句柄
void aac_encodeRelease(void **aacEnc)
{
    if(!aacEnc)
        return;
    faacEncClose(*aacEnc);
    *aacEnc = NULL;
}

#endif
