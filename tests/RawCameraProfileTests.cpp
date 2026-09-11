#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "core/raw/RawCameraProfiles.h"
#include "core/raw/RawContainerMetadata.h"

namespace {
void put16(QByteArray &b, quint16 v) {
    b.append(char(v & 0xff)); b.append(char((v >> 8) & 0xff));
}
void put32(QByteArray &b, quint32 v) {
    b.append(char(v & 0xff)); b.append(char((v >> 8) & 0xff));
    b.append(char((v >> 16) & 0xff)); b.append(char((v >> 24) & 0xff));
}
void entry(QByteArray &b, quint16 tag, quint16 type, quint32 count, quint32 valueOrOffset) {
    put16(b, tag); put16(b, type); put32(b, count); put32(b, valueOrOffset);
}
}

class RawCameraProfileTests : public QObject {
    Q_OBJECT
private slots:
    void a7r6ProfileUsesValidatedCharacterization() {
        const auto profile = RawCameraProfiles::find(QStringLiteral("SONY"), QStringLiteral("ILCE-7RM6"));
        QVERIFY(profile.has_value());
        QCOMPARE(profile->blackLevel, 512);
        QCOMPARE(profile->whiteLevel, 16383);
        QVERIFY(profile->provenance.contains(QStringLiteral("RawSpeed PR #979")));
        const std::array<double,9> expected{1.1765,-0.5595,-0.1192,-0.3689,1.1507,0.2485,0.0051,0.0681,0.5731};
        for (int i=0;i<9;++i) QVERIFY(std::abs(profile->cameraToXyz[std::size_t(i)]-expected[std::size_t(i)])<1e-9);
        QVERIFY(!RawCameraProfiles::find(QStringLiteral("SONY"), QStringLiteral("ILCE-7RM5")).has_value());
        QVERIFY(!RawCameraProfiles::find(QStringLiteral("Canon"), QStringLiteral("ILCE-7RM6")).has_value());
    }

    void classicTiffDefaultCropHonorsActiveAreaAndSubIfd() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path=dir.filePath(QStringLiteral("crop.arw"));
        QByteArray bytes;
        bytes.append("II",2); put16(bytes,42); put32(bytes,8);
        // Root IFD at 8: one SubIFDs entry pointing to offset 26.
        put16(bytes,1); entry(bytes,330,4,1,26); put32(bytes,0);
        QCOMPARE(bytes.size(),26);
        // SubIFD: crop origin/size, black, white and ActiveArea.
        put16(bytes,5);
        entry(bytes,50719,4,2,92);   // DefaultCropOrigin
        entry(bytes,50720,4,2,100);  // DefaultCropSize
        entry(bytes,50714,4,1,512);  // BlackLevel
        entry(bytes,50717,4,1,16383);// WhiteLevel
        entry(bytes,50829,4,4,108);  // ActiveArea
        put32(bytes,0);
        QCOMPARE(bytes.size(),92);
        put32(bytes,12); put32(bytes,8);       // origin relative to ActiveArea
        put32(bytes,9984); put32(bytes,6656);  // size
        put32(bytes,4); put32(bytes,6); put32(bytes,7000); put32(bytes,10000); // top,left,bottom,right
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes),qint64(bytes.size())); file.close();

        const auto info=RawContainerMetadata::read(path);
        QCOMPARE(info.defaultCrop,QRect(18,12,9984,6656));
        QCOMPARE(info.blackLevel,512);
        QCOMPARE(info.whiteLevel,16383);
        QVERIFY(info.cropSource.contains(QStringLiteral("DefaultCrop")));
    }
};

QTEST_MAIN(RawCameraProfileTests)
#include "RawCameraProfileTests.moc"
