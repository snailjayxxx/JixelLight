#pragma once
#include "core/look/LookLut.h"
#include "core/async/LatestJob.h"
#include <QVariantMap>
struct LookCalibrationRequest {QImage linearSource,reference;quint64 photo=0,revision=0;QVariantMap provenance;};
struct LookCalibrationResult {std::shared_ptr<const LookLut> lut;QVariantMap report;QString error;};
// Fits an image-specific sRGB color transform. No claim of universal camera calibration.
LookCalibrationResult calibrateLook(const LookCalibrationRequest &request,const CancelToken &cancel={});
// Also exposed for deterministic image-pair regression tests; inputs are display sRGB.
LookCalibrationResult fitLookImages(const QImage &baseline,const QImage &reference,const CancelToken &cancel={});

struct LookCalibrationPair { QImage baseline,reference;QString id;bool validation=false; };
// Explicit independent validation scenes never update the fitted coefficients.
LookCalibrationResult fitLookDataset(const QVector<LookCalibrationPair> &pairs,const CancelToken &cancel={});
QImage calibrationSource(const QImage &linearSource,const QImage &reference,QVariantMap *geometry=nullptr);
