/**************************************************
 * 
 *  rtp协议的数据打包、解包、udp发、收接口的封装
 * 
 **************************************************/

#ifndef _RTP_H_
#define _RTP_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <pthread.h> //mtex
#include <sys/time.h>

#define RTP_VESION 2

typedef enum
{
    RTP_PAYLOAD_TYPE_PCMU = 0,
    RTP_PAYLOAD_TYPE_GSM = 3,
    RTP_PAYLOAD_TYPE_G723 = 4,
    RTP_PAYLOAD_TYPE_PCMA = 8,
    RTP_PAYLOAD_TYPE_G722 = 9,
    RTP_PAYLOAD_TYPE_G728 = 15,
    RTP_PAYLOAD_TYPE_G729 = 18,
    RTP_PAYLOAD_TYPE_H264 = 96,
    RTP_PAYLOAD_TYPE_AAC = 97,
} RTP_AUDIO_TYPE;

#define RTP_PCMA_PKT_SIZE 160

#define RTP_HEADER_SIZE 12
/*
 *
 *    0                   1                   2                   3
 *    7 6 5 4 3 2 1 0|7 6 5 4 3 2 1 0|7 6 5 4 3 2 1 0|7 6 5 4 3 2 1 0
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                           timestamp                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |           synchronization source (SSRC) identifier            |
 *   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 *   |            contributing source (CSRC) identifiers             |
 *   :                             ....                              :
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
typedef struct
{
    /* byte 0 */
    uint8_t cc : 4; //crc标识符个数
    uint8_t x : 1;  //扩展标志,为1时表示rtp报头后有1个扩展包
    uint8_t p : 1;  //填充标志,为1时标识数据尾部有无效填充
    uint8_t v : 2;  //版本号

    /* byte 1 */
    uint8_t pt : 7; //有小载荷类型
    uint8_t m : 1;  //载荷标记,视频为1帧结束,音频为会话开始

    /* bytes 2,3 */
    uint16_t seq; //序列号,每帧+1,随机开始,音/视频分开

    /* bytes 4-7 */
    uint32_t timestamp; //时间戳,us,自增

    /* bytes 8-11 */
    uint32_t ssrc; //同步信号源

} RtpHeader;

typedef struct
{
    RtpHeader rtpHeader;
    uint8_t payload[4096];
} RtpPacket;

typedef struct
{
    int fd;
    struct sockaddr_in addr;
    size_t addrSize;
    pthread_mutex_t lock;
} SocketStruct;

void rtp_header(RtpPacket *rtpPacket, uint8_t cc, uint8_t x,
                uint8_t p, uint8_t v, uint8_t pt, uint8_t m,
                uint16_t seq, uint32_t timestamp, uint32_t ssrc);

int rtp_send(SocketStruct *ss, RtpPacket *rtpPacket, uint32_t dataSize);

int rtp_recv(SocketStruct *ss, RtpPacket *rtpPacket, uint32_t *dataSize);

SocketStruct *rtp_socket(char *ip, uint16_t port, bool isServer);

void rtp_create_sdp(char *file, char *ip, uint16_t port, uint16_t chn, uint16_t freq, RTP_AUDIO_TYPE type);

//系统当前毫秒数获取
__time_t getTickUs(void);


/* ----- 辅助 wmix 添加的链表管理结构 ----- */
/* ----- 检索到新增同ip和端口连接时使用现存,关闭时由最后使用者回收内存 ----- */

//链表节点,又来记录当前这套ip和端口的使用情况
typedef struct RtpChainStruct{
    char ip[16];
    int port;
    SocketStruct *ss;//连接句柄
    bool send_run, recv_run;//正在使用的发收线程(一套ip和端口只允许一个s和一个r在使用)
    struct RtpChainStruct *last, *next;//链表
}RtpChain_Struct;

//申请节点(已自动连上socket),NULL为失败
RtpChain_Struct *rtpChain_get(char *ip, int port, bool isSend);
//释放节点(不要调用free(rcs)!! 链表的内存由系统决定何时回收)
void rtpChain_release(RtpChain_Struct *rcs, bool isSend);

#endif //_RTP_H_
