#include <QtTest>
#include <QColorSpace>
#include <limits>
#include <random>
#include "core/pipeline/SpatialTexture.h"

class SpatialTextureTests:public QObject {
    Q_OBJECT
    static SpatialTexture::Triple identity(SpatialTexture::Triple value){return value;}
    static QImage plane(QSize size={17,15}) {
        QImage image(size,QImage::Format_RGBA32FPx4);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;p[0]=.5;p[1]=.01;p[2]=-.01;p[3]=1;
        }
        return image;
    }
    static QImage filter(const QImage &source,SpatialTexture::Parameters parameters={1,2,6},bool parallel=true) {
        return SpatialTexture::apply(source,parameters,identity,identity,{},parallel);
    }
private slots:
    void flatDisabledAndAlphaWeightedIdentity() {
        auto source=plane();source.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb));
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<float *>(source.scanLine(y))[4*x+3]=(x+y)%3?1.f:.01f;
        const auto original=source.copy();QCOMPARE(filter(source),original);QCOMPARE(filter(source,{-1,2,6}),original);
        QCOMPARE(filter(source,{0,2,6}),original);QCOMPARE(filter(source,{1,0,0}),original);
        for(int y=0;y<source.height();++y)for(int x=0;x<4;++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=9000;p[1]=-7000;p[3]=0;
        }
        QCOMPARE(filter(source),source);QCOMPARE(source.colorSpace(),original.colorSpace());
    }
    void analyticImpulsePositiveNegativeAndAlpha() {
        for(float alpha:{1.f,.25f}) {
            auto source=plane({13,13});auto *center=reinterpret_cast<float *>(source.scanLine(6))+24;
            center[0]=.52f;center[3]=alpha;
            const double sum=std::pow(1+2*std::exp(-.5)+2*std::exp(-2.),2);
            const double neighbor=sum-1;
            for(float amount:{-1.f,-.3f,.7f,1.f}) {
                const auto output=filter(source,{amount,0,2});QVERIFY(!output.isNull());
                const auto *out=reinterpret_cast<const float *>(output.constScanLine(6))+24;
                const double expected=center[0]+.8*amount*(center[0]-.5)*neighbor/(alpha+neighbor)*4*center[0]*(1-center[0]);
                QVERIFY(std::abs(out[0]-expected)<2.e-7);QCOMPARE(out[1],center[1]);QCOMPARE(out[2],center[2]);QCOMPARE(out[3],alpha);
            }
        }
    }
    void irregularAlphaHolesIgnoreHiddenColorExactly() {
        auto source=plane({31,29});
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.37f;
            p[3]=(x*7+y*11)%5?float((x+y)%3+1)/3.f:0.f;
            if(p[3]==0){p[0]=9000;p[1]=-7000;p[2]=5000;}
        }
        const auto first=filter(source);QVERIFY(!first.isNull());auto other=source.copy();
        for(int y=0;y<other.height();++y)for(int x=0;x<other.width();++x) {
            auto *p=reinterpret_cast<float *>(other.scanLine(y))+4*x;if(p[3]==0){p[0]=-8000;p[1]=3000;p[2]=-6000;}
        }
        const auto second=filter(other);QVERIFY(!second.isNull());
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            const auto *a=reinterpret_cast<const float *>(first.constScanLine(y))+4*x;
            const auto *b=reinterpret_cast<const float *>(second.constScanLine(y))+4*x;
            const auto *original=reinterpret_cast<const float *>(source.constScanLine(y))+4*x;
            if(original[3]>0)for(int c=0;c<4;++c){QCOMPARE(a[c],b[c]);QCOMPARE(a[c],original[c]);}
        }
    }
    void arbitraryFractionalNeighborhoodMatchesIndependentDoubleOracle() {
        auto source=plane({13,11});std::mt19937 rng(73131);std::uniform_real_distribution<float> delta(-.2f,.2f);
        const float alphas[]{0,.01f,.5f,1};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]+=delta(rng);p[1]=delta(rng);p[2]=delta(rng);p[3]=alphas[(x+y)%4];
        }
        for(float radius:{.1f,.5f,1.7f,5.5f,6.f})for(float amount:{-.7f,.8f}) {
            const auto output=filter(source,{amount,std::min(2.f,radius/3),radius});QVERIFY(!output.isNull());
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
                const auto *center=reinterpret_cast<const float *>(source.constScanLine(y))+4*x;
                const auto *out=reinterpret_cast<const float *>(output.constScanLine(y))+4*x;
                for(int c=1;c<4;++c)QCOMPARE(out[c],center[c]);
                if(center[3]==0){QCOMPARE(out[0],center[0]);continue;}
                const auto mean=[&](double r) {
                    const int extent=int(std::ceil(r));const double sigma=std::max(.01,.5*r);
                    double sum=0,weighted=0;
                    for(int j=-extent;j<=extent;++j)for(int i=-extent;i<=extent;++i) {
                        const auto *p=reinterpret_cast<const float *>(source.constScanLine(std::clamp(y+j,0,source.height()-1)))+4*std::clamp(x+i,0,source.width()-1);
                        const double weight=std::clamp(r+1-std::abs(i),0.,1.)*std::clamp(r+1-std::abs(j),0.,1.)
                            *std::exp(-double(i*i+j*j)/(2*sigma*sigma))*p[3];
                        sum+=weight;weighted+=weight*p[0];
                    }
                    return weighted/sum;
                };
                const double expected=center[0]+.8*amount*(mean(std::min(2.f,radius/3))-mean(radius))*4*center[0]*(1-center[0]);
                QVERIFY2(std::abs(out[0]-expected)<3.e-7,qPrintable(QString("radius%1 amount%2 x%3 y%4").arg(radius).arg(amount).arg(x).arg(y)));
            }
        }
    }
    void mediumScaleBandRespondsMoreThanFineAndBroadVariation() {
        const auto gain=[&](double period,float amount) {
            auto source=plane({257,7});
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
                reinterpret_cast<float *>(source.scanLine(y))[4*x]=.5f+.02f*std::sin(2*std::acos(-1.)*x/period);
            const auto output=filter(source,{amount,2,6});
            double dot=0,power=0;
            for(int x=24;x<source.width()-24;++x) {
                const double input=reinterpret_cast<const float *>(source.constScanLine(3))[4*x];
                const double delta=reinterpret_cast<const float *>(output.constScanLine(3))[4*x]-input;
                dot+=delta*(input-.5);power+=(input-.5)*(input-.5);
            }
            return dot/power;
        };
        const double medium=gain(8,1),fine=gain(3,1),broad=gain(96,1);
        QVERIFY(medium>.3);QVERIFY(medium>4*fine);QVERIFY(medium>10*broad);
        QVERIFY(std::abs(gain(8,-1)+medium)<2.e-6);
    }
    void signedHdrAndHiddenCentersStayUnclipped() {
        auto source=plane();
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=x%2?-2.f:4.f;p[1]=-3;p[2]=5;
            if(x%3==0)p[3]=std::numeric_limits<float>::denorm_min();
        }
        QCOMPARE(filter(source),source);QCOMPARE(filter(source,{-1,2,6}),source);
    }
    void serialParallelCancellationAndSourceImmutability() {
        auto source=plane({769,389});
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<float *>(source.scanLine(y))[4*x]+=(float((x*17+y*7)%23)-11)*.001f;
        const auto original=source.copy();
        for(float radius:{.5f,3.f,6.f})QCOMPARE(filter(source,{.7f,std::min(2.f,radius/3),radius},true),filter(source,{.7f,std::min(2.f,radius/3),radius},false));
        const auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(SpatialTexture::apply(source,{1,2,6},identity,identity,token).isNull());QCOMPARE(source,original);
    }
    void invalidInputsAndTransformsRefuseCompleteOutput() {
        const auto source=plane();auto invalid=source.copy();reinterpret_cast<float *>(invalid.scanLine(2))[4]=std::numeric_limits<float>::quiet_NaN();
        QVERIFY(filter(invalid).isNull());QCOMPARE(source,plane());
        for(const auto parameters:{SpatialTexture::Parameters{2,2,6},{-2,2,6},{1,-1,6},{1,3,6},{1,2,7},{1,2,1},{1,2,std::numeric_limits<float>::infinity()}})
            QVERIFY(filter(source,parameters).isNull());
        QVERIFY(SpatialTexture::apply(source,{1,2,6},nullptr,identity).isNull());
        const auto bad=[](SpatialTexture::Triple)->SpatialTexture::Triple{return {std::numeric_limits<float>::infinity(),0,0};};
        QVERIFY(SpatialTexture::apply(source,{1,2,6},bad,identity).isNull());
        auto impulse=source.copy();reinterpret_cast<float *>(impulse.scanLine(7))[32]=.6f;
        QVERIFY(SpatialTexture::apply(impulse,{1,2,6},identity,bad).isNull());
        QVERIFY(filter(source.convertToFormat(QImage::Format_RGBA64)).isNull());
    }
};
QTEST_GUILESS_MAIN(SpatialTextureTests)
#include "SpatialTextureTests.moc"
