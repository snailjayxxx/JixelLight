#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSysInfo>
#include <QRgba64>
#include <QThread>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "core/pipeline/ImagePipeline.h"
QImage legacyProcess(const QImage &,const AdjustmentState &,ImagePipeline::InputEncoding);
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const bool quick=app.arguments().contains("--quick");
    QJsonArray measurements;
    for (const int width:std::vector<int>{512,quick?768:2048}) {
        QImage image(width,width*2/3,QImage::Format_RGBA64);
        for(int y=0;y<image.height();++y) {
            auto *line=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<width;++x) line[x]=QRgba64::fromRgba64((x*137+y*37)%50000,(x*97+y*47)%50000,(x*71+y*67)%50000,65535);
        }
        for(int mode=0;mode<2;++mode) {
            AdjustmentState state;
            if(mode) { state.exposure=.4;state.temperature=15;state.highlights=-20;state.saturation=12;state.hslSaturation[5]=25;state.redCurve[2]=.54; }
            for(int backend=0;backend<3;++backend) {
                std::vector<double> samples;
                for(int repeat=-1;repeat<(quick?3:5);++repeat) {
                    QElapsedTimer timer;timer.start();
                    auto out=backend==0 ? legacyProcess(image,state,ImagePipeline::InputEncoding::LinearProPhoto)
                        : ImagePipeline::process(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,{},backend==2);
                    if(out.isNull()) return 2;
                    if(repeat>=0) samples.push_back(timer.nsecsElapsed()/1e6);
                }
                std::sort(samples.begin(),samples.end());QJsonArray values;for(auto v:samples) values.append(v);
                measurements.append(QJsonObject{{"width",width},{"height",image.height()},{"adjustments",mode?"color-and-tone":"neutral"},
                    {"backend",backend==0?"legacy_serial":backend==1?"optimized_serial":"optimized_parallel"},
                    {"median_ms",samples[samples.size()/2]},{"samples_ms",values}});
            }
        }
    }
    const auto document=QJsonDocument(QJsonObject{{"platform",QSysInfo::prettyProductName()},{"architecture",QSysInfo::currentCpuArchitecture()},
        {"logical_processors",QThread::idealThreadCount()},{"fixture","synthetic linear ProPhoto RGBA64; no RAW decode, GPU or UI included"},
        {"measurements",measurements}}).toJson(QJsonDocument::Indented);
    std::fwrite(document.constData(),1,size_t(document.size()),stdout);
}
