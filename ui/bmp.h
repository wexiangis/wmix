
/*
 *  bmp文件读写
 */
#ifndef _BMP_H
#define _BMP_H

/*
 *  读取图片
 *  参数:
 *      filePath: 图片路径
 *      picMaxSize: 返回图片内存大小
 *      width, height: 返回宽高
 *      per: 返回每像素字节数
 *  返回: 内存指针 !! 用完记得free() !!
 */
unsigned char *bmp_get(char *filePath, int *picMaxSize, int *width, int *height, int *per);

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
int bmp_create(char *filePath, unsigned char *data, int width, int height, int per);

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
void bmp_create2(int order, char *folder, unsigned char *data, int width, int height, int per);

#endif
