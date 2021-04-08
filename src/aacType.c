/**************************************************
 * 
 *  基于libaac、libaad库的接口二次封装。
 * 
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aacType.h"

//虽有15格,实际只有前13中
int aac_freqList[15] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0};

int aac_createHeader(AacHeader *head, uint8_t chn, uint16_t freq, uint16_t codeRate, uint16_t datLen)
{
    datLen += 7;
    //byte 1
    head->syncwordH = 0xFF;
    //byte 2
    head->syncwordL = 0xF;
    head->id = 0;
    head->layer = 0;
    head->protectionAbsent = 1;
    //byte 3
    head->profile = 1;
    if (freq <= aac_freqList[12])
        head->samplingFreqIndex = 12;
    else if (freq <= aac_freqList[11])
        head->samplingFreqIndex = 11;
    else if (freq <= aac_freqList[10])
        head->samplingFreqIndex = 10;
    else if (freq <= aac_freqList[9])
        head->samplingFreqIndex = 9;
    else if (freq <= aac_freqList[8])
        head->samplingFreqIndex = 8;
    else if (freq <= aac_freqList[7])
        head->samplingFreqIndex = 7;
    else if (freq <= aac_freqList[6])
        head->samplingFreqIndex = 6;
    else if (freq <= aac_freqList[5])
        head->samplingFreqIndex = 5;
    else if (freq <= aac_freqList[4])
        head->samplingFreqIndex = 4;
    else if (freq <= aac_freqList[3])
        head->samplingFreqIndex = 3;
    else if (freq <= aac_freqList[2])
        head->samplingFreqIndex = 2;
    else if (freq <= aac_freqList[1])
        head->samplingFreqIndex = 1;
    else
        head->samplingFreqIndex = 0;
    head->privateBit = 0;
    head->chnH = (chn >> 2) & 0x1;
    //byte 4
    head->chnL = chn & 0x3;
    head->originalCopy = 0;
    head->home = 0;
    head->copyrightIdentificationBit = 0;
    head->copyrightIdentificationStart = 0;
    head->aacFrameLengthH = (datLen >> 11) & 0x3;
    //byte 5
    head->aacFrameLengthM = (datLen >> 3) & 0xFF;
    //byte 6
    head->aacFrameLengthL = datLen & 0x7;
    head->adtsBufferFullnessH = (codeRate >> 6) & 0x1F;
    //byte 7
    head->adtsBufferFullnessL = codeRate & 0x3F;
    head->numberOfRawDataBlockInFrame = 0;
    return datLen;
}

int aac_parseHeader(AacHeader *head, uint8_t *chn, uint16_t *freq, uint16_t *frameLen, uint8_t show)
{
    if (head->syncwordH != 0xFF || head->syncwordL != 0xF)
        return -1;
    if (chn)
        *chn = (head->chnH << 3) | head->chnL;
    if (freq)
        *freq = aac_freqList[head->samplingFreqIndex];
    if (frameLen)
        *frameLen = (head->aacFrameLengthH << 11) | (head->aacFrameLengthM << 3) | head->aacFrameLengthL;
    if (show)
    {
        printf("adts:id  %d\n", head->id);
        printf("adts:layer  %d\n", head->layer);
        printf("adts:protection_absent  %d\n", head->protectionAbsent);
        printf("adts:profile  %d\n", head->profile);
        printf("adts:sf_index  %dHz\n", aac_freqList[head->samplingFreqIndex]);
        printf("adts:pritvate_bit  %d\n", head->privateBit);
        printf("adts:channel_configuration  %d\n", (head->chnH << 3) | head->chnL);
        printf("adts:original  %d\n", head->originalCopy);
        printf("adts:home  %d\n", head->home);
        printf("adts:copyright_identification_bit  %d\n", head->copyrightIdentificationBit);
        printf("adts:copyright_identification_start  %d\n", head->copyrightIdentificationStart);
        printf("adts:aac_frame_length  %d\n", (head->aacFrameLengthH << 11) | (head->aacFrameLengthM << 3) | head->aacFrameLengthL);
        printf("adts:adts_buffer_fullness  %d\n", (head->adtsBufferFullnessH << 6) | head->adtsBufferFullnessL);
        printf("adts:no_raw_data_blocks_in_frame  %d\n", head->numberOfRawDataBlockInFrame);
        printf("\n");
    }
    return 0;
}

//------------------ faac, faad ------------------

#if (MAKE_AAC)

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "faad.h"
#include "faac.h"

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
int aac_decode(void **aacDec, uint8_t *in, int inLen, uint8_t *out, int *bytesConsumed, uint8_t *chn, uint16_t *freq)
{
    int count = 0;
    uint16_t frameLen = 0;
    NeAACDecHandle hDecoder;
    NeAACDecFrameInfo hInfo;
    uint8_t *ret;
    unsigned long samplerate;
    unsigned char channels;

    if (!aacDec)
        return -1;
    //找到aac头
    for (; count < inLen - 7; count++)
    {
        if (in[0] == 0xFF && (in[1] & 0xF0) == 0xF0)
        {
            if (aac_parseHeader((AacHeader *)in, NULL, NULL, &frameLen, 0) == 0)
            {
                if (count > 0)
                    printf("break at %d\n", count);
                break;
            }
        }
        in++;
    }
    //检查是否足够一包数据
    if (frameLen == 0)
        return 0;
    else if (inLen - count < frameLen)
    {
        *bytesConsumed = frameLen - (inLen - count);
        return 0;
    }
    //第一次初始化解码器句柄
    hDecoder = *((NeAACDecHandle *)aacDec);
    if (!hDecoder)
    {
        hDecoder = NeAACDecOpen();
        //初始化解码器
        NeAACDecInit(hDecoder, in, frameLen, &samplerate, &channels);
        *aacDec = hDecoder;
    }
    //解码
    ret = (uint8_t *)NeAACDecDecode(hDecoder, &hInfo, in, frameLen);
    if (!ret || hInfo.error > 0)
    {
        printf("aac_decode: err %d [%s]\n",
               hInfo.error, NeAACDecGetErrorMessage(hInfo.error));
        return -1;
    }
    //拷贝数据
    memcpy(out, ret, hInfo.samples * hInfo.channels);

    //参数返回
    if (bytesConsumed)
        *bytesConsumed = hInfo.bytesconsumed + count;
    if (chn)
        *chn = hInfo.channels;
    if (freq)
        *freq = hInfo.samplerate;

    return hInfo.samples * hInfo.channels;
}

/*
 *  aac解码为pcm
 *  参数:
 *      aacDec: 解码器句柄,值为NULL时自动初始化
 *      aacFile_fd: 已打开的aac文件句柄
 *      out: 返回数据缓冲区,要求大于等于8192
 *  返回: pcm数据长度, -1失败
 */
