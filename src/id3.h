#ifndef _ID3_H_
#define _ID3_H_

/***************************
4.20 AENC [[＃sec4.20 |音频加密]]
4.15 APIC [＃sec4.15附图]
4.11 COMM [＃sec4.11评论]
4.25 COMR [＃sec4.25商业框架]
4.26 ENCR [＃sec4.26加密方法注册]
4.13 EQUA [＃sec4.13均衡]
4.6 ETCO [＃sec4.6事件时序码]
4.16 GEOB [＃sec4.16一般封装对象]
4.27 GRID [＃sec4.27团体识别登记]
4.4 IPLS [＃sec4.4参与人员名单]
4.21 LINK[＃sec4.21链接信息]
4.5 MCDI [＃sec4.5音乐CD标识符]
4.7 MLLT [＃sec4.7 MPEG位置查找表]
4.24 OWNE [＃sec4.24所有权框架]
4.28 PRIV [＃sec4.28私人框架]
4.17 PCNT [＃sec4.17 Play counter]
4.18 POPM [＃sec4.18 Popularimeter]
4.22 POSS [＃sec4.22位置同步框架]
4.19 RBUF [＃sec4.19推荐的缓冲区大小]
4.12 RVAD [＃sec4.12相对音量调整]
4.14 RVRB [＃sec4.14 Reverb]
4.10 SYLT [＃sec4.10同步歌词/文字]
4.8 SYTC [＃sec4.8同步速度代码]
4.2.1 TALB [#TALB专辑/电影/节目标题]
4.2.1 TBPM [#TBPM BPM（每分钟节拍）]
4.2.1 TCOM [#TCOM Composer]
4.2.1 TCON [#TCON内容类型]
4.2.1 TCOP [#TCOP版权信息]
4.2.1 TDAT [#TDAT日期]
4.2.1 TDLY [#TDLY播放列表延迟]
4.2.1 TENC [#TENC编码]
4.2.1 TEXT [#TEXT作词/文本作者]
4.2.1 TFLT [#TFLT文件类型]
4.2.1 TIME [#TIME时间]
4.2.1 TIT1 [＃TIT1内容组说明]
4.2.1 TIT2 [＃TIT2标题/歌曲名称/内容说明]
4.2.1 TIT3 [＃TIT3字幕/描述细化]
4.2.1 TKEY [#TKEY初始密钥]
4.2.1 TLAN [#TLAN语言]
4.2.1 TLEN [#TLEN长度]
4.2.1 TMED [#TMED媒体类型]
4.2.1 TOAL [#TOAL原创专辑/电影/节目标题]
4.2.1 TOFN [#TOFN原始文件名]
4.2.1 TOLY [#TOLY原创作词/文字作者]
4.2.1 TOPE [#TOPE原创艺术家/表演者]
4.2.1 TORY [#TORY原始发行年份]
4.2.1 TOWN [#TOWN文件所有者/被许可人]
4.2.1 TPE1 [＃TPE1主要表演者/独奏者]
4.2.1 TPE2 [＃TPE2乐队/管弦乐队/伴奏]
4.2.1 TPE3 [#TPE3导体/执行器改进]
4.2.1 TPE4 [＃TPE4解释，重新混合或以其他方式修改]
4.2.1 TPOS [#TPOS一部分]
4.2.1 TPUB [#TPUB Publisher]
4.2.1 TRCK [#TRCK轨道编号/位置]
4.2.1 TRDA [#TRDA录制日期]
4.2.1 TRSN [#TRSN网络电台名称]
4.2.1 TRSO [#TRSO互联网广播电台所有者]
4.2.1 TSIZ [#TSIZ大小]
4.2.1 TSRC [#TSRC ISRC（国际标准录制代码）]
4.2.1 TSSE [#TSEE软件/用于编码的硬件和设置]
4.2.1 TYER [#TYER年份]
4.2.2 TXXX [#TXXX用户自定义文本信息框]
4.1 UFID [＃sec4.1唯一文件标识符]
4.23 USER [＃sec4.23使用条款]
4.9 USLT [＃sec4.9非同步歌词/文字转录]
4.3.1 WCOM [#WCOM商业信息]
4.3.1 WCOP [#WCOP版权/法律信息]
4.3.1 WOAF [#WOAF官方音频文件网页]
4.3.1 WOAR [#WOAR官方艺术家/表演者网页]
4.3.1 WOAS [#WOAS官方音频源网页]
4.3.1 WORS [#WORS官方网络电台主页]
4.3.1 WPAY [#WPAY付款]
4.3.1 WPUB [#WPUB出版社官方网页]
4.3.2 WXXX [#WXXX用户自定义URL链接框]
***************************/

//文件从起始识别到的id3标签总长度
//返回: 标签总长度
unsigned int id3_len(char *filePath);

//用回调函数的方式逐一返回解析到的标签信息
// privateData: 用户自己的私有指针,会作为回调函数的参数传回给用户
// type: 标签类型,详情比对上面的列表
// info: 标签对应的内容
// len info长度
//返回: 成功解析的标签数
unsigned int id3_info(char *filePath, 
    void *privateData, 
    void (*callback)(
        void *privateData, 
        char type[4], 
        char *info, 
        int len));

#endif
