#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "id3.h"

typedef union{
    uint32_t value;
    char str[4];
}ID3_TAG;

const ID3_TAG tag[] = {
    {.str = "AENC"},
    {.str = "APIC"},
    {.str = "COMM"},
    {.str = "COMR"},
    {.str = "ENCR"},
    {.str = "EQUA"},
    {.str = "ETCO"},
    {.str = "GEOB"},
    {.str = "GRID"},
    {.str = "IPLS"},
    {.str = "LINK"},
    {.str = "MCDI"},
    {.str = "MLLT"},
    {.str = "OWNE"},
    {.str = "PRIV"},
    {.str = "PCNT"},
    {.str = "POPM"},
    {.str = "POSS"},
    {.str = "RBUF"},
    {.str = "RVAD"},
    {.str = "RVRB"},
    {.str = "SYLT"},
    {.str = "SYTC"},
    {.str = "TALB"},
    {.str = "TBPM"},
    {.str = "TCOM"},
    {.str = "TCON"},
    {.str = "TCOP"},
    {.str = "TDAT"},
    {.str = "TDLY"},
    {.str = "TENC"},
    {.str = "TEXT"},
    {.str = "TFLT"},
    {.str = "TIME"},
    {.str = "TIT1"},
    {.str = "TIT2"},
    {.str = "TIT3"},
    {.str = "TEKY"},
    {.str = "TLAN"},
    {.str = "TLEN"},
    {.str = "TMED"},
    {.str = "TOAL"},
    {.str = "TOFN"},
    {.str = "TOLY"},
    {.str = "TOPE"},
    {.str = "TORY"},
    {.str = "TOWN"},
    {.str = "TPE1"},
    {.str = "TPE2"},
    {.str = "TPE3"},
    {.str = "TPE4"},
    {.str = "TPOS"},
    {.str = "TPUB"},
    {.str = "TRCK"},
    {.str = "TRDA"},
    {.str = "TRSN"},
    {.str = "TRSO"},
    {.str = "TSIZ"},
    {.str = "TSRC"},
    {.str = "TSSE"},
    {.str = "TYER"},
    {.str = "TXXX"},
    {.str = "UFID"},
    {.str = "USER"},
    {.str = "USLT"},
    {.str = "WCOM"},
    {.str = "WCOP"},
    {.str = "WOAF"},
    {.str = "WOAR"},
    {.str = "WOAS"},
    {.str = "WORS"},
    {.str = "WPAY"},
    {.str = "WPUB"},
    {.str = "WXXX"}
};

unsigned int id3_len(char *filePath)
{
    int fd = 0;
    int ret = 0;
    unsigned char buff[128];
    //
    fd = open(filePath, O_RDONLY);
    if(fd <= 0){
        fprintf(stderr, "id3_len: open %s err\n", filePath);
        return 0;
    }
    //
    ret = read(fd, buff, 128);
    //ID3V2.1~4
    if(ret > 9 &&
        buff[0] == 'I' &&
        buff[1] == 'D' &&
        buff[2] == '3')
        ret = ((buff[6]<<21)|(buff[7]<<14)|(buff[8]<<7)|buff[9]) + 10;
    //ID3V1.1
    else if(ret > 127 &&
        buff[0] == 'T' &&
        buff[1] == 'A' &&
        buff[2] == 'G')
        ret = 128;
    else
        ret = 0;
    //
    close(fd);
    return ret;
}

unsigned int id3_info(char *filePath, void *privateData, 
    void (*callback)(void *privateData, char type[4], char *info, int len))
{

    return 0;
}
