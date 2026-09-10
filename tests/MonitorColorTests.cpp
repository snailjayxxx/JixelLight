#include <QtTest>
#include <QColorSpace>
#include <QImage>
#include <QRgba64>
#include <array>
#include <cmath>

#include "core/color/ColorManagement.h"
#include "core/color/MonitorColorTransform.h"

namespace {
std::array<float,3> atlasGridSample(const QImage &atlas,int n,int r,int g,int b) {
    if(atlas.isNull()||atlas.format()!=QImage::Format_RGBA32FPx4||atlas.size()!=QSize(n*n,n))return {};
    const auto *row=reinterpret_cast<const float *>(atlas.constScanLine(g));
    const int x=b*n+r;
    return {row[x*4+0],row[x*4+1],row[x*4+2]};
}
QImage inverseLut(int n=33) {
    QImage lut=MonitorColorTransform::identityLut(n);
    for(int y=0;y<lut.height();++y) {
        auto *row=reinterpret_cast<float *>(lut.scanLine(y));
        for(int x=0;x<lut.width();++x) {
            row[x*4+0]=1.0f-row[x*4+0];
            row[x*4+1]=1.0f-row[x*4+1];
            row[x*4+2]=1.0f-row[x*4+2];
        }
    }
    return lut;
}
}

class MonitorColorTests final : public QObject {
    Q_OBJECT
private slots:
    void identityLutUsesExpectedPacking() {
        constexpr int n=33;
        const QImage lut=MonitorColorTransform::identityLut(n);
        QCOMPARE(lut.format(),QImage::Format_RGBA32FPx4);
        QCOMPARE(lut.size(),QSize(n*n,n));
        const auto black=atlasGridSample(lut,n,0,0,0);
        const auto white=atlasGridSample(lut,n,n-1,n-1,n-1);
        const auto sample=atlasGridSample(lut,n,8,16,24);
        QVERIFY(std::abs(black[0])<1e-7f&&std::abs(black[1])<1e-7f&&std::abs(black[2])<1e-7f);
        QVERIFY(std::abs(white[0]-1)<1e-7f&&std::abs(white[1]-1)<1e-7f&&std::abs(white[2]-1)<1e-7f);
        QVERIFY(std::abs(sample[0]-.25f)<1e-7f);
        QVERIFY(std::abs(sample[1]-.50f)<1e-7f);
        QVERIFY(std::abs(sample[2]-.75f)<1e-7f);
    }

    void srgbMonitorProfileIsNearIdentity() {
        constexpr int n=33;
        QString error;
        const QImage lut=MonitorColorTransform::srgbToMonitorLut(
            ColorManagement::iccProfile(ColorManagement::OutputSpace::SRgb),n,&error);
        QVERIFY2(!lut.isNull(),qPrintable(error));
        for(const auto &point:std::array<std::array<int,3>,5>{{
                {{0,0,0}},{{8,16,24}},{{16,16,16}},{{32,4,20}},{{32,32,32}}
            }}) {
            const auto value=atlasGridSample(lut,n,point[0],point[1],point[2]);
            QVERIFY(std::abs(value[0]-float(point[0])/(n-1))<0.003f);
            QVERIFY(std::abs(value[1]-float(point[1])/(n-1))<0.003f);
            QVERIFY(std::abs(value[2]-float(point[2])/(n-1))<0.003f);
        }
    }

    void displayP3ProfileProducesDeviceRgbTransform() {
        constexpr int n=33;
        QString error;
        const QImage lut=MonitorColorTransform::srgbToMonitorLut(
            ColorManagement::iccProfile(ColorManagement::OutputSpace::DisplayP3),n,&error);
        QVERIFY2(!lut.isNull(),qPrintable(error));
        const auto red=atlasGridSample(lut,n,n-1,0,0);
        // sRGB red lies inside Display P3 but is not the P3 device red primary;
        // a real monitor-space transform therefore produces non-zero G/B.
        QVERIFY2(red[0]>0.85f&&red[0]<0.98f,qPrintable(QString::number(red[0])));
        QVERIFY2(red[1]>0.08f&&red[1]<0.35f,qPrintable(QString::number(red[1])));
        QVERIFY2(red[2]>0.04f&&red[2]<0.28f,qPrintable(QString::number(red[2])));
    }

    void cpuIdentityLutPreservesEncodedSrgb() {
        QImage image(2,1,QImage::Format_RGBA64);
        auto *row=reinterpret_cast<QRgba64 *>(image.scanLine(0));
        row[0]=QRgba64::fromRgba64(12345,34567,54321,50000);
        row[1]=QRgba64::fromRgba64(65535,0,32768,65535);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        const QImage out=MonitorColorTransform::applyLut(image,MonitorColorTransform::identityLut(),33);
        QVERIFY(!out.isNull());
        QVERIFY(!out.colorSpace().isValid());
        const auto *actual=reinterpret_cast<const QRgba64 *>(out.constScanLine(0));
        for(int i=0;i<2;++i) {
            QVERIFY(std::abs(int(actual[i].red())-int(row[i].red()))<=1);
            QVERIFY(std::abs(int(actual[i].green())-int(row[i].green()))<=1);
            QVERIFY(std::abs(int(actual[i].blue())-int(row[i].blue()))<=1);
            QCOMPARE(actual[i].alpha(),row[i].alpha());
        }
    }

    void cpuDisplayPathUsesSameLutConventionAsShader() {
        QImage image(1,1,QImage::Format_RGBA64);
        auto *pixel=reinterpret_cast<QRgba64 *>(image.scanLine(0));
        pixel[0]=QRgba64::fromRgba64(16384,32768,49151,60000);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        const QImage out=MonitorColorTransform::applyLut(image,inverseLut(),33);
        QVERIFY(!out.isNull());
        const auto p=reinterpret_cast<const QRgba64 *>(out.constScanLine(0))[0];
        QVERIFY(std::abs(int(p.red())-(65535-16384))<=2);
        QVERIFY(std::abs(int(p.green())-(65535-32768))<=2);
        QVERIFY(std::abs(int(p.blue())-(65535-49151))<=2);
        QCOMPARE(p.alpha(),quint16(60000));
    }

    void cpuDisplayNormalizesTaggedInputToSrgbBeforeMonitorLut() {
        QImage p3(1,1,QImage::Format_RGBA64);
        auto *pixel=reinterpret_cast<QRgba64 *>(p3.scanLine(0));
        pixel[0]=QRgba64::fromRgba64(50000,18000,9000,65535);
        p3.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        const QImage expected=p3.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64);
        QVERIFY(!expected.isNull());
        const QImage out=MonitorColorTransform::applyLut(p3,MonitorColorTransform::identityLut(),33);
        QVERIFY(!out.isNull());
        const auto a=reinterpret_cast<const QRgba64 *>(expected.constScanLine(0))[0];
        const auto b=reinterpret_cast<const QRgba64 *>(out.constScanLine(0))[0];
        QVERIFY(std::abs(int(a.red())-int(b.red()))<=2);
        QVERIFY(std::abs(int(a.green())-int(b.green()))<=2);
        QVERIFY(std::abs(int(a.blue())-int(b.blue()))<=2);
    }

    void invalidProfileFailsClosed() {
        QString error;
        const QImage lut=MonitorColorTransform::srgbToMonitorLut(QByteArrayLiteral("not-an-icc-profile"),33,&error);
        QVERIFY(lut.isNull());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(MonitorColorTests)
#include "MonitorColorTests.moc"
