#ifndef _DELAY_H_
#define _DELAY_H_

void delayus(unsigned int us);
unsigned int getTickUs(void);

/* 自动校准的延时3件套 */
#define DELAY_INIT \
    unsigned int _tick1 = 0, _tick2;

#define DELAY_RESET() \
    _tick1 = getTickUs();

#define DELAY_US(us)                             \
    _tick2 = getTickUs();                        \
    if (_tick2 > _tick1 && _tick2 - _tick1 < us) \
        delayus(us - (_tick2 - _tick1));         \
    _tick1 = getTickUs();

#define DELAY_INIT2 \
    unsigned int _tick = 0, _tickErr;

//逐级逼近20ms延时,时差大则用大延时,时差小则用小延时
#define DELAY_US2(us, err)             \
    _tickErr = getTickUs() - _tick;    \
    if (_tickErr > 0 && us > _tickErr) \
    {                                  \
        _tickErr = us - _tickErr;      \
        if (_tickErr > err)            \
        {                              \
            delayus(_tickErr / 2);     \
            continue;                  \
        }                              \
    }                                  \
    _tick = getTickUs();

#endif