#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRgba64>
#include <limits>
#include <numbers>
#include "core/pipeline/GeometryState.h"
#include "tests/tangential_oracle/LegacyGeometryState.h"
#include "tests/GeometryProgressProbe.h"
namespace {
QByteArray pixels(const QImage &im) {
    QByteArray out; const int bytes=im.width()*im.depth()/8;
    for(int y=0;y<im.height();++y) out.append(reinterpret_cast<const char *>(im.constScanLine(y)),bytes);
    return out;
}
QImage image(QSize size,bool floating=false,bool mixed=false) {
    QImage im(size,floating?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
    im.setColorSpace(QColorSpace::SRgbLinear);im.setDevicePixelRatio(2);im.setDotsPerMeterX(4321);im.setDotsPerMeterY(3456);im.setText("Original","unmodified");
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x) {
        const int phase=(x+3*y)%4;const quint16 alphas[]{0,1,23456,65535};
        if(floating) {auto *p=reinterpret_cast<float *>(im.scanLine(y))+4*x;
            p[0]=float(2+.003*x+.002*y);p[1]=float(.1+.002*x+.004*y);p[2]=float(-.75+.0003*x-.0002*y);p[3]=mixed?alphas[phase]/65535.0f:1.0f;
        }else reinterpret_cast<QRgba64 *>(im.scanLine(y))[x]=QRgba64::fromRgba64(1001+31*x+17*y,2003+12*x+57*y,3007+13*x+11*y,mixed?alphas[phase]:65535);
    }return im;
}
struct Position {long double x,y,u,v;};
Position position(const GeometryState &g,QSize src,QSize dst,int x,int y,long double ca=1) {
    const long double cx=(src.width()-1)/2.0L,cy=(src.height()-1)/2.0L;
    const long double a=static_cast<long double>(g.straighten)*std::numbers::pi_v<long double>/180;
    const long double dx=x-(dst.width()-1)/2.0L,dy=y-(dst.height()-1)/2.0L;
    const long double xx=cosl(a)*dx+sinl(a)*dy,yy=-sinl(a)*dx+cosl(a)*dy;
    const long double den=1+g.perspectiveHorizontal*xx/std::max(.5L,cx)+g.perspectiveVertical*yy/std::max(.5L,cy);
    const long double px=xx/den,py=yy/den,r=hypotl(cx,cy),u=r>0?px/r:0,v=r>0?py/r:0;
    const long double radial=1+g.distortion*(u*u+v*v);
    const long double tx=r*(2*g.tangentialP1*u*v+g.tangentialP2*(3*u*u+v*v));
    const long double ty=r*(g.tangentialP1*(u*u+3*v*v)+2*g.tangentialP2*u*v);
    return {cx+ca*(px*radial+tx),cy+ca*(py*radial+ty),u,v};
}
struct Sample {long double r=0,g=0,b=0,a=0;};
Sample reference(const QImage &im,long double x,long double y) {
    x=std::clamp(x,0.0L,static_cast<long double>(im.width()-1));y=std::clamp(y,0.0L,static_cast<long double>(im.height()-1));
    const int left=int(floorl(x)),top=int(floorl(y)),right=std::min(left+1,im.width()-1),bottom=std::min(top+1,im.height()-1);
    Sample out;const long double fx=x-left,fy=y-top;
    for(int row=0;row<2;++row)for(int col=0;col<2;++col) {
        const long double w=(col?fx:1-fx)*(row?fy:1-fy);const int xx=col?right:left,yy=row?bottom:top;
        Sample s;
        if(im.format()==QImage::Format_RGBA32FPx4) {const auto *p=reinterpret_cast<const float *>(im.constScanLine(yy))+4*xx;s={p[0],p[1],p[2],p[3]};}
        else {const auto p=reinterpret_cast<const QRgba64 *>(im.constScanLine(yy))[xx];s={static_cast<long double>(p.red()),static_cast<long double>(p.green()),static_cast<long double>(p.blue()),static_cast<long double>(p.alpha())};}
        out.a+=w*s.a;out.r+=w*s.a*s.r;out.g+=w*s.a*s.g;out.b+=w*s.a*s.b;
    }
    if(out.a>0){out.r/=out.a;out.g/=out.a;out.b/=out.a;}else out.r=out.g=out.b=0;
    return out;
}
quint16 rounded(long double x) {return quint16(std::clamp(llroundl(x),0LL,65535LL));}
}
class TangentialGeometryTests final:public QObject {
    Q_OBJECT
    QJsonObject record{{"normally_registered_component",true},{"product_complete",false},{"Root_or_GUI_or_RAW_GPU_acceptance",false}};
    int done=0;
    void save() {QSaveFile f(qEnvironmentVariable("JIXEL_TANGENTIAL_EVIDENCE_PATH"));if(!f.open(QIODevice::WriteOnly))qFatal("owned result write");f.write(QJsonDocument(record).toJson());if(!f.commit())qFatal("owned result commit");}
private slots:
    void initTestCase(){QVERIFY(!qEnvironmentVariable("JIXEL_TANGENTIAL_EVIDENCE_PATH").isEmpty());record["started"]=true;save();}
    void legacyZeroAndCanonicalSchemasStayExact() {
        qint64 count=0,total=0;
        for(QSize sz:{QSize(101,61),QSize(61,101),QSize(1,9),QSize(9,1),QSize(1,1)})
        for(int profile=0;profile<5;++profile)for(bool floating:{false,true}) {
            GeometryState g;if(profile==1)g.straighten=13;if(profile>=2){g.distortion=profile==2?-.3:.3;g.perspectiveHorizontal=.4;g.perspectiveVertical=-.4;g.redCa=2;g.blueCa=-2;g.straighten=profile==4?-45:17;}
            if(profile==3){g.crop={.1,.2,.7,.6};g.quarterTurns=1;g.flipVertical=true;}
            auto old=LegacyGeometryState::fromJson(g.toJson());QCOMPARE(g.toJson(),old.toJson());QVERIFY(!g.toJson().contains("tangentialP1"));
            const auto src=image(sz,floating,true);const auto a=g.apply(src),b=old.apply(src);
            QVERIFY(!a.isNull());QCOMPARE(a.size(),b.size());QCOMPARE(a.format(),b.format());QCOMPARE(pixels(a),pixels(b));QCOMPARE(g.correctedSize(sz),old.correctedSize(sz));++count;total+=qint64(a.width())*a.height();
        }
        record["zero_legacy_exact_profiles"]=count;record["zero_legacy_exact_pixels"]=total;++done;save();
    }
    void newCanonicalSchemaRequiresTypedActiveCoefficients() {
        qint64 accepted=0,refused=0;
        for(double a:{-.02,0.,.02})for(double b:{-.02,0.,.02})if(a||b) {
            GeometryState g;g.tangentialP1=a;g.tangentialP2=b;g.straighten=4;g.distortion=-.3;g.redCa=2;g.crop={.1,.2,.6,.7};
            const auto j=g.toJson();QCOMPARE(j["schema"].toInt(),4);QVERIFY(GeometryState::validJson(j));QCOMPARE(GeometryState::fromJson(j).toJson(),j);++accepted;
            for(const auto *key:{"tangentialP1","tangentialP2"}) {
                auto bad=j;bad.remove(key);QVERIFY(!GeometryState::validJson(bad));++refused;
                for(QJsonValue v:{QJsonValue(-.020001),QJsonValue(.020001),QJsonValue("0.01"),QJsonValue(true),QJsonValue()}){bad=j;bad[key]=v;QVERIFY(!GeometryState::validJson(bad));++refused;}
            }
            auto bad=j;bad["tangentialP1"]=0;bad["tangentialP2"]=0;QVERIFY(!GeometryState::validJson(bad));++refused;
            bad=j;bad["schema"]=3;QVERIFY(!GeometryState::validJson(bad));++refused;
        }
        GeometryState zero;zero.distortion=.1;zero.tangentialP1=-0.0;QCOMPARE(zero.toJson()["schema"].toInt(),3);
        for(double v:{-.020001,.020001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){GeometryState g;g.tangentialP1=v;QVERIFY(!g.validCorrections());QVERIFY(g.apply(image({9,7})).isNull());++refused;}
        record["canonical4_accepted"]=accepted;record["canonical4_refusals"]=refused;++done;save();
    }
    void integerWholePixelsMatchIndependentLongDoubleCoordinates() {
        qint64 profiles=0,total=0;int maximum=0;
        for(QSize size:{QSize(101,61),QSize(61,101)})for(auto coeff:{QPointF(.02,0),QPointF(-.02,0),QPointF(0,.02),QPointF(0,-.02),QPointF(.02,-.02),QPointF(-.02,.02)})for(int p=0;p<3;++p){
            GeometryState g;g.tangentialP1=coeff.x();g.tangentialP2=coeff.y();if(p){g.perspectiveHorizontal=p==1?.4:-.4;g.perspectiveVertical=p==1?-.4:.4;g.distortion=p==1?-.3:.3;g.straighten=p==1?31:-45;g.redCa=2;g.blueCa=-2;}
            const auto src=image(size,false,true);const auto before=pixels(src);const auto out=g.apply(src);QVERIFY(!out.isNull());QCOMPARE(out.size(),g.correctedSize(size));
            for(int y=0;y<out.height();++y)for(int x=0;x<out.width();++x){auto gp=position(g,size,out.size(),x,y),rp=position(g,size,out.size(),x,y,1+g.redCa/100.0L),bp=position(g,size,out.size(),x,y,1+g.blueCa/100.0L);const auto green=reference(src,gp.x,gp.y),red=reference(src,rp.x,rp.y),blue=reference(src,bp.x,bp.y);const auto actual=reinterpret_cast<const QRgba64 *>(out.constScanLine(y))[x];const QRgba64 expected=green.a>0?QRgba64::fromRgba64(rounded(red.r),rounded(green.g),rounded(blue.b),rounded(green.a)):QRgba64::fromRgba64(0,0,0,0);
                const int err=std::max({abs(int(actual.red())-expected.red()),abs(int(actual.green())-expected.green()),abs(int(actual.blue())-expected.blue()),abs(int(actual.alpha())-expected.alpha())});maximum=std::max(maximum,err);QVERIFY(err<=1);++total;}
            QCOMPARE(pixels(src),before);++profiles;
        }
        record["new_integer_profiles"]=profiles;record["new_integer_pixels"]=total;record["independent_long_double_integer_max_error"]=maximum;++done;save();
    }
    void signedFloatAssociatedAlphaAndMetadataStayUnclipped() {
        qint64 profiles=0,total=0;double maxError=0;bool negative=false,headroom=false;
        for(QSize size:{QSize(101,61),QSize(61,101)})for(auto coeff:{QPointF(.02,.02),QPointF(-.02,-.02),QPointF(.01,-.02),QPointF(-.02,.01)}){
            GeometryState g;g.tangentialP1=coeff.x();g.tangentialP2=coeff.y();g.perspectiveHorizontal=.32;g.perspectiveVertical=-.27;g.distortion=-.25;g.redCa=-2;g.blueCa=2;g.straighten=-17;
            const auto src=image(size,true,true),out=g.apply(src);QVERIFY(!out.isNull());QCOMPARE(out.format(),QImage::Format_RGBA32FPx4);QCOMPARE(out.colorSpace(),src.colorSpace());QCOMPARE(out.devicePixelRatio(),src.devicePixelRatio());QCOMPARE(out.dotsPerMeterX(),src.dotsPerMeterX());QCOMPARE(out.dotsPerMeterY(),src.dotsPerMeterY());QCOMPARE(out.text("Original"),src.text("Original"));
            for(int y=0;y<out.height();++y)for(int x=0;x<out.width();++x){auto gp=position(g,size,out.size(),x,y),rp=position(g,size,out.size(),x,y,.98L),bp=position(g,size,out.size(),x,y,1.02L);const auto green=reference(src,gp.x,gp.y),red=reference(src,rp.x,rp.y),blue=reference(src,bp.x,bp.y);const long double expected[]{green.a>0?red.r:0,green.a>0?green.g:0,green.a>0?blue.b:0,green.a};const auto *a=reinterpret_cast<const float *>(out.constScanLine(y))+4*x;for(int c=0;c<4;++c){maxError=std::max(maxError,double(fabsl(static_cast<long double>(a[c])-expected[c])));QVERIFY(maxError<2e-6);}negative|=a[2]<0;headroom|=a[0]>1;++total;}
            ++profiles;
        }
        QVERIFY(negative&&headroom);record["float_profiles"]=profiles;record["float_pixels"]=total;record["float_max_absolute_error"]=maxError;record["signed_and_headroom_retained"]=true;++done;save();
    }
    void completeDomainBoundsRemainInsideAndLocallyNonfolding() {
        qint64 profiles=0,points=0,pix=0;double minimum=1;const auto color=QRgba64::fromRgba64(12345,23456,34567,65535);
        for(QSize size:{QSize(101,61),QSize(61,101),QSize(1,9),QSize(9,1),QSize(1,1)})for(auto coeff:{QPointF(.02,0),QPointF(-.02,0),QPointF(0,.02),QPointF(0,-.02),QPointF(.02,.02),QPointF(-.02,-.02)})for(double h:{-.4,0.,.4})for(double v:{-.4,0.,.4})for(double k:{-.3,.3})for(double angle:{-45.,0.,45.}){
            GeometryState g;g.tangentialP1=coeff.x();g.tangentialP2=coeff.y();g.perspectiveHorizontal=h;g.perspectiveVertical=v;g.distortion=k;g.straighten=angle;g.redCa=2;g.blueCa=-2;
            QImage src(size,QImage::Format_RGBA64);src.fill(QColor::fromRgba64(color));const auto out=g.apply(src);QVERIFY(!out.isNull());QVERIFY(out.width()<=size.width()&&out.height()<=size.height());
            for(int y=0;y<out.height();++y)for(int x=0;x<out.width();++x){QCOMPARE(reinterpret_cast<const QRgba64 *>(out.constScanLine(y))[x],color);++pix;}
            for(int iy=0;iy<9;++iy)for(int ix=0;ix<9;++ix){const int x=(out.width()-1)*ix/8,y=(out.height()-1)*iy/8;const auto q=position(g,size,out.size(),x,y);for(long double ca:{.98L,1.L,1.02L}){const auto a=position(g,size,out.size(),x,y,ca);QVERIFY(a.x>=-1e-7L&&a.x<=size.width()-1+1e-7L&&a.y>=-1e-7L&&a.y<=size.height()-1+1e-7L);}const long double u=q.u,w=q.v,p1=g.tangentialP1,p2=g.tangentialP2;
                const long double a=1+k*(3*u*u+w*w)+2*p1*w+6*p2*u,d=1+k*(u*u+3*w*w)+6*p1*w+2*p2*u,b=2*k*u*w+2*p1*u+2*p2*w;const long double eigen=(a+d-sqrtl((a-d)*(a-d)+4*b*b))/2;QVERIFY(eigen>0);minimum=std::min(minimum,double(eigen));++points;}
            ++profiles;
        }
        record["whole_domain_profiles"]=profiles;record["whole_domain_sampled_points"]=points;record["opaque_exact_pixels"]=pix;record["minimum_sampled_lens_eigenvalue"]=minimum;++done;save();
    }
    void parallelOwnershipCancellationAndOrientedCrop() {
        GeometryState g;g.tangentialP1=.01;g.tangentialP2=-.012;g.distortion=.1;const auto src=image({900,700},true,true);const auto before=pixels(src);qputenv("JIXELLIGHT_CPU_THREADS","1");const auto serial=g.apply(src);qputenv("JIXELLIGHT_CPU_THREADS","8");const auto parallel=g.apply(src);QVERIFY(qint64(parallel.width())*parallel.height()>=262144);QCOMPARE(pixels(parallel),pixels(serial));QCOMPARE(pixels(src),before);
        qputenv("JIXELLIGHT_CPU_THREADS","1");auto token=std::make_shared<std::atomic_bool>(true);QVERIFY(g.apply(src,token).isNull());token->store(false);int rows=0;GeometryProgressProbe::rowCompleted=[&](int y){++rows;if(y==3)token->store(true);};const auto cancelled=g.apply(src,token);GeometryProgressProbe::rowCompleted={};QVERIFY(cancelled.isNull());QVERIFY(rows==4);QCOMPARE(pixels(src),before);
        GeometryState crop=g;crop.crop={.1,.2,.6,.5};crop.quarterTurns=1;crop.flipHorizontal=true;const auto result=crop.apply(src);
        const int left=int(floor(.1*serial.width())),top=int(floor(.2*serial.height())),right=int(ceil(.7*serial.width())),bottom=int(ceil(.7*serial.height()));const auto expected=serial.copy(left,top,right-left,bottom-top).transformed(QTransform().rotate(90),Qt::FastTransformation).mirrored(true,false);QCOMPARE(pixels(result),pixels(expected));QCOMPARE(result.size(),expected.size());
        record["serial_parallel_exact_pixels"]=qint64(parallel.width())*parallel.height();record["cancelled_after_actual_rows"]=rows;record["oriented_crop_exact_pixels"]=qint64(result.width())*result.height();++done;save();
    }
    void cleanupTestCase(){record["completed_new_methods"]=done;record["passed"]=done==6;record["physical_or_full_RAW_or_new_shipping_claim"]=false;save();QCOMPARE(done,6);}
};
QTEST_GUILESS_MAIN(TangentialGeometryTests)
#include "TangentialGeometryTests.moc"
