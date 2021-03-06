#include <QDebug>
#include <QMetaEnum>
#include <QDateTime>
#include <QTimeZone>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "common_utility.h"
#include "bar_collector.h"

BarCollector::BarCollector(const QString &instrumentID, int timeFrameFlags, bool saveBarsToDB, QObject *parent) :
    QObject(parent),
    instrument(instrumentID),
    saveBarsToDB(saveBarsToDB)
{
    keys = enumValueToList<TimeFrames>(timeFrameFlags);
    for (auto key : qAsConst(keys)) {
        barMap.insert(key, Bar());
    }

    if (saveBarsToDB) {
        QSqlDatabase sqlDB = QSqlDatabase::database();
        QSqlQuery qry(sqlDB);
        if (!qry.exec("SHOW DATABASES")) {
            qCritical().noquote() << "Show databases failed!";
            qCritical().noquote() << qry.lastError();
            this->saveBarsToDB = false;
            return;
        }
        QStringList dbNames;
        while (qry.next()) {
            QString dbName = qry.value(0).toString();
            dbNames << dbName;
        }
        if (!dbNames.contains("market", Qt::CaseInsensitive)) {
            if (!qry.exec("CREATE DATABASE market")) {
                qCritical().noquote() << "Create database market failed!";
                qCritical().noquote() << qry.lastError();
                this->saveBarsToDB = false;
                return;
            }
        }
        sqlDB.close();
        sqlDB.setDatabaseName("market");
        sqlDB.open();
        const QStringList tables = sqlDB.tables();

        for (auto key : qAsConst(keys)) {
            // Check if the table is already created for collected bars
            QString tableName = QString("%1_%2").arg(instrumentID, QMetaEnum::fromType<TimeFrames>().valueToKey(key));
            if (!tables.contains(tableName, Qt::CaseInsensitive)) {
                QString tableOfDB = QString("market.%1").arg(tableName);
                bool ok = qry.exec("CREATE TABLE " + tableOfDB + " ("
                                   "`time` BIGINT NOT NULL,"
                                   "`open` DOUBLE NULL,"
                                   "`high` DOUBLE NULL,"
                                   "`low` DOUBLE NULL,"
                                   "`close` DOUBLE NULL,"
                                   "`tick_volume` BIGINT NULL,"
                                   "`volume` BIGINT NULL,"
                                   "`type` INT NULL,"
                                   "PRIMARY KEY (`time`))");

                if (!ok) {
                    qCritical().noquote() << "Create table" << tableName << "failed!";
                    qCritical().noquote() << qry.lastError();
                    this->saveBarsToDB = false;
                    break;
                }
            }
        }
        sqlDB.close();
        sqlDB.setDatabaseName("indicator");
        sqlDB.open();
    }
}

BarCollector::~BarCollector()
{
}

#define MIN_UNIT    60
#define HOUR_UNIT   3600

static const QMap<BarCollector::TimeFrame, int> g_time_table = {
    {BarCollector::SEC1,    1},
    {BarCollector::SEC2,    2},
    {BarCollector::SEC3,    3},
    {BarCollector::SEC4,    4},
    {BarCollector::SEC5,    5},
    {BarCollector::SEC6,    6},
    {BarCollector::SEC10,  10},
    {BarCollector::SEC12,  12},
    {BarCollector::SEC15,  15},
    {BarCollector::SEC20,  20},
    {BarCollector::SEC30,  30},
    {BarCollector::MIN1,    1 * MIN_UNIT},
    {BarCollector::MIN2,    2 * MIN_UNIT},
    {BarCollector::MIN3,    3 * MIN_UNIT},
    {BarCollector::MIN4,    4 * MIN_UNIT},
    {BarCollector::MIN5,    5 * MIN_UNIT},
    {BarCollector::MIN6,    6 * MIN_UNIT},
    {BarCollector::MIN10,  10 * MIN_UNIT},
    {BarCollector::MIN12,  12 * MIN_UNIT},
    {BarCollector::MIN15,  15 * MIN_UNIT},
    {BarCollector::MIN30,  30 * MIN_UNIT},
    {BarCollector::HOUR1,   1 * HOUR_UNIT},
    {BarCollector::HOUR2,   2 * HOUR_UNIT},
    {BarCollector::HOUR3,   3 * HOUR_UNIT},
    {BarCollector::HOUR4,   4 * HOUR_UNIT},
    {BarCollector::HOUR6,   6 * HOUR_UNIT},
    {BarCollector::HOUR8,   8 * HOUR_UNIT},
    {BarCollector::HOUR12, 12 * HOUR_UNIT},
    {BarCollector::DAY,    24 * HOUR_UNIT},
};

