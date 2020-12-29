
/*
 *  bmp文件读写
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define Bmp_FileHeader_Size 14 // sizeof(Bmp_FileHeader)的值不一定准确
typedef struct
{
    uint8_t bfType[2];    //文件类型: "BM"/bmp, "BA"/.. , ...
    uint32_t bfSize;      //整个文件的大小
    uint16_t bfReserved1; //保留: 0
    uint16_t bfReserved2; //保留: 0
    uint32_t bfOffbits;   //文件数据从第几个字节开始
} Bmp_FileHeader;
//
#define Bmp_Info_Size 40
typedef struct
{
    uint32_t biSize;         //该段占用字节数
    uint32_t biWidth;        //图像宽度, 单位像素
    int32_t biHeight;        //图像高度, 单位像素(数据为正时为倒向)
    uint16_t biPlanes;       //平面数, 总是为1
    uint16_t biBitCount;     //单位像素占用比特数: 1, 4, 8, 16, 24, 42
    uint32_t biCompression;  //图像压缩方式: 0/BI_BGB 不压缩,
                             //                         1/BI_RLE8 8比特游程压缩, 只用于8位位图
                             //                         2/BI_RLE4 4比特游程压缩, 只用于4位位图
                             //                         3/BI_BITFIELDS 比特域, 用于16/32位位图
                             //                         4/BI_JPEG 位图含有jpeg图像, 用于打印机
                             //                         5/BI_PWG 位图含有pwg图像, 用于打印机
    uint32_t biSizeImage;    //说明图像大小, 为BI_BGB时可能为0
    int32_t biXPelsPerMeter; //水平分辨率, 像素/米, 有符号整数
    int32_t biYPelsPerMeter; //垂直分辨率, 像素/米, 有符号整数
    uint32_t biClrUsed;      //位图实际使用彩色表中的颜色索引数(为0表示使用所有)
    uint32_t biClrImportant; //图像显示有重要影响的颜色索引数
} Bmp_Info;

//功能: 读取bmp格式图片
//参数: filePath: 传入, 文件地址
//         picMaxSize: 传出, 用以返回读取到的图片矩阵的总字节数
//         width: 传出, 用以返回图片横向的像素个数
//         height: 传出, 用以返回图片纵向的像素个数
//         per: 传出, 用以返回图片每像素占用字节数
//返回: 图片矩阵数据的指针(注意指针是函数内分配的内存, 用完需释放)
unsigned char *bmp_get(char *filePath, int *picMaxSize, int *width, int *height, int *per)
{
    int fd;
    Bmp_FileHeader bf;
    Bmp_Info bi;
    int perW, perWCount;
    int ret;
    int i, j, picCount, totalSize;
    int overLineBytesNum;
    unsigned int overLineBytesSum; // overLineBytesNum : 行结尾补0字节数  //每行字节数规定为4的倍数, 不足将补0, 所以读行的时候注意跳过
    unsigned char buffHeader[512], *data, *pic;
    //
    if (filePath == NULL)
        return NULL;
    //
    if ((fd = open(filePath, O_RDONLY)) < 0)
    {
        printf("bmp_get : open file %s failed\r\n", filePath);
        return NULL;
    }
    //Bmp_FileHeader
    if (read(fd, buffHeader, Bmp_FileHeader_Size) <= 0)
    {
        printf("bmp_get : read Bmp_FileHeader failed\r\n");
        close(fd);
        return NULL;
    }
    bf.bfType[0] = buffHeader[0];
    bf.bfType[1] = buffHeader[1];
    bf.bfSize = buffHeader[2] + ((buffHeader[3] & 0xFF) << 8) + ((buffHeader[4] & 0xFF) << 16) + ((buffHeader[5] & 0xFF) << 24);
    bf.bfOffbits = buffHeader[10] + ((buffHeader[11] & 0xFF) << 8) + ((buffHeader[12] & 0xFF) << 16) + ((buffHeader[13] & 0xFF) << 24);
    //printf("bmp_get : bfType/%s, bfSize/%d, bfOffbits/%d\r\n", bf.bfType, bf.bfSize, bf.bfOffbits);
    if (bf.bfType[0] != 'B' || bf.bfType[1] != 'M')
    {
        printf("bmp_get : bmp type err, bfType must be \"BM\"\r\n");
        close(fd);
        return NULL;
    }
    //Bmp_Info
    if (bf.bfOffbits - Bmp_FileHeader_Size < Bmp_Info_Size || read(fd, buffHeader, Bmp_Info_Size) <= 0)
    {
        printf("bmp_get : read Bmp_Info failed\r\n");
        close(fd);
        return NULL;
    }
    bi.biSize = buffHeader[0] + ((buffHeader[1] & 0xFF) << 8) + ((buffHeader[2] & 0xFF) << 16) + ((buffHeader[3] & 0xFF) << 24);
    bi.biWidth = buffHeader[4] + ((buffHeader[5] & 0xFF) << 8) + ((buffHeader[6] & 0xFF) << 16) + ((buffHeader[7] & 0xFF) << 24);
    bi.biHeight = buffHeader[8] | ((buffHeader[9] & 0xFF) << 8) | ((buffHeader[10] & 0xFF) << 16) | ((buffHeader[11] & 0xFF) << 24);
    bi.biPlanes = buffHeader[12] + ((buffHeader[13] & 0xFF) << 8);
    bi.biBitCount = buffHeader[14] + ((buffHeader[15] & 0xFF) << 8);
    bi.biCompression = buffHeader[16] + ((buffHeader[17] & 0xFF) << 8) + ((buffHeader[18] & 0xFF) << 16) + ((buffHeader[19] & 0xFF) << 24);
    bi.biSizeImage = buffHeader[20] + ((buffHeader[21] & 0xFF) << 8) + ((buffHeader[22] & 0xFF) << 16) + ((buffHeader[23] & 0xFF) << 24);
    bi.biXPelsPerMeter = buffHeader[24] | ((buffHeader[25] & 0xFF) << 8) | ((buffHeader[26] & 0xFF) << 16) | ((buffHeader[27] & 0xFF) << 24);
    bi.biYPelsPerMeter = buffHeader[28] | ((buffHeader[29] & 0xFF) << 8) | ((buffHeader[30] & 0xFF) << 16) | ((buffHeader[31] & 0xFF) << 24);
    bi.biClrUsed = buffHeader[32] + ((buffHeader[33] & 0xFF) << 8) + ((buffHeader[34] & 0xFF) << 16) + ((buffHeader[35] & 0xFF) << 24);
    bi.biClrImportant = buffHeader[36] + ((buffHeader[37] & 0xFF) << 8) + ((buffHeader[38] & 0xFF) << 16) + ((buffHeader[39] & 0xFF) << 24);
    //perW 每像素字节数
    if (bi.biBitCount >= 8)
        perW = bi.biBitCount / 8;
    else
        perW = 1;
    //计算总字节数
    //totalSize = bf.bfSize - bf.bfOffbits;
    //计算总字节数
    overLineBytesNum = 4 - bi.biWidth * (bi.biBitCount / 8) % 4;
    if (overLineBytesNum == 4)
        overLineBytesNum = 0;
    if (bi.biHeight < 0)
    {
        totalSize = bi.biWidth * (-bi.biHeight) * (bi.biBitCount / 8);
        overLineBytesSum = overLineBytesNum * (-bi.biHeight);
    }
    else
    {
        totalSize = bi.biWidth * bi.biHeight * (bi.biBitCount / 8);
        overLineBytesSum = overLineBytesNum * bi.biHeight;
    }
    //printf("bmp_get : biSize/%d, biWidth/%d, biHeight/%d, biPlanes/%d, biBitCount/%d, biCompression/%d, biSizeImage/%d, biXPelsPerMeter/%d, biYPelsPerMeter/%d, biClrUsed/%d, biClrImportant/%d, overLineBytesNum/%d, overLineBytesSum/%d, totalSize/%d\r\n", bi.biSize, bi.biWidth, bi.biHeight, bi.biPlanes, bi.biBitCount, bi.biCompression, bi.biSizeImage, bi.biXPelsPerMeter, bi.biYPelsPerMeter, bi.biClrUsed, bi.biClrImportant, overLineBytesNum, overLineBytesSum, totalSize);
    //指针移动到数据起始
    if (lseek(fd, bf.bfOffbits, 0) < 0)
    {
        printf("bmp_get : lseek failed\r\n");
        close(fd);
        return NULL;
    }
    //分配内存一次读入整张图片
    data = (unsigned char *)calloc(1, totalSize + overLineBytesSum + perW); //多1像素的字节, 防止操作不当溢出
    if ((ret = read(fd, data, totalSize + overLineBytesSum)) != (totalSize + overLineBytesSum))
    {
        if (ret <= 0)
        {
            printf("bmp_get : read data failed\r\n");
            free(data);
            close(fd);
            return NULL;
        }
    }
    //close
    close(fd);
    //
    pic = (unsigned char *)calloc(1, totalSize);
    memset(pic, 0, totalSize);
    //根据图片方向拷贝数据
    if (bi.biHeight > 0) //倒向        //上下翻转 + 左右翻转 + 像素字节顺序调整
    {
        for (i = 0, picCount = totalSize; i < totalSize + overLineBytesSum && picCount >= 0;)
        {
            picCount -= bi.biWidth * perW;
            for (j = 0, perWCount = perW - 1; j < bi.biWidth * perW && i < totalSize + overLineBytesSum && picCount >= 0; j++)
            {
                pic[picCount + perWCount] = data[i++];
                if (--perWCount < 0)
                    perWCount = perW - 1;
                if (perWCount == perW - 1)
                    picCount += perW;
            }
            picCount -= bi.biWidth * perW;
            i += overLineBytesNum;
        }
    }
    else // 正向        //像素字节顺序调整
    {
        for (i = 0, j = 0, picCount = 0, perWCount = perW - 1; i < totalSize + overLineBytesSum && picCount < totalSize;)
        {
            pic[picCount + perWCount] = data[i++];
            if (--perWCount < 0)
                perWCount = perW - 1;
            if (perWCount == perW - 1)
                picCount += perW;
            if (++j == bi.biWidth * perW)
            {
                j = 0;
                i += overLineBytesNum;
            }
        }
    }
    //free
    free(data);
    //返回 宽, 高, 像素字节
    if (picMaxSize)
        *picMaxSize = totalSize;
    if (width)
        *width = bi.biWidth;
    if (height)
    {
        if (bi.biHeight > 0)
            *height = bi.biHeight;
        else
            *height = -bi.biHeight;
    }
    if (per)
        *per = perW;
    return pic;
}

/*
 *  创建图片,返回文件大小
 *  参数:
 *      filePath: 传入, 文件地址
 *      data: 传入, 图片矩阵数据的指针,rgb格式
 *      width: 传入, 图片横向的像素个数
 *      height: 传入, 图片纵向的像素个数
 *      per: 传入, 图片每像素占用字节数
 *  返回: 创建的bmp图片文件的大小, -1表示创建失败
 */
