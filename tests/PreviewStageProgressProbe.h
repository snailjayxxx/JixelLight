#pragma once
#ifndef JIXELLIGHT_PREVIEW_STAGE_TEST_PROGRESS
#error PreviewStageProgressProbe is only available in the texture-cache test
#endif
#include <QImage>
#include <functional>
namespace PreviewStageProgressProbe {
inline thread_local std::function<void(const QImage &)> completed;
inline void notify(const QImage &image) {if(completed)completed(image);}
}
