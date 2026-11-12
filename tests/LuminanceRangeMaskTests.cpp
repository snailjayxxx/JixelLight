#include <QtTest>
#include <limits>
#include "core/pipeline/LuminanceRangeMask.h"
class LuminanceRangeMaskTests:public QObject {
    Q_OBJECT
private slots:
    void analyticPlateauShouldersAndExposure() {
        LuminanceRangeMask::Parameters p{true,false,1,.25f,.75f,.125f};
        for(float y:{.25f,.5f,.75f}) {QCOMPARE(LuminanceRangeMask::weight(y,p),1.f);QCOMPARE(LuminanceRangeMask::gain(y,p),2.f);}
        for(float y:{.125f,.875f,2.f}) {QCOMPARE(LuminanceRangeMask::weight(y,p),0.f);QCOMPARE(LuminanceRangeMask::gain(y,p),1.f);}
        for(float y:{.1875f,.8125f}) {QCOMPARE(LuminanceRangeMask::weight(y,p),.5f);QCOMPARE(LuminanceRangeMask::gain(y,p),std::sqrt(2.f));}
        p.exposure=-2;QCOMPARE(LuminanceRangeMask::gain(.5f,p),.25f);
    }
    void disabledAndZeroRemainIdentity() {
        LuminanceRangeMask::Parameters p;
        for(float y:{-100.f,0.f,.5f,1.f,100.f}) {QCOMPARE(LuminanceRangeMask::weight(y,p),0.f);QCOMPARE(LuminanceRangeMask::gain(y,p),1.f);}
        p.enabled=true;for(float y:{0.f,.3f,1.f,100.f})QCOMPARE(LuminanceRangeMask::gain(y,p),1.f);
        p.exposure=3;p.enabled=false;QCOMPARE(LuminanceRangeMask::gain(.5f,p),1.f);
    }
    void inversionIsAnExactComplementAndRemainsBounded() {
        LuminanceRangeMask::Parameters p{true,false,3,.1f,.7f,.2f};auto inverted=p;inverted.inverted=true;
        float previous=LuminanceRangeMask::weight(-.2f,p);
        for(int i=-1000;i<=10000;++i) {const float y=i/5000.f,w=LuminanceRangeMask::weight(y,p),other=LuminanceRangeMask::weight(y,inverted);
            QVERIFY(w>=0&&w<=1);QCOMPARE(w+other,1.f);QVERIFY(std::abs(w-previous)<.01f);previous=w;
            const float gain=LuminanceRangeMask::gain(y,p);QVERIFY(gain>=1&&gain<=8);
        }
    }
    void blackAndHdrSelectionDoesNotClipTheInputDomain() {
        LuminanceRangeMask::Parameters p{true,false,1,0,.1f,.02f};
        QCOMPARE(LuminanceRangeMask::weight(-4,p),1.f);QCOMPARE(LuminanceRangeMask::weight(0,p),1.f);
        p.lower=1.5f;p.upper=2;QCOMPARE(LuminanceRangeMask::weight(1.7f,p),1.f);QCOMPARE(LuminanceRangeMask::weight(3,p),0.f);
        p.inverted=true;QCOMPARE(LuminanceRangeMask::weight(3,p),1.f);
        for(float y:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {QCOMPARE(LuminanceRangeMask::weight(y,p),0.f);QCOMPARE(LuminanceRangeMask::gain(y,p),1.f);}
    }
    void minimumFeatherAndCoincidentBoundsHaveFiniteSupport() {
        LuminanceRangeMask::Parameters p{true,false,1,.5f,.5f,.001f};QVERIFY(LuminanceRangeMask::valid(p));
        QCOMPARE(LuminanceRangeMask::weight(.5f,p),1.f);QCOMPARE(LuminanceRangeMask::weight(.498f,p),0.f);QCOMPARE(LuminanceRangeMask::weight(.502f,p),0.f);
        for(int i=0;i<1000;++i) {const float w=LuminanceRangeMask::weight(.499f+i*.000002f,p);QVERIFY(std::isfinite(w)&&w>=0&&w<=1);}
    }
    void invalidParametersAreRejectedBeforeEvaluation() {
        const LuminanceRangeMask::Parameters good;QVERIFY(LuminanceRangeMask::valid(good));
        for(auto field:{&LuminanceRangeMask::Parameters::exposure,&LuminanceRangeMask::Parameters::lower,&LuminanceRangeMask::Parameters::upper,&LuminanceRangeMask::Parameters::feather}) {
            auto p=good;p.*field=std::numeric_limits<float>::quiet_NaN();QVERIFY(!LuminanceRangeMask::valid(p));
            p=good;p.*field=std::numeric_limits<float>::infinity();QVERIFY(!LuminanceRangeMask::valid(p));
        }
        auto p=good;p.lower=1.1f;QVERIFY(!LuminanceRangeMask::valid(p));p=good;p.upper=2.01f;QVERIFY(!LuminanceRangeMask::valid(p));p=good;p.feather=0;QVERIFY(!LuminanceRangeMask::valid(p));p=good;p.exposure=-3.01f;QVERIFY(!LuminanceRangeMask::valid(p));
    }
};
QTEST_GUILESS_MAIN(LuminanceRangeMaskTests)
#include "LuminanceRangeMaskTests.moc"