int bmp_create(char *filePath, unsigned char *data, int width, int height, int per)
{
    int fp = 0;
    int fileSize, fileSize2, count, headSize;
    unsigned char *bmpData, *p;
    int perWCount;
    int i, j, picCount;
    int overLineBytesNum;
    unsigned int overLineBytesSum = 0; // overLineBytesNum : 行结尾补0字节数  //每行字节数规定为4的倍数, 不足将补0, 所以读行的时候注意跳过
    //
    if (width < 0)
    {
        printf("bmp_create : width < 0 , err !!\r\n");
        return -1;
    }
    //
    if ((fp = open(filePath, O_WRONLY | O_CREAT, S_IRWXU)) < 0)
    {
        printf("bmp_create : create %s err\r\n", filePath);
        return -1;
    }
    //
    overLineBytesNum = width * per % 4;
    if (overLineBytesNum == 4)
        overLineBytesNum = 0;
    headSize = Bmp_FileHeader_Size + Bmp_Info_Size;
    if (height < 0)
    {
        overLineBytesNum = overLineBytesNum * (-height);
        fileSize2 = width * (-height) * per;
    }
    else
    {
        overLineBytesNum = overLineBytesNum * height;
        fileSize2 = width * height * per;
    }
    fileSize = headSize + fileSize2;
    bmpData = (unsigned char *)calloc(1, fileSize + overLineBytesNum);
    count = 0;
    //
    bmpData[count++] = 'B'; //bfType
    bmpData[count++] = 'M';
    bmpData[count++] = (unsigned char)((fileSize >> 0) & 0xFF); //bfSize     低位在前
    bmpData[count++] = (unsigned char)((fileSize >> 8) & 0xFF);
    bmpData[count++] = (unsigned char)((fileSize >> 16) & 0xFF);
    bmpData[count++] = (unsigned char)((fileSize >> 24) & 0xFF);
    count++; //保留
    count++;
    count++;
    count++;
    bmpData[count++] = (unsigned char)((headSize >> 0) & 0xFF); //bfOffbits     低位在前
    bmpData[count++] = (unsigned char)((headSize >> 8) & 0xFF);
    bmpData[count++] = (unsigned char)((headSize >> 16) & 0xFF);
    bmpData[count++] = (unsigned char)((headSize >> 24) & 0xFF);

    bmpData[count++] = (unsigned char)((Bmp_Info_Size >> 0) & 0xFF); //biSize
    bmpData[count++] = (unsigned char)((Bmp_Info_Size >> 8) & 0xFF);
    bmpData[count++] = (unsigned char)((Bmp_Info_Size >> 16) & 0xFF);
    bmpData[count++] = (unsigned char)((Bmp_Info_Size >> 24) & 0xFF);
    bmpData[count++] = (unsigned char)((width >> 0) & 0xFF); //biWidth
    bmpData[count++] = (unsigned char)((width >> 8) & 0xFF);
    bmpData[count++] = (unsigned char)((width >> 16) & 0xFF);
    bmpData[count++] = (unsigned char)((width >> 24) & 0xFF);
    bmpData[count++] = (unsigned char)((height >> 0) & 0xFF); //biHeight
    bmpData[count++] = (unsigned char)((height >> 8) & 0xFF);
    bmpData[count++] = (unsigned char)((height >> 16) & 0xFF);
    bmpData[count++] = (unsigned char)((height >> 24) & 0xFF);
    bmpData[count++] = 0x01; //biPlanes
    bmpData[count++] = 0x00;
    bmpData[count++] = 24; //biBitCount
    bmpData[count++] = 0;
    bmpData[count++] = 0; //biCompression
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = (unsigned char)((fileSize2 >> 0) & 0xFF); //biSizeImage
    bmpData[count++] = (unsigned char)((fileSize2 >> 8) & 0xFF);
    bmpData[count++] = (unsigned char)((fileSize2 >> 16) & 0xFF);
    bmpData[count++] = (unsigned char)((fileSize2 >> 24) & 0xFF);
    bmpData[count++] = 0; //biXPelsPerMeter
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0; //biYPelsPerMeter
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0; //biClrUsed
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0; //biClrImportant
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    bmpData[count++] = 0;
    //
    p = &bmpData[count];
    if (height >= 0) //倒向        //上下翻转 + 左右翻转 + 像素字节顺序调整
    {
        for (i = 0, picCount = fileSize2; i < fileSize2 + overLineBytesSum && picCount >= 0;)
        {
            picCount -= width * per;
            for (j = 0, perWCount = per - 1; j < width * per && i < fileSize2 + overLineBytesSum && picCount >= 0; j++)
            {
                p[i++] = data[picCount + perWCount];
                if (--perWCount < 0)
                    perWCount = per - 1;
                if (perWCount == per - 1)
                    picCount += per;
            }
            picCount -= width * per;
            i += overLineBytesNum;
        }
    }
    else // 正向        //像素字节顺序调整
    {
        for (i = 0, j = 0, picCount = 0, perWCount = per - 1; i < fileSize2 + overLineBytesSum && picCount < fileSize2;)
        {
            p[i++] = data[picCount + perWCount];
            if (--perWCount < 0)
                perWCount = per - 1;
            if (perWCount == per - 1)
                picCount += per;
            if (++j == width * per)
            {
                j = 0;
                i += overLineBytesNum;
            }
        }
    }
    //
    fileSize = write(fp, bmpData, fileSize);
    free(bmpData);
    //sync();
    return fileSize;
}

/*
 *  连续输出帧图片
 *  参数:
 *      order: 帧序号,用来生成图片名称效果如: 0001.bmp
 *      folder: 帧图片保存路径,格式如: /tmp
 *      data: 传入, 图片矩阵数据的指针,rgb格式
 *      width: 传入, 图片横向的像素个数
 *      height: 传入, 图片纵向的像素个数
 *      per: 传入, 图片每像素占用字节数
 */
void bmp_create2(int order, char *folder, unsigned char *data, int width, int height, int per)
{
    char file[1024] = {0};
    //参数检查
    if (!folder || !data || strlen(folder) < 1 || width < 1 || height < 1 || per < 3)
        return;
    //路径名称要不要补'/'
    if (folder[strlen(folder) - 1] == '/')
        snprintf(file, sizeof(file), "%s%04d.bmp", folder, order);
    else
        snprintf(file, sizeof(file), "%s/%04d.bmp", folder, order);
    //生成文件
    bmp_create(file, data, width, height, per);
}
