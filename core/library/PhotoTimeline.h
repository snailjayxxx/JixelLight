#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QTimeZone>
#include <cmath>

// Camera capture time is a wall-clock record, not a guessed UTC timestamp.
// Import/edit times are UTC milliseconds owned by the catalog.
struct PhotoTimeline {
    QString captureTime;
    bool captureChecked = false;
    qint64 importedAt = 0, editedAt = 0;
    static QString cameraTime(const QString &text) {
        if (text.size() != 19) return {};
        const auto date = QDate::fromString(text.left(10),"yyyy:MM:dd");
        const auto time = QTime::fromString(text.mid(11),"HH:mm:ss");
        if (!date.isValid() || !time.isValid() || text[10] != ' ') return {};
        return date.toString(Qt::ISODate) + " " + time.toString("HH:mm:ss");
    }
    qint64 captureOrder() const {
        if (captureTime.isEmpty()) return 0;
        return QDate::fromString(captureTime.left(10),Qt::ISODate).toJulianDay()*86400000LL
            + QTime::fromString(captureTime.mid(11),"HH:mm:ss").msecsSinceStartOfDay();
    }
    static qint64 sqlImportTime(const QString &text) {
        const auto date = QDate::fromString(text.left(10),Qt::ISODate);
        const auto time = QTime::fromString(text.mid(11),"HH:mm:ss");
        if (text.size() != 19 || !date.isValid() || !time.isValid() || text[10] != ' ') return 0;
        return QDateTime(date,time,QTimeZone::UTC).toMSecsSinceEpoch();
    }
    QJsonObject toJson() const {
        return {{"schema",1},{"capture",captureTime},{"captureChecked",captureChecked},
                {"imported",importedAt},{"edited",editedAt}};
    }
    static bool fromJson(const QJsonObject &json, PhotoTimeline *result) {
        if (!result || json.size() != 5 || json.value("schema").toDouble() != 1
            || !json.value("capture").isString() || !json.value("captureChecked").isBool()) return false;
        PhotoTimeline staged; staged.captureTime=json.value("capture").toString(); staged.captureChecked=json.value("captureChecked").toBool();
        if (!staged.captureTime.isEmpty()) {
            if (staged.captureTime.size() != 19) return false;
            auto raw=staged.captureTime; raw[4]=':'; raw[7]=':';
            if (!staged.captureChecked || cameraTime(raw) != staged.captureTime) return false;
        }
        for (const auto *field : {"imported","edited"}) {
            const auto value=json.value(QLatin1String(field)); const auto number=value.toDouble(-1);
            if (!value.isDouble() || !std::isfinite(number) || number < 0 || number > 253402300799999.0 || number != std::floor(number)) return false;
        }
        staged.importedAt=qint64(json.value("imported").toDouble()); staged.editedAt=qint64(json.value("edited").toDouble());
        if (staged.toJson() != json) return false;
        *result=staged; return true;
    }
};
