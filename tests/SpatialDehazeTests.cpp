#include <QtTest>
#include <QElapsedTimer>
#include <limits>
#include <random>
#include "core/pipeline/SpatialDehaze.h"
class SpatialDehazeTests:public QObject {
    Q_OBJECT
    static QImage scene(QSize size={17,19}) {
        QImage image(size,QImage::Format_RGBA32FPx4);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;p[0]=.4f;p[1]=.5f;p[2]=.6f;p[3]=1;}
        return image;
    }
    static SpatialDehaze::Sampler sampler(const QImage &image) {return [&image](int x,int y){const auto *p=reinterpret_cast<const float *>(image.constScanLine(y))+4*x;return SpatialDehaze::Pixel{p[0],p[1],p[2],p[3]};};}
private slots:
    void disabledAndNegativeVeilHaveAnalyticAlphaPreservingResults() {
        const auto image=scene(),before=image.copy();SpatialDehaze::Parameters p;p.airlight={.8f,.9f,1};
        QCOMPARE(SpatialDehaze::apply(image,p),image);
        for(float amount:{-.25f,-1.f}) {
            p.amount=amount;const auto actual=SpatialDehaze::apply(image,p);QVERIFY(!actual.isNull());
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto *a=reinterpret_cast<const float *>(actual.constScanLine(y))+4*x,*b=reinterpret_cast<const float *>(image.constScanLine(y))+4*x;
                for(int c=0;c<3;++c)QVERIFY(std::abs(a[c]-(p.airlight[c]+(b[c]-p.airlight[c])*(1+.65*amount)))<1.e-7);QCOMPARE(a[3],b[3]);}
        }
        QCOMPARE(image,before);
    }
    void constantHazeMatchesRadiometricInverse() {
        const auto image=scene();
        for(float amount:{.25f,.7f,1.f}) {
            SpatialDehaze::Parameters p{amount,2,1,{.8f,.9f,1}};const auto actual=SpatialDehaze::apply(image,p);QVERIFY(!actual.isNull());
            const double transmission=1-amount*.95*.5;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto *a=reinterpret_cast<const float *>(actual.constScanLine(y))+4*x,*b=reinterpret_cast<const float *>(image.constScanLine(y))+4*x;
                for(int c=0;c<3;++c)QVERIFY(std::abs(a[c]-((b[c]-p.airlight[c])/transmission+p.airlight[c]))<2.e-6);QCOMPARE(a[3],1.f);}
        }
    }
    void irregularAlphaHolesNeverSupplyVisibleColor() {
        auto first=scene({31,29});
        for(int y=0;y<first.height();++y)for(int x=0;x<first.width();++x){auto *p=reinterpret_cast<float *>(first.scanLine(y))+4*x;p[3]=(x+y)%5?float((x+y)%3+1)/3:0;p[0]+=.03f*std::sin(float(x));if(p[3]==0){p[0]=9000;p[1]=-7000;p[2]=5000;}}
        auto second=first.copy();for(int y=0;y<second.height();++y)for(int x=0;x<second.width();++x){auto *p=reinterpret_cast<float *>(second.scanLine(y))+4*x;if(p[3]==0){p[0]=-8000;p[1]=3000;p[2]=-6000;}}
        for(float amount:{-1.f,1.f}) {
            const auto a=SpatialDehaze::apply(first,{amount,7,3,{.8f,.9f,1}}),b=SpatialDehaze::apply(second,{amount,7,3,{.8f,.9f,1}});QVERIFY(!a.isNull()&&!b.isNull());
            for(int y=0;y<first.height();++y)for(int x=0;x<first.width();++x){const auto *p=reinterpret_cast<const float *>(a.constScanLine(y))+4*x,*q=reinterpret_cast<const float *>(b.constScanLine(y))+4*x,*o=reinterpret_cast<const float *>(first.constScanLine(y))+4*x;
                if(o[3]>0)for(int c=0;c<4;++c)QCOMPARE(p[c],q[c]);else for(int c=0;c<4;++c)QCOMPARE(p[c],o[c]);}
        }
    }
    void airlightIsBoundedAlphaAwareAndRejectsInvalidSamples() {
        auto image=scene({37,41});
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;p[0]=.8f;p[1]=.9f;p[2]=1;if(x%7==0){p[0]=60000;p[1]=40000;p[2]=50000;p[3]=0;}}
        auto facts=SpatialDehaze::estimateAirlight(image.size(),sampler(image));QVERIFY(facts.valid&&facts.available);QVERIFY(facts.samples>0&&facts.samples<=4096);
        for(int c=0;c<3;++c)QVERIFY(std::abs(facts.rgb[c]-std::array<float,3>{.8f,.9f,1}[c])<1.e-6);
        const auto transparent=[](int,int){return SpatialDehaze::Pixel{60000,-30000,50000,0};};facts=SpatialDehaze::estimateAirlight({31,29},transparent);QVERIFY(facts.valid&&!facts.available);QCOMPARE(facts.samples,0);
        const auto invalid=[](int,int){return SpatialDehaze::Pixel{std::numeric_limits<float>::quiet_NaN(),0,0,1};};QVERIFY(!SpatialDehaze::estimateAirlight({31,29},invalid).valid);
    }
    void twoDimensionalDoubleOracleMatchesIrregularHdrAndFractionalSupport() {
        auto image=scene({23,19});
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            auto *v=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            v[0]=.2f+float((x*17+y*3)%41)/37;v[1]=.1f+float((x*7+y*11)%37)/43;
            v[2]=-.04f+float((x*13+y*5)%31)/29;v[3]=(x+3*y)%7?float((x+y)%5+1)/5:0;
            if(v[3]==0){v[0]=50000;v[1]=-40000;v[2]=30000;}
        }
        const auto before=image.copy();const int w=image.width(),h=image.height();
        const auto at=[&](int x,int y){return reinterpret_cast<const float *>(image.constScanLine(std::clamp(y,0,h-1)))+4*std::clamp(x,0,w-1);};
        for(const auto radii:{std::array<float,2>{0,0},std::array<float,2>{2.3f,1.6f},std::array<float,2>{7,3}}) {
            const SpatialDehaze::Parameters p{.73f,radii[0],radii[1],{.65f,.8f,1.2f}};
            const auto actual=SpatialDehaze::apply(image,p,{},false);QVERIFY(!actual.isNull());
            const int r=int(std::ceil(double(p.darkRadius))),f=int(std::ceil(double(p.refineRadius)));
            const auto guide=[&](const float *v){return std::clamp((double(v[0])/p.airlight[0]+double(v[1])/p.airlight[1]+double(v[2])/p.airlight[2])/3,0.,4.);};
            const auto transmission=[&](int x,int y) {
                double dark=std::numeric_limits<double>::infinity();
                for(int j=-r;j<=r;++j)for(int i=-r;i<=r;++i) {
                    const auto *v=at(x+i,y+j);if(v[3]<=1.e-20f)continue;
                    dark=std::min(dark,std::clamp(std::min({double(v[0])/p.airlight[0],double(v[1])/p.airlight[1],double(v[2])/p.airlight[2]}),0.,1.));
                }
                return std::isfinite(dark)?std::clamp(1-.95*dark,.1,1.):1.;
            };
            double maxError=0;const double sigma=std::max(.01,.5*double(p.refineRadius));
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const auto *v=at(x,y),*a=reinterpret_cast<const float *>(actual.constScanLine(y))+4*x;
                QCOMPARE(a[3],v[3]);if(v[3]<=1.e-20f){for(int c=0;c<4;++c)QCOMPARE(a[c],v[c]);continue;}
                double sum=0,weighted=0;
                for(int j=-f;j<=f;++j)for(int i=-f;i<=f;++i) {
                    const int xx=std::clamp(x+i,0,w-1),yy=std::clamp(y+j,0,h-1);const auto *q=at(xx,yy);if(q[3]<=1.e-20f)continue;
                    const double range=guide(q)-guide(v);
                    const double wi=std::clamp(double(p.refineRadius)+1-std::abs(i),0.,1.)*std::exp(-double(i*i)/(2*sigma*sigma));
                    const double wj=std::clamp(double(p.refineRadius)+1-std::abs(j),0.,1.)*std::exp(-double(j*j)/(2*sigma*sigma));
                    const double weight=wi*wj*std::exp(-range*range/.02)*q[3];sum+=weight;weighted+=weight*transmission(xx,yy);
                }
                const double t=1-double(p.amount)*(1-(sum>1.e-20?std::clamp(weighted/sum,.1,1.):transmission(x,y)));
                for(int c=0;c<3;++c)maxError=std::max(maxError,std::abs(double(a[c])-((double(v[c])-p.airlight[c])/t+p.airlight[c])));
            }
            QVERIFY2(maxError<2.e-5,qPrintable(QString("double 2D oracle maximum %1").arg(maxError,0,'g',12)));
        }
        QCOMPARE(image,before);
    }
    void parallelHaloAndInvalidInputsPreserveTheSourceContract() {
        auto image=scene({521,509});
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            auto *v=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            v[0]=.35f+.1f*std::sin(x*.07f);v[1]=.45f+.08f*std::cos(y*.09f);v[2]=.55f+.04f*std::sin((x+y)*.11f);
            v[3]=(x+2*y)%13?.63f:0;
        }
        const auto before=image.copy();
        for(float amount:{-1.f,.8f}) {
            const SpatialDehaze::Parameters p{amount,2.3f,1.6f,{.8f,.9f,1}};
            const auto serial=SpatialDehaze::apply(image,p,{},false),parallel=SpatialDehaze::apply(image,p,{},true);
            QVERIFY(!serial.isNull());QCOMPARE(parallel,serial);
            const QRect roi(179,137,91,83);const int halo=SpatialDehaze::halo(p);
            const auto expanded=roi.adjusted(-halo,-halo,halo,halo).intersected(image.rect());
            const auto cropped=SpatialDehaze::apply(image.copy(expanded),p);
            QCOMPARE(cropped.copy(roi.translated(-expanded.topLeft())),serial.copy(roi));
        }
        auto token=std::make_shared<std::atomic_bool>(true);QVERIFY(SpatialDehaze::apply(image,{1,7,3,{1,1,1}},token).isNull());
        for(auto p:{SpatialDehaze::Parameters{1.1f,7,3,{1,1,1}},SpatialDehaze::Parameters{1,7.1f,3,{1,1,1}},SpatialDehaze::Parameters{1,7,3,{0,1,1}}}) {
            QVERIFY(!SpatialDehaze::valid(p));QVERIFY(SpatialDehaze::apply(image,p).isNull());
        }
        auto invalid=image.copy();reinterpret_cast<float *>(invalid.scanLine(7))[19*4+1]=std::numeric_limits<float>::infinity();
        QVERIFY(SpatialDehaze::apply(invalid,{1,7,3,{1,1,1}}).isNull());
        QCOMPARE(image,before);
    }
    void airlightWorkIsIndependentOfFullFramePixelCount() {
        int reads=0;QElapsedTimer timer;timer.start();
        const auto facts=SpatialDehaze::estimateAirlight({10000,8000},[&](int x,int y){++reads;return SpatialDehaze::Pixel{.1f+.7f*x/9999.f,.2f+.5f*y/7999.f,.4f,1};});
        QVERIFY(facts.valid&&facts.available);QCOMPARE(facts.samples,4096);QVERIFY(reads<=4096*40);
        qInfo()<<"full-frame airlight sampled pixels"<<reads<<"for80MP, ms"<<timer.nsecsElapsed()/1.e6;
        const auto token=std::make_shared<std::atomic_bool>(false);int cancelledReads=0;
        const auto cancelled=SpatialDehaze::estimateAirlight({10000,8000},[&](int,int){if(++cancelledReads==129)token->store(true);return SpatialDehaze::Pixel{.8f,.9f,1,1};},token);
        QVERIFY(!cancelled.valid);QVERIFY(cancelledReads>=129&&cancelledReads<reads);
    }
};
QTEST_GUILESS_MAIN(SpatialDehazeTests)
#include "SpatialDehazeTests.moc"
