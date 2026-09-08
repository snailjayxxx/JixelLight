#pragma once
#include <QImage>
#include <array>
#include "core/async/LatestJob.h"
namespace LookDetail {
int halo(const std::array<float,4> &settings);
QImage apply(const QImage &image,const std::array<float,4> &settings,const CancelToken &cancel={});
}
