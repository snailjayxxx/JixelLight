#include <QtTest>
#include <random>
#include <limits>
#include "core/pipeline/SpatialDenoise.h"

class SpatialDenoiseTests:public QObject {
    Q_OBJECT
    static SpatialDenoise::Triple identity(SpatialDenoise::Triple value){return value;}
    static QImage plane(QSize size={17,15}) {
        QImage image(size,QImage::Format_RGBA32FPx4);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;p[0]=.5;p[1]=.01;p[2]=-.01;p[3]=1;
        }
        return image;
    }
    static QImage filter(const QImage &image,SpatialDenoise::Parameters p={1,1,2},bool parallel=true) {
        return SpatialDenoise::apply(image,image,p,identity,{},parallel);
    }
private slots:
    void flatDisabledAndTransparentIdentity() {
        auto source=plane();const auto original=source.copy();QCOMPARE(filter(source),original);
        QCOMPARE(filter(source,{0,0,2}),original);QCOMPARE(filter(source,{1,1,0}),original);
        for(int y=0;y<source.height();++y)for(int x=0;x<8;++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=1;p[1]=-.9;p[2]=.9;p[3]=0;
        }
        const auto before=source.copy();QCOMPARE(filter(source),before);QCOMPARE(source,before);
    }
    void analyticImpulseAndIndependentComponents() {
        auto source=plane({13,13});auto *center=reinterpret_cast<float *>(source.scanLine(6))+4*6;
        const float delta[]{.02f,.01f,-.01f};for(int c=0;c<3;++c)center[c]+=delta[c];
        const double spatial=std::pow(1+2*std::exp(-1/(2*1.2*1.2))+2*std::exp(-4/(2*1.2*1.2)),2)-1;
        const double range=std::exp(-.5*(.02*.02/(.04*.04)+(.01*.01+.01*.01)/(.06*.06)));
        const double mix=spatial*range/(1+spatial*range);
        for(const auto p : {SpatialDenoise::Parameters{1,1,2},{.6f,0,2},{0,.8f,2}}) {
            const auto actual=filter(source,p);QVERIFY(!actual.isNull());const auto *out=reinterpret_cast<const float *>(actual.constScanLine(6))+4*6;
            for(int c=0;c<3;++c){const double amount=c==0?p.lightness:p.chroma;QVERIFY(std::abs(out[c]-(center[c]-amount*delta[c]*mix))<2.e-7);}
            QCOMPARE(out[3],1.f);
        }
    }
    void suppressesNoiseAndPreservesIsolightnessColorEdge() {
        auto source=plane({129,131});std::mt19937 rng(54691);std::normal_distribution<float> noise(0,.008f);
        double varianceBefore=0;
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.5+noise(rng);p[1]=(x<64?-.18f:.18f)+noise(rng);p[2]=noise(rng);
            varianceBefore+=std::pow(p[0]-.5,2);
        }
        const auto out=filter(source);QVERIFY(!out.isNull());double varianceAfter=0,left=0,right=0;
        for(int y=0;y<out.height();++y)for(int x=0;x<out.width();++x) {
            const auto *p=reinterpret_cast<const float *>(out.constScanLine(y))+4*x;varianceAfter+=std::pow(p[0]-.5,2);
            if(x==63)left+=p[1];if(x==64)right+=p[1];QCOMPARE(p[3],1.f);
        }
        QVERIFY(varianceAfter<varianceBefore*.45);QVERIFY((right-left)/out.height()>.35);
        qInfo()<<"guide noise variance ratio"<<varianceAfter/varianceBefore<<"color edge"<<(right-left)/out.height();
    }
    void alphaWeightedImpulseAndSignedHdrSafety() {
        auto source=plane({13,13});for(int y=0;y<13;++y)for(int x=0;x<13;++x)reinterpret_cast<float *>(source.scanLine(y))[4*x+3]=.5;
        auto *center=reinterpret_cast<float *>(source.scanLine(6))+24;center[0]+=.02;center[3]=.25;
        const auto out=filter(source);QVERIFY(!out.isNull());
        const double spatial=std::pow(1+2*std::exp(-1/(2*1.2*1.2))+2*std::exp(-4/(2*1.2*1.2)),2)-1;
        const double neighbors=.5*spatial*std::exp(-.5*.02*.02/(.04*.04));
        const auto *pixel=reinterpret_cast<const float *>(out.constScanLine(6))+24;
        QVERIFY(std::abs(pixel[0]-(center[0]-.02*neighbors/(.25+neighbors)))<2.e-7);QCOMPARE(pixel[3],.25f);
        auto linear=plane(),guide=linear.copy();
        for(int y=0;y<linear.height();++y)for(int x=0;x<linear.width();++x){auto *p=reinterpret_cast<float *>(linear.scanLine(y))+4*x;p[0]=-2;p[1]=4;p[2]=.1;}
        QCOMPARE(SpatialDenoise::apply(linear,guide,{1,1,2},identity),linear);
    }
    void fractionalRadiusParallelAgreementAndCancellation() {
        auto source=plane({769,389});for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]+=float((x*17+y*7)%23-11)*.001f;}
        for(float radius : {.1f,.5f,1.7f,2.f})QCOMPARE(filter(source,{.7f,.8f,radius},true),filter(source,{.7f,.8f,radius},false));
        const auto token=std::make_shared<std::atomic_bool>(true);QVERIFY(SpatialDenoise::apply(source,source,{1,1,2},identity,token).isNull());
    }
    void arbitraryNeighborhoodMatchesIndependentDoubleOracle() {
        auto source=plane({13,11});std::mt19937 rng(9141);std::uniform_real_distribution<float> delta(-.025f,.025f);const float alphas[]{0,.01f,.5f,1};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;for(int c=0;c<3;++c)p[c]+=delta(rng);p[3]=alphas[(x+y)%4];}
        for(float radius : {.3f,.5f,1.3f,2.f})for(const auto amounts : {QPointF(.7,0),QPointF(0,.8),QPointF(.7,.8)}) {
            const auto actual=filter(source,{float(amounts.x()),float(amounts.y()),radius});QVERIFY(!actual.isNull());
            const int extent=int(std::ceil(radius));const double sigma=std::max(.01,.6*radius);
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
                const auto *center=reinterpret_cast<const float *>(source.constScanLine(y))+4*x,*out=reinterpret_cast<const float *>(actual.constScanLine(y))+4*x;QCOMPARE(out[3],center[3]);
                if(center[3]==0){for(int c=0;c<3;++c)QCOMPARE(out[c],center[c]);continue;}
                double sum=0,components[3]{};
                for(int yy=-extent;yy<=extent;++yy)for(int xx=-extent;xx<=extent;++xx) {
                    const auto *p=reinterpret_cast<const float *>(source.constScanLine(std::clamp(y+yy,0,source.height()-1)))+4*std::clamp(x+xx,0,source.width()-1);
                    const double L=double(p[0])-center[0],a=double(p[1])-center[1],b=double(p[2])-center[2];
                    const double edgeX=std::clamp(double(radius)+1-std::abs(xx),0.,1.),edgeY=std::clamp(double(radius)+1-std::abs(yy),0.,1.);
                    const double weight=edgeX*edgeY*p[3]*std::exp(-(xx*xx+yy*yy)/(2*sigma*sigma)-.5*(L*L/(.04*.04)+(a*a+b*b)/(.06*.06)));
                    sum+=weight;for(int c=0;c<3;++c)components[c]+=weight*p[c];
                }
                for(int c=0;c<3;++c){const double amount=c==0?amounts.x():amounts.y(),expected=center[c]+amount*(components[c]/sum-center[c]);QVERIFY(std::abs(out[c]-expected)<3.e-7);}
            }
        }
    }
    void invalidPlanesAndParametersRefuseAtomically() {
        const auto original=plane();auto guide=original.copy();reinterpret_cast<float *>(guide.scanLine(2))[8]=std::numeric_limits<float>::quiet_NaN();
        QVERIFY(SpatialDenoise::apply(original,guide,{1,1,2},identity).isNull());QCOMPARE(original,plane());
        guide=original.copy();reinterpret_cast<float *>(guide.scanLine(2))[11]=.5;QVERIFY(SpatialDenoise::apply(original,guide,{1,1,2},identity).isNull());
        for(const auto p : {SpatialDenoise::Parameters{-1,0,2},{0,2,2},{1,1,3},{1,1,std::numeric_limits<float>::infinity()}})QVERIFY(filter(original,p).isNull());
        QVERIFY(SpatialDenoise::apply(original,original,{1,1,2},nullptr).isNull());
    }
};
QTEST_GUILESS_MAIN(SpatialDenoiseTests)
#include "SpatialDenoiseTests.moc"