void BarCollector::setTradingDay(const QString &tradingDay, const QString &lastNight)
{
    auto date = QDateTime::fromString(tradingDay, QStringLiteral("yyyyMMdd"));
    date.setTimeZone(QTimeZone::utc());
    auto newTradingDayBase = date.toSecsSinceEpoch();
    if (tradingDayBase != newTradingDayBase) {
        tradingDayBase = newTradingDayBase;
        lastVolume = 0;
    }

    date = QDateTime::fromString(lastNight, QStringLiteral("yyyyMMdd"));
    date.setTimeZone(QTimeZone::utc());
    lastNightBase = date.toSecsSinceEpoch();
    morningBase = lastNightBase + 24 * 3600;
}

bool BarCollector::onMarketData(int time, double lastPrice, int volume)
{
    const bool isNewTick = (volume != lastVolume);
    qint64 currentTime = 0;
    if (time < 8 * 3600) {
        currentTime = morningBase + time;
    } else if (time > 17 * 3600) {
        currentTime = lastNightBase + time;
    } else {
        currentTime = tradingDayBase + time;
    }

    for (auto key : qAsConst(keys)) {
        Bar & bar = barMap[key];
        auto time_unit = g_time_table[static_cast<TimeFrame>(key)];  // TODO optimize, use time_unit as key

        if ((currentTime / time_unit) != (bar.time / time_unit)) {
            if (key != DAY) {
                saveEmitReset(key, bar);
            }
        }

        if (!isNewTick) {
            continue;
        }

        if (bar.isEmpty()) {
            bar.open = lastPrice;
            if (key == DAY) {
                bar.time = tradingDayBase;
            } else {
                bar.time = currentTime / time_unit * time_unit;
            }
        }

        if (lastPrice > bar.high) {
            bar.high = lastPrice;
        }

        if (lastPrice < bar.low) {
            bar.low = lastPrice;
        }

        bar.close = lastPrice;
        bar.tick_volume ++;
        bar.volume += (volume - lastVolume);
    }
    lastVolume = volume;
    return isNewTick;
}

void BarCollector::saveEmitReset(int timeFrame, Bar &bar)
{
    if (!bar.isEmpty()) {
        if (saveBarsToDB) {
            saveBar(timeFrame, bar);
        }
        emit collectedBar(instrument, timeFrame, bar);
        qInfo().noquote() << instrument << ":" << bar;
        bar.reset();
    }
}

void BarCollector::saveBar(int timeFrame, const Bar &bar)
{
    QSqlDatabase sqlDB = QSqlDatabase();
    QSqlQuery qry(sqlDB);
    QString tableName = QMetaEnum::fromType<TimeFrames>().valueToKey(timeFrame);
    QString tableOfDB = QString("market.%1_%2").arg(instrument).arg(tableName);
    qry.prepare("INSERT INTO " + tableOfDB + " (time, open, high, low, close, tick_volume, volume, type) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    qry.bindValue(0, bar.time);
    qry.bindValue(1, bar.open);
    qry.bindValue(2, bar.high);
    qry.bindValue(3, bar.low);
    qry.bindValue(4, bar.close);
    qry.bindValue(5, bar.tick_volume);
    qry.bindValue(6, bar.volume);
    qry.bindValue(7, 1);
    bool ok = qry.exec();
    if (!ok) {
        qCritical().noquote() << "Insert bar into" << tableOfDB << "failed!";
        qCritical().noquote() << qry.lastError();
    }
}

void BarCollector::flush(bool endOfDay)
{
    for (auto key : qAsConst(keys)) {
        if ((key != DAY) || ((key == DAY) && endOfDay)) {
            saveEmitReset(key, barMap[key]);
        }
    }
}
