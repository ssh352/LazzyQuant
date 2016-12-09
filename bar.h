#ifndef BAR_H
#define BAR_H

#include <QMetaType>

#define TimeType int
typedef unsigned short      WORD;

struct KTExportBar {
    TimeType m_time;        //时间,UCT
    float   m_fOpen;        //开盘
    float   m_fHigh;        //最高
    float   m_fLow;         //最低
    float   m_fClose;       //收盘
    float   m_fVolume;      //成交量(手)
    float   m_fAmount;      //成交额(元)/持仓(未平仓合约，仅期货有效)
    WORD    m_wAdvance;     //上涨家数(仅大盘有效)
    WORD    m_wDecline;     //下跌家数(仅大盘有效)
    float   amount;
    float   settle;
};

class Bar {
public:
    uint   time;
    long   tick_volume; // to be compatible with MT5(although MT5 use 64bit long), and also for memory alignment
    double open;
    double high;
    double low;
    double close;
    double volume;

    Bar() { init(); }

    Bar(const Bar &other) {
        time = other.time;
        tick_volume = other.tick_volume;
        open = other.open;
        high = other.high;
        low = other.low;
        close = other.close;
        volume = other.volume;
    }

    Bar(const KTExportBar &ktbar) {
        time = ktbar.m_time;
        tick_volume = 0;
        open = ktbar.m_fOpen;
        high = ktbar.m_fHigh;
        low = ktbar.m_fLow;
        close = ktbar.m_fClose;
        volume = ktbar.m_fVolume;
    }

    void init() {
        time = 0;
        tick_volume = 0;
        open = -1.5f;
        high = -1.0f;
        low = 100000.0f;
        close = -1.0f;
        volume = 0.0f;
    }

    bool isNewBar() {
        return tick_volume == 0;
    }
};

Q_DECLARE_METATYPE(Bar)

#endif // BAR_H