int aac_decode2(void **aacDec, int aacFile_fd, uint8_t *out, uint8_t *chn, uint16_t *freq)
{
    uint16_t frameLen = 0;
    NeAACDecHandle hDecoder;
    NeAACDecFrameInfo hInfo;
    uint8_t in[2048];
    uint8_t *retp;
    size_t ret;
    unsigned long samplerate;
    unsigned char channels;

    if (!aacDec)
        return -1;
    //找到aac头
    do
    {
        if (read(aacFile_fd, in, 2) != 2)
            return -1;
        if (in[0] == 0xFF && (in[1] & 0xF0) == 0xF0)
        {
            if (read(aacFile_fd, &in[2], 5) != 5)
                return -1;
            if (aac_parseHeader((AacHeader *)in, NULL, NULL, &frameLen, 0) == 0)
                break;
        }
    } while (1);
    //读数据段
    if (read(aacFile_fd, &in[7], frameLen - 7) != frameLen - 7)
        return -1;
    //第一次初始化解码器句柄
    hDecoder = *((NeAACDecHandle *)aacDec);
    if (!hDecoder)
    {
        hDecoder = NeAACDecOpen();
        //初始化解码器
        NeAACDecInit(hDecoder, in, frameLen, &samplerate, &channels);
        *aacDec = hDecoder;
    }
    //解码
    retp = (uint8_t *)NeAACDecDecode(hDecoder, &hInfo, in, frameLen);
    if (!retp || hInfo.error > 0)
    {
        printf("aac_decode: err %d [%s]\n",
               hInfo.error, NeAACDecGetErrorMessage(hInfo.error));
        return -1;
    }
    //拷贝数据
    ret = hInfo.samples * hInfo.channels;
    if (ret > 0)
        memcpy(out, retp, ret);

    //参数返回
    if (chn)
        *chn = hInfo.channels;
    if (freq)
        *freq = hInfo.samplerate;
    return ret;
}

