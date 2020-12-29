
#include <stdio.h>
#include <sys/time.h>

void delayus(unsigned int us)
{
    struct timeval delay;
    delay.tv_sec = us / 1000000;
    delay.tv_usec = us % 1000000;
    select(0, NULL, NULL, NULL, &delay);
}

unsigned int getTickUs(void)
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec * 1000000u + tv.tv_usec);
}
