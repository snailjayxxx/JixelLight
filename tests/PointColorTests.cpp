#include <QtTest>
#include <cmath>
#include <limits>
#include "core/pipeline/PointColor.h"
class PointColorTests:public QObject {
    Q_OBJECT
    static PointColor::Lab polar(float L,float C,float h){return {L,C*std::cos(h*3.14159265358979323846f/180),C*std::sin(h*3.14159265358979323846f/180)};}
private slots:
    void disabledZeroAndUnselectedColorsStayExact() {
        PointColor::Parameters p;
        for(auto v:{PointColor::Lab{.6f,.2f,0},PointColor::Lab{-2,1,-3},PointColor::Lab{3,9,7}}) {
            const auto a=PointColor::apply(v,p);QCOMPARE(a.L,v.L);QCOMPARE(a.a,v.a);QCOMPARE(a.b,v.b);
            p.enabled=true;const auto b=PointColor::apply(v,p);QCOMPARE(b.L,v.L);QCOMPARE(b.a,v.a);QCOMPARE(b.b,v.b);p.enabled=false;
        }
        p.enabled=true;p.hueShift=90;p.saturationShift=1;p.lightnessShift=.3f;
        const auto v=polar(.6f,.2f,120),a=PointColor::apply(v,p);QCOMPARE(PointColor::weight(v,p),0.f);QCOMPARE(a.L,v.L);QCOMPARE(a.a,v.a);QCOMPARE(a.b,v.b);
    }
    void selectedCenterHasAnalyticHueChromaAndLightnessChanges() {
        PointColor::Parameters p;p.enabled=true;const PointColor::Lab v{.6f,.2f,0};QCOMPARE(PointColor::weight(v,p),1.f);
        p.hueShift=90;auto a=PointColor::apply(v,p);QVERIFY(std::abs(a.a)<1.e-7);QVERIFY(std::abs(a.b-.2f)<1.e-7);QCOMPARE(a.L,v.L);
        p.hueShift=0;p.saturationShift=1;p.lightnessShift=.3f;a=PointColor::apply(v,p);QCOMPARE(a.a,.4f);QCOMPARE(a.b,0.f);QVERIFY(std::abs(a.L-.9f)<1.e-7);
        p.saturationShift=-1;p.lightnessShift=-.3f;a=PointColor::apply(v,p);QCOMPARE(a.a,0.f);QCOMPARE(a.b,0.f);QCOMPARE(a.L,.3f);
    }
    void circularHueAndNeutralSelectionsHaveNoSeam() {
        PointColor::Parameters p;p.enabled=true;p.centerHue=179;
        QCOMPARE(PointColor::weight(polar(.6f,.2f,-179),p),1.f);QCOMPARE(PointColor::weight(polar(.6f,.2f,-90),p),0.f);
        p.hueRadius=180;QCOMPARE(PointColor::weight(polar(.6f,.2f,-1),p),1.f);
        p.centerChroma=0;p.centerHue=173;p.hueRadius=1;QCOMPARE(PointColor::weight({.6f,0,0},p),1.f);
        p.hueShift=100;p.saturationShift=1;const auto a=PointColor::apply({.6f,0,0},p);QCOMPARE(a.a,0.f);QCOMPARE(a.b,0.f);
    }
    void smoothIndependentRangeShouldersMultiply() {
        PointColor::Parameters p;p.enabled=true;
        QVERIFY(std::abs(PointColor::weight({.6f+p.lightnessRadius*.75f,.2f,0},p)-.5f)<1.e-6);
        QVERIFY(std::abs(PointColor::weight({.6f,.2f+p.chromaRadius*.75f,0},p)-.5f)<1.e-6);
        QVERIFY(std::abs(PointColor::weight(polar(.6f,.2f,p.hueRadius*.75f),p)-.5f)<1.e-6);
        QVERIFY(std::abs(PointColor::weight(polar(.6f+p.lightnessRadius*.75f,.2f+p.chromaRadius*.75f,p.hueRadius*.75f),p)-.125f)<1.e-6);
        QCOMPARE(PointColor::weight({.6f+p.lightnessRadius,.2f,0},p),0.f);
    }
    void manualSrgbConversionsPreserveDisplayColors() {
        for(const std::array<float,3> rgb:{std::array<float,3>{0,0,0},{1,1,1},{1,0,0},{0,1,0},{0,0,1},{.2f,.4f,.7f},{.04f,.003f,.9f}}) {
            const auto lab=PointColor::fromEncodedSrgb(rgb);const auto back=PointColor::toEncodedSrgb(lab);
            for(int c=0;c<3;++c)QVERIFY2(std::abs(back[c]-rgb[c])<1.f/65535.f,qPrintable(QString("rgb %1 %2 %3, channel %4 roundtrip %5 error %6").arg(rgb[0]).arg(rgb[1]).arg(rgb[2]).arg(c).arg(back[c],0,'g',10).arg(std::abs(back[c]-rgb[c]),0,'g',10)));
        }
        const auto red=PointColor::fromEncodedSrgb({1,0,0});QVERIFY(std::abs(red.L-.62795536f)<2.e-6f);QVERIFY(std::abs(red.a-.22486306f)<2.e-6f);QVERIFY(std::abs(red.b-.1258463f)<2.e-6f);
        const auto swatch=PointColor::toEncodedSrgb({2,3,-2});for(float c:swatch)QVERIFY(std::isfinite(c)&&c>=0&&c<=1);
    }
    void strictFiniteSettingsAndUnclippedWorkingValues() {
        PointColor::Parameters p;QVERIFY(PointColor::valid(p));
        for(auto field:{&PointColor::Parameters::hueRadius,&PointColor::Parameters::chromaRadius,&PointColor::Parameters::lightnessRadius}){auto bad=p;bad.*field=0;QVERIFY(!PointColor::valid(bad));bad.*field=std::numeric_limits<float>::quiet_NaN();QVERIFY(!PointColor::valid(bad));}
        p.enabled=true;p.centerLightness=1.4f;p.lightnessShift=.3f;const auto a=PointColor::apply({1.4f,.2f,0},p);QVERIFY(a.L>1);QCOMPARE(a.a,.2f);
    }
};
QTEST_GUILESS_MAIN(PointColorTests)
#include "PointColorTests.moc"