void aac_decodeToFile(char *aacFile, char *pcmFile)
{
    void *aacDec = NULL;
    uint8_t *out = NULL;
    int ret = 0;
    uint8_t chn = 0;
    uint16_t freq = 0;
    size_t totalBytes = 0;
    int fw, fr;

    remove(pcmFile);
    fw = open(pcmFile, O_WRONLY | O_CREAT, 0666);
    if (fw < 1)
        return;
    fr = open(aacFile, O_RDONLY);
    if (fr < 1)
        return;

    //循环取数据
    out = (uint8_t *)malloc(8192);
    do
    {
        ret = aac_decode2(&aacDec, fr, out, &chn, &freq);
        if (ret > 0 && ret <= 8192)
        {
            totalBytes += ret;
            write(fw, out, ret);
        }
    } while (ret >= 0);
    free(out);

    if (totalBytes < 0x100000)
        printf("aac_decodeToFile: final %.1fKb chn/%d freq/%d\n",
               (float)totalBytes / 1024, chn, freq);
    else
        printf("aac_decodeToFile: final %.1fMb chn/%d freq/%d\n",
               (float)totalBytes / 1024 / 1024, chn, freq);

    if (aacDec)
        aac_decodeRelease(&aacDec);

    close(fr);
    close(fw);
}

//销毁解码器句柄
void aac_decodeRelease(void **aacDec)
{
    if (!aacDec)
        return;
    NeAACDecClose(*aacDec);
    *aacDec = NULL;
}

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
int aac_encode(void **aacEnc, uint8_t *in, int inLen, uint8_t *out, uint32_t outSize, int chn, int freq)
{
    // uint32_t nPCMBitSize = 16;
    uint32_t nInputSamples = 0;
    uint32_t nMaxOutputBytes = 0;
    faacEncHandle hEncoder;
    faacEncConfigurationPtr pConfiguration;

    if (!aacEnc)
        return -1;

    //第一次初始化编码器
    hEncoder = *((faacEncHandle *)aacEnc);
    if (hEncoder == NULL)
    {
        hEncoder = faacEncOpen(
            (unsigned long)freq,
            (unsigned int)chn,
            (unsigned long *)&nInputSamples,
            (unsigned long *)&nMaxOutputBytes);
        if (!hEncoder)
        {
            fprintf(stderr, "aac_encode: faacEncOpen err chn/%d, freq/%d\n", chn, freq);
            return -1;
        }

        pConfiguration = faacEncGetCurrentConfiguration(hEncoder);
        // printf("aac_encode: \n"
        //     "  nInputSamples %d\n"
        //     "  nMaxOutputBytes %d\n"
        //     "  outputFormat %d\n"
        //     "  inputFormat %d\n"
        //     "  bitrate %d\n"
        //     "  bandWidth %d\r\n",
        //     nInputSamples,
        //     nMaxOutputBytes,
        //     pConfiguration->outputFormat,
        //     pConfiguration->inputFormat,
        //     pConfiguration->bitRate,
        //     pConfiguration->bandWidth);
        pConfiguration->inputFormat = FAAC_INPUT_16BIT;
        faacEncSetConfiguration(hEncoder, pConfiguration);
        *aacEnc = hEncoder;
    }
    return faacEncEncode(hEncoder, (int32_t *)in, inLen / 2, out, outSize);
}

//文件
void aac_encodeToFile2(int pcmFile_fd, int aacFile_fd, int chn, int freq)
{
    void *aacEnc = NULL;
    uint8_t *in, *out;
    int ret, rLen = 2048 * chn;
    size_t totalBytes = 0;
    int fw, fr;

    fw = aacFile_fd;
    if (fw < 1)
        return;

    fr = pcmFile_fd;
    if (fr < 1)
        return;

    in = (uint8_t *)malloc(4096);
    out = (uint8_t *)malloc(4096);
    do
    {
        ret = read(fr, in, rLen);
        if (ret < rLen)
            break;

        ret = aac_encode(&aacEnc, in, rLen, out, 4096, chn, freq);
        if (ret > 0)
        {
            totalBytes += ret;
            write(fw, out, ret);
        }
    } while (ret >= 0);

    free(in);
    free(out);

    if (totalBytes < 0x100000)
        printf("aac_encodeToFile: final %.1fKb chn/%d freq/%d\n",
               (float)totalBytes / 1024, chn, freq);
    else
        printf("aac_encodeToFile: final %.1fMb chn/%d freq/%d\n",
               (float)totalBytes / 1024 / 1024, chn, freq);

    if (aacEnc)
        aac_encodeRelease(&aacEnc);
}

void aac_encodeToFile(char *pcmFile, char *aacFile, int chn, int freq)
{
    int fw, fr;

    remove(aacFile);
    fw = open(aacFile, O_WRONLY | O_CREAT, 0666);
    if (fw < 1)
        return;

    fr = open(pcmFile, O_RDONLY);
    if (fr < 1)
    {
        close(fw);
        return;
    }

    aac_encodeToFile2(fr, fw, chn, freq);

    close(fw);
    close(fr);
}

//销毁编码器句柄
void aac_encodeRelease(void **aacEnc)
{
    if (!aacEnc)
        return;
    faacEncClose(*aacEnc);
    *aacEnc = NULL;
}

#endif // #if (MAKE_AAC)
