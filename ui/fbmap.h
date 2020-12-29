/*
 *  fb矩阵输出
 */
#ifndef _FBMAP_H_
#define _FBMAP_H_

//屏幕宽高
extern int fb_width, fb_height;

/*
 *  屏幕输出
 *  data: 图像数组,数据长度必须为 width*height*3, RGB格式
 *  offsetX, offsetY: 屏幕起始位置
 *  width, height: 图像宽高
 */
void fb_output(unsigned char *data, int offsetX, int offsetY, int width, int height);

/*
 *  截取屏幕保存为bmp文件
 */
void fb_screensShot(char *bmpPath);
void fb_screensShot2(int order, char *folder);

void fb_release(void);

#endif