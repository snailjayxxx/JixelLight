#pragma once
#include <QImage>
#include <QVariantMap>
#include "core/async/LatestJob.h"
#include "core/scopes/ScopesEngine.h"
struct CameraReferenceRequest {QString rawPath,manualPath;QVariantMap metadata;quint64 photo=0;};
struct CameraReferenceResult {QImage image;QVariantMap info;ScopesResult scopes;QString error;};
namespace CameraReference {
bool matches(const QVariantMap &raw,const QVariantMap &jpeg,QString *reason=nullptr);
CameraReferenceResult load(const CameraReferenceRequest &request,const CancelToken &cancel={});
}
