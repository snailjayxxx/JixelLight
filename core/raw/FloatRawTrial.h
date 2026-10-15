#pragma once

#include "core/raw/RawDecoder.h"
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>

// Stage 6C.2: stand-alone, explicitly invoked real-RAW FP32 research.
// Neither successful decoding nor good patch statistics grants permission
// to use this pipeline for ordinary RAW images.
struct FloatRawTrial final {
    struct Request {
        QString rawPath;
        SensorLinearMosaic::Calibration sensor;
        SensorFloatDevelop::Profile color;
        QRect sampleRoi;   // must include the required active-sensor halo
        QRect outputRoi;   // absolute sensor coordinates
        QString referencePath; // optional separately hashed JSON patch means
        QByteArray referenceSha256;
    };
    struct Result {
        QJsonObject report;
        QString error;
        bool valid() const { return error.isEmpty() && !report.isEmpty(); }
    };
    static bool parse(const QJsonObject &manifest,const QString &manifestDirectory,
                      Request *request,QString *error);
    static QJsonObject comparePatchMeans(const QImage &frame,const QRect &rawRoi,
                                        const QJsonObject &reference,
                                        const QByteArray &sourceSha256,QString *error);
    static Result run(const Request &request,const CancelToken &cancel={});
};
