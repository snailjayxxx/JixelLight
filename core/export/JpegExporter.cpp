#include "core/export/JpegExporter.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QSaveFile>
#include <QThread>
#include <algorithm>
#include <array>
#include <vector>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <jpeglib.h>
#include <jerror.h>

namespace {
struct JpegError { jpeg_error_mgr base{}; std::jmp_buf jump; char message[JMSG_LENGTH_MAX]{}; };
struct Destination {
    jpeg_destination_mgr base{};
    QSaveFile *file = nullptr;
    std::array<JOCTET,32768> buffer{};
};
void onError(j_common_ptr c) {
    auto *error = reinterpret_cast<JpegError *>(c->err);
    (*c->err->format_message)(c,error->message);
    std::longjmp(error->jump,1);
}
void initDestination(j_compress_ptr c) {
    auto *d=reinterpret_cast<Destination *>(c->dest);
    d->base.next_output_byte=d->buffer.data(); d->base.free_in_buffer=d->buffer.size();
}
boolean writeDestination(j_compress_ptr c) {
    auto *d=reinterpret_cast<Destination *>(c->dest);
    if (d->file->write(reinterpret_cast<const char *>(d->buffer.data()),d->buffer.size())!=qint64(d->buffer.size())) ERREXIT(c,JERR_FILE_WRITE);
    initDestination(c); return TRUE;
}
void finishDestination(j_compress_ptr c) {
    auto *d=reinterpret_cast<Destination *>(c->dest);
    const auto count=d->buffer.size()-d->base.free_in_buffer;
    if (d->file->write(reinterpret_cast<const char *>(d->buffer.data()),qint64(count))!=qint64(count)) ERREXIT(c,JERR_FILE_WRITE);
}
class Writer {
public:
    explicit Writer(const QString &path) : file(path) { info.err=jpeg_std_error(&error.base); error.base.error_exit=onError; }
    ~Writer() { if (created) jpeg_destroy_compress(&info); }
    bool begin(int width,int height,int quality,const QByteArray &profile) {
        if (width<=0 || height<=0 || width>JPEG_MAX_DIMENSION || height>JPEG_MAX_DIMENSION) { failure="Image exceeds JPEG dimension limit"; return false; }
        if (!file.open(QIODevice::WriteOnly)) { failure=file.errorString(); return false; }
        std::vector<QByteArray> markers;
        const int count=(int(profile.size())+65518)/65519;
        for (int i=0;i<count;++i) {
            QByteArray marker("ICC_PROFILE\0",12);
            marker.append(char(i+1)); marker.append(char(count)); marker.append(profile.mid(i*65519,65519));
            markers.push_back(marker);
        }
        if (setjmp(error.jump)) { failure=QString::fromLatin1(error.message); return false; }
        jpeg_create_compress(&info); created=true;
        destination.file=&file;
        destination.base.init_destination=initDestination;
        destination.base.empty_output_buffer=writeDestination;
        destination.base.term_destination=finishDestination;
        info.dest=&destination.base;
        info.image_width=JDIMENSION(width); info.image_height=JDIMENSION(height);
        info.input_components=3; info.in_color_space=JCS_RGB;
        jpeg_set_defaults(&info); jpeg_set_quality(&info,std::clamp(quality,1,100),TRUE);
        info.optimize_coding=FALSE;
        if (quality>=90) for (int i=0;i<3;++i) { info.comp_info[i].h_samp_factor=1; info.comp_info[i].v_samp_factor=1; }
        jpeg_start_compress(&info,TRUE);
        for (const auto &marker:markers) jpeg_write_marker(&info,JPEG_APP0+2,reinterpret_cast<const JOCTET *>(marker.constData()),unsigned(marker.size()));
        return true;
    }
    bool rows(const QImage &tile) {
        const QImage rgb=tile.convertToFormat(QImage::Format_RGB888);
        if (rgb.isNull()) { failure="Unable to allocate JPEG strip"; return false; }
        if (setjmp(error.jump)) { failure=QString::fromLatin1(error.message); return false; }
        for (int y=0;y<rgb.height();++y) {
            JSAMPROW row=const_cast<JSAMPROW>(rgb.constScanLine(y));
            if (jpeg_write_scanlines(&info,&row,1)!=1) { failure="JPEG encoder suspended unexpectedly"; return false; }
        }
        return true;
    }
    bool finish() {
        if (setjmp(error.jump)) { failure=QString::fromLatin1(error.message); return false; }
        jpeg_finish_compress(&info);
        if (!file.commit()) { failure=file.errorString(); return false; }
        return true;
    }
    QString failure;
private:
    QSaveFile file;
    jpeg_compress_struct info{};
    JpegError error;
    Destination destination;
    bool created=false;
};
}
bool exportJpegTiled(const QImage &source,const AdjustmentState &state,const QString &path,
                     ColorManagement::OutputSpace space,int quality,const CancelToken &token,
                     QString *error,const std::function<void(int)> &progress,
                     const std::shared_ptr<std::atomic_bool> &interactive,
                     bool rawSource,float rawBaseExposureStops) {
    if (error) error->clear();
    if (source.isNull() || cancelled(token)) { if (error) *error="Cancelled or no source image"; return false; }
    PerformanceSpan timing("jpeg_export",{{"pixels",qint64(source.width())*source.height()},{"space",ColorManagement::key(space)},
                                           {"raw",rawSource},{"raw_base_ev",rawBaseExposureStops}});
    Writer writer(path);
    if (!writer.begin(source.width(),source.height(),quality,ColorManagement::iccProfile(space))) { if (error) *error=writer.failure; return false; }
    const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space,rawSource,rawBaseExposureStops);
    const QImage input=source.format()==QImage::Format_RGBA64 ? source : source.convertToFormat(QImage::Format_RGBA64);
    for (int y=0;y<input.height();y+=128) {
        while (interactive && interactive->load(std::memory_order_relaxed) && !cancelled(token)) QThread::msleep(10);
        if (cancelled(token)) { if (error) *error="Cancelled"; return false; }
        const QImage rendered=ImagePipeline::processRegion(input,plan,QRect(0,y,input.width(),std::min(128,input.height()-y)),token);
        if (rendered.isNull() || !writer.rows(rendered)) { if (error) *error=cancelled(token) ? "Cancelled" : writer.failure; return false; }
        if (progress) progress(std::min(100,(y+rendered.height())*100/input.height()));
    }
    if (cancelled(token)) { if (error) *error="Cancelled"; return false; }
    if (!writer.finish()) { if (error) *error=writer.failure; return false; }
    return true;
}
