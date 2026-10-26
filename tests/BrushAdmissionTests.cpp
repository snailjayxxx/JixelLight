#include <QtTest>
#include <QColorSpace>
#include <QSemaphore>
#include <QRgba64>
#include <QFile>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>
#include <cstring>
#include <limits>
#include <vector>
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/export/JpegExporter.h"
#include "core/export/RasterExporter.h"
#include "core/scopes/ScopesEngine.h"
#include "core/look/LookLut.h"

namespace {
QImage scene(int width=341,int height=259,bool floating=true) {
    QImage source(width,height,floating?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
    source.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        if(floating) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=float(x%37)/13.f-.21f;p[1]=float(y%43)/17.f;p[2]=float((x+y)%29)/19.f-.08f;p[3]=(x+y)%7?.71f:0.f;
        }else reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*101+y*23)%65536,(x*29+y*307)%65536,(x*71+y*509)%65536,(x+y)%7?65535:0);
    }
    return source;
}
AdjustmentState edits(int dabs=7) {
    AdjustmentState state;state.exposure=-.65;state.saturation=13;
    BrushMaskGeometry::Stroke stroke;stroke.style={.12,.43,.71};
    for(int i=0;i<dabs;++i)stroke.dabs.push_back({{.2+i*.07,.31+i*.035},.6+(i%3)*.1});
    state.brushMasks.layers.push_back({"paint",true,.85,{stroke}});
    return state;
}
QImage oracle(const QImage &source,const AdjustmentState &state,const BrushRenderRequest::Spec &spec) {
    auto plan=ProcessingPlan::compile(state,spec.encoding,spec.output,spec.rawSource,spec.rawBaseExposureStops);plan.setFrameRect(spec.normalizedFrame);
    const auto input=spec.sourceStage==BrushRenderRequest::SourceStage::Original?state.geometry.apply(source):source;
    const auto rendered=ImagePipeline::processWithPlan(input,plan,{},false);
    return spec.region.isNull()?rendered:rendered.copy(spec.region);
}
QByteArray fileBytes(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
}
class BrushAdmissionTests final:public QObject {
    Q_OBJECT
private slots:
    void preparedPlanPreservesOriginalLookAndAppliesGeometryOnce() {
        const auto source=scene(317,239);auto state=edits();state.geometry.straighten=1.2;
        state.geometry.crop={.03,.02,.92,.95};state.geometry.quarterTurns=1;
        state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"contrast",2}};
        const auto prepared=state.geometry.apply(source);QVERIFY(!prepared.isNull());
        auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::DisplayP3,true,.35f);
        plan.setFrameRect({.13,.21,.71,.63});const auto expected=ImagePipeline::processWithPlan(prepared,plan);
        auto twice=ProcessingPlan::compile(plan.state,plan.encoding,plan.output,plan.rawSource,plan.baseExposureStops);
        twice.setFrameRect({.13,.21,.71,.63});QVERIFY(ImagePipeline::processWithPlan(prepared,twice)!=expected);
        state.exposure=4;state.geometry={};state.brushMasks.layers.clear();
        QString error;auto request=BrushRenderRequest::createPrepared(prepared,plan,BrushRenderRequest::Layout::Whole,{},&error);
        QVERIFY2(request,qPrintable(error));const auto actual=request->render({},&error);QVERIFY2(actual,qPrintable(error));QCOMPARE(actual.image(),expected);
        auto stream=BrushRenderRequest::createPrepared(prepared,plan,BrushRenderRequest::Layout::Stripes128,{},&error);QVERIFY2(stream,qPrintable(error));
        int rows=0;QVERIFY(stream->visitStrips([&](const auto &lease,int y){
            if(y!=rows||lease.image()!=expected.copy(0,y,expected.width(),lease.image().height()))return false;
            rows+=lease.image().height();return true;
        },{},&error));QCOMPARE(rows,expected.height());
    }
    void changedOrManuallyAssembledPreparedPlansRefuseAndRecover() {
        const auto source=scene(79,53);auto state=edits();state.look.mode="manual";state.look.code="FL";
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,.2f);
        auto budget=std::make_shared<BrushRenderBudget>();QString error;
        for(int changed=0;changed<9;++changed) {
            auto bad=plan;
            if(changed==0)bad.data[ProcessingPlan::Tonal].x+=.1f;
            if(changed==1)bad.state.hue+=1e-12;
            if(changed==2)bad.state.masterCurve[2]+=.01;
            if(changed==3)bad.state.brushMasks.layers[0].exposureEv+=.01;
            if(changed==4)bad.state.look.lut=LookLut::identity(2);
            if(changed==5)bad.state.geometry.quarterTurns=1;
            if(changed==6)bad.state.look.error="invalid";
            if(changed==7)bad.state.look.parameters["clarity"]=1;
            if(changed==8){ProcessingPlan manual;manual.data=plan.data;manual.state=plan.state;manual.encoding=plan.encoding;manual.rawSource=plan.rawSource;manual.baseExposureStops=plan.baseExposureStops;bad=manual;}
            QVERIFY(!BrushRenderRequest::createPrepared(source,bad,BrushRenderRequest::Layout::Whole,{},&error,budget));
            QVERIFY(!error.isEmpty());QCOMPARE(budget->snapshot().charged,0);
        }
        auto runtime=plan;runtime.data[ProcessingPlan::Dimensions]={79,53,1,0};runtime.data[ProcessingPlan::PixelMap]=runtime.pixelMap(source.size());
        auto request=BrushRenderRequest::createPrepared(source,runtime,BrushRenderRequest::Layout::Whole,{},&error,budget);QVERIFY2(request,qPrintable(error));
        QCOMPARE(request->render({},&error).image(),ImagePipeline::processWithPlan(source,plan));request.reset();QCOMPARE(budget->snapshot().charged,0);
    }
    void frozenSourceStateGeometryAndViewportMatchExistingOracle() {
        auto source=scene();auto state=edits();state.geometry.straighten=.8;state.geometry.crop={.02,.03,.95,.91};state.geometry.quarterTurns=1;
        state.geometry.distortion=.03;state.geometry.perspectiveHorizontal=.04;state.geometry.redCa=.2;state.geometry.blueCa=-.1;
        state.look.mode="manual";state.look.code="FL";state.look.parameters={{"sharpness",4},{"clarity",3}};
        BrushRenderRequest::Spec spec;spec.output=ColorManagement::OutputSpace::DisplayP3;spec.normalizedFrame={.13,.07,.71,.81};
        const auto expected=oracle(source,state,spec);QVERIFY(!expected.isNull());QString error;
        auto request=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(request,qPrintable(error));
        source.fill(Qt::black);state.exposure=5;state.geometry={};state.brushMasks.layers[0].exposureEv=-3;spec.normalizedFrame={0,0,1,1};
        const auto lease=request->render({},&error);QVERIFY2(lease,qPrintable(error));QCOMPARE(lease.image(),expected);
        QVERIFY(!request->render({},&error));QVERIFY(error.contains("already"));
        const auto fixture=scene(83,61);std::vector<float> external(std::size_t(83)*61*4);
        for(int y=0;y<61;++y)std::memcpy(external.data()+std::size_t(y)*83*4,fixture.constScanLine(y),83*16);
        QImage borrowed(reinterpret_cast<uchar *>(external.data()),83,61,83*16,QImage::Format_RGBA32FPx4);
        borrowed.setColorSpace(fixture.colorSpace());const auto stableState=edits();BrushRenderRequest::Spec stableSpec;
        const auto independent=oracle(borrowed,stableState,stableSpec);
        auto owned=BrushRenderRequest::create(borrowed,stableState,stableSpec,{},&error);QVERIFY2(owned,qPrintable(error));
        std::fill(external.begin(),external.end(),0.f);const auto frozen=owned->render({},&error);QVERIFY2(frozen,qPrintable(error));QCOMPARE(frozen.image(),independent);
    }
    void exactBudgetBoundaryAndEvictedOutputLeaseRemainCharged() {
        const auto source=scene(83,61);const auto state=edits();BrushRenderRequest::Spec spec;QString error;
        auto measurement=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(measurement,qPrintable(error));
        const auto needed=measurement->reservedBytes();measurement.reset();
        auto shortBudget=std::make_shared<BrushRenderBudget>(needed-1);
        QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error,shortBudget));QCOMPARE(shortBudget->snapshot().charged,0);QCOMPARE(shortBudget->snapshot().peak,0);
        auto budget=std::make_shared<BrushRenderBudget>(needed);
        auto request=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));QCOMPARE(budget->snapshot().charged,needed);
        QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error,budget));QCOMPARE(budget->snapshot().charged,needed);
        auto lease=request->render({},&error);QVERIFY2(lease,qPrintable(error));auto borrowed=lease;
        request.reset();QCOMPARE(budget->snapshot().charged,needed);QVERIFY(!borrowed.image().isNull());
        lease={};QCOMPARE(budget->snapshot().charged,needed);borrowed={};QCOMPARE(budget->snapshot().charged,0);QCOMPARE(budget->snapshot().peak,needed);
        auto recovered=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(recovered,qPrintable(error));recovered.reset();QCOMPARE(budget->snapshot().charged,0);
    }
    void streamedUnevenRoiAndRetainedStripsMatchFullDetailPixels() {
        const auto source=scene(643,401);auto state=edits();state.look.mode="manual";state.look.code="ST";state.look.parameters={{"sharpness",5},{"clarity",4}};
        BrushRenderRequest::Spec spec;spec.layout=BrushRenderRequest::Layout::Stripes128;spec.region={13,19,567,337};spec.normalizedFrame={.17,.11,.69,.75};
        const auto expected=oracle(source,state,spec);QVERIFY(!expected.isNull());QString error;
        auto budget=std::make_shared<BrushRenderBudget>();auto request=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));
        QVERIFY(!request->render({},&error));QVERIFY(error.contains("layout"));
        std::vector<BrushImageLease> retained;int rows=0;
        QVERIFY2(request->visitStrips([&](const BrushImageLease &lease,int y){if(y!=rows)return false;rows+=lease.image().height();retained.push_back(lease);return true;},{},&error),qPrintable(error));
        QCOMPARE(rows,337);QCOMPARE(retained.size(),std::size_t(3));QCOMPARE(retained.back().image().height(),81);
        QImage assembled(expected.size(),QImage::Format_RGBA64);assembled.setColorSpace(expected.colorSpace());int y=0;
        for(const auto &lease:retained)for(int row=0;row<lease.image().height();++row,++y)std::memcpy(assembled.scanLine(y),lease.image().constScanLine(row),std::size_t(assembled.width())*8);
        QCOMPARE(assembled,expected);const auto charge=request->reservedBytes();request.reset();QCOMPARE(budget->snapshot().charged,charge);
        retained.clear();QCOMPARE(budget->snapshot().charged,0);
    }
    void preparedInputNeverAppliesStoredGeometryTwice() {
        const auto original=scene();auto state=edits();state.geometry.quarterTurns=3;state.geometry.crop={.1,.03,.8,.93};state.geometry.flipHorizontal=true;
        const auto prepared=state.geometry.apply(original);BrushRenderRequest::Spec spec;spec.sourceStage=BrushRenderRequest::SourceStage::Prepared;
        spec.normalizedFrame={.08,.12,.81,.73};const auto expected=oracle(prepared,state,spec);QString error;
        auto request=BrushRenderRequest::create(prepared,state,spec,{},&error);QVERIFY2(request,qPrintable(error));QCOMPARE(request->size(),prepared.size());
        const auto output=request->render({},&error);QVERIFY2(output,qPrintable(error));QCOMPARE(output.image(),expected);
    }
    void originalLimitsAndWholeOperationHaloBudgetsStayIntact() {
        QImage source(1024,1024,QImage::Format_RGBA64);source.fill(QColor(70,90,110));auto state=edits(1);
        auto &stroke=state.brushMasks.layers[0].strokes[0];stroke.style.radius=.5;stroke.dabs.assign(96,{{.5,.5},1});
        BrushRenderRequest::Spec spec;QString error;
        auto whole=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(whole,qPrintable(error));whole.reset();
        spec.layout=BrushRenderRequest::Layout::Stripes128;auto streamed=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(streamed,qPrintable(error));streamed.reset();
        state.look.mode="manual";state.look.code="ST";state.look.parameters={{"sharpness",5},{"clarity",4}};
        QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error));QVERIFY(error.contains("work budget"));
        state.look={};stroke.dabs.push_back({{.5,.5},1});spec.layout=BrushRenderRequest::Layout::Whole;
        QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error));QVERIFY(error.contains("work budget"));
        QImage tooLargeForResearch(2049,1024,QImage::Format_RGBA64);const auto ordinary=edits();const auto plan=ProcessingPlan::compile(ordinary,ImagePipeline::InputEncoding::LinearProPhoto);
        QVERIFY(!ImagePipeline::preflightTiledResearchRequest(tooLargeForResearch.size(),plan,128,{},&error));
        QVERIFY(ImagePipeline::processWithPlan(tooLargeForResearch,plan).isNull());
        auto admitted=BrushRenderRequest::create(tooLargeForResearch,ordinary,spec,{},&error);QVERIFY2(admitted,qPrintable(error));
    }
    void sourcePixelBoundaryAndFullFp32ValidationPrecedeGeometry() {
        BrushRenderRequest::Spec spec;const auto state=edits();QString error;
        {QImage source(4096,8192,QImage::Format_RGBA64);QVERIFY(!source.isNull());source.fill(QColor(70,90,110));auto request=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(request,qPrintable(error));}
        {QImage source(4097,8192,QImage::Format_RGBA64);QVERIFY(!source.isNull());QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error));QVERIFY(error.contains("32 Mi"));}
        auto source=scene(29,17);auto cropped=state;cropped.geometry.crop={0,0,.3,.3};
        auto *outside=reinterpret_cast<float *>(source.scanLine(16))+4*28;outside[0]=std::numeric_limits<float>::quiet_NaN();
        auto budget=std::make_shared<BrushRenderBudget>();QVERIFY(!BrushRenderRequest::create(source,cropped,spec,{},&error,budget));QCOMPARE(budget->snapshot().charged,0);
        QImage unsupported(29,17,QImage::Format_ARGB32);QVERIFY(!BrushRenderRequest::create(unsupported,state,spec,{},&error));
        for(const auto &region:{QRect(1,1,0,0),QRect(-1,0,2,2),QRect(0,0,30,18)}) {
            spec.region=region;QVERIFY(!BrushRenderRequest::create(scene(29,17),state,spec,{},&error));QVERIFY(error.contains("region"));
        }
        spec.region={};
        auto cancelledToken=std::make_shared<std::atomic_bool>(true);QVERIFY(!BrushRenderRequest::create(scene(),state,spec,cancelledToken,&error));
    }
    void concurrentAdmissionsAndOneShotExecutionCannotMultiplyReservations() {
        const auto source=scene(177,129);const auto state=edits();BrushRenderRequest::Spec spec;QString error;
        auto measured=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(measured,qPrintable(error));const auto needed=measured->reservedBytes();measured.reset();
        auto budget=std::make_shared<BrushRenderBudget>(needed);QSemaphore start;
        std::vector<QFuture<std::shared_ptr<BrushRenderRequest>>> futures;
        for(int i=0;i<8;++i)futures.push_back(QtConcurrent::run([&]{start.acquire();return std::shared_ptr<BrushRenderRequest>(BrushRenderRequest::create(source,state,spec,{},nullptr,budget));}));
        start.release(8);std::vector<std::shared_ptr<BrushRenderRequest>> admitted;
        for(auto &future:futures){future.waitForFinished();if(auto request=future.result())admitted.push_back(std::move(request));}
        QCOMPARE(admitted.size(),std::size_t(1));QCOMPARE(budget->snapshot().charged,needed);QCOMPARE(budget->snapshot().peak,needed);futures.clear();
        auto request=admitted.front();admitted.clear();QSemaphore go;
        auto one=QtConcurrent::run([&]{go.acquire();return request->render();});auto two=QtConcurrent::run([&]{go.acquire();return request->render();});go.release(2);
        one.waitForFinished();two.waitForFinished();auto a=one.result(),b=two.result();QVERIFY(bool(a)!=bool(b));request.reset();
        // QFuture's result also owns its lease until the future is released.
        one={};two={};a={};b={};QCOMPARE(budget->snapshot().charged,0);
    }
    void cancellationAndConsumerFailureReleaseOnlyAfterLastLease() {
        const auto source=scene(513,401);const auto state=edits();BrushRenderRequest::Spec spec;spec.layout=BrushRenderRequest::Layout::Stripes128;QString error;
        auto budget=std::make_shared<BrushRenderBudget>();auto request=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));
        std::vector<BrushImageLease> partial;int calls=0;auto token=std::make_shared<std::atomic_bool>(false);
        QVERIFY(!request->visitStrips([&](const auto &lease,int){partial.push_back(lease);if(++calls==2)token->store(true);return true;},token,&error));
        QCOMPARE(calls,2);QVERIFY(error.contains("Cancelled"));const auto charged=request->reservedBytes();
        QVERIFY(!request->visitStrips([](const auto &,int){return true;},{},&error));QVERIFY(error.contains("already"));request.reset();QCOMPARE(budget->snapshot().charged,charged);
        partial.clear();QCOMPARE(budget->snapshot().charged,0);
        auto healthy=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(healthy,qPrintable(error));
        QVERIFY(!healthy->visitStrips([](const auto &,int){return false;},{},&error));QVERIFY(error.contains("consumer"));healthy.reset();QCOMPARE(budget->snapshot().charged,0);
        QVERIFY(token->load());
    }
    void retainedLutAllocatedCapacityParticipatesInAdmission() {
        const auto source=scene(53,41);auto compact=LookLut::identity(2);auto expanded=std::make_shared<LookLut>(*compact);
        expanded->rgb.reserve(262144);QVERIFY(expanded->validate());
        auto state=edits();state.look.mode="calibrated";state.look.lut=compact;BrushRenderRequest::Spec spec;QString error;
        auto one=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(one,qPrintable(error));const auto small=one->reservedBytes();one.reset();
        state.look.lut=expanded;auto two=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(two,qPrintable(error));const auto large=two->reservedBytes();two.reset();
        QCOMPARE(large-small,qint64(expanded->rgb.capacity()-compact->rgb.capacity())*qint64(sizeof(float)));
        auto budget=std::make_shared<BrushRenderBudget>(large-1);QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error,budget));QCOMPARE(budget->snapshot().charged,0);
    }
    void malformedTypedLutsRefuseBeforePixelsAndReleaseAdmission() {
        const auto source=scene(53,41);const auto unchanged=source.copy();
        auto budget=std::make_shared<BrushRenderBudget>();BrushRenderRequest::Spec spec;QString error;
        for(const auto &mode:{QString("calibrated"),QString("manual")})for(int malformed=0;malformed<4;++malformed) {
            auto lut=std::make_shared<LookLut>(*LookLut::identity(2));
            if(malformed==0)lut->size=33;
            if(malformed==1)lut->rgb.pop_back();
            if(malformed==2)lut->rgb[0]=std::numeric_limits<float>::quiet_NaN();
            if(malformed==3)lut->rgb[0]=1.01f;
            auto state=edits();state.look.mode=mode;state.look.lut=lut;
            QVERIFY(!BrushRenderRequest::create(source,state,spec,{},&error,budget));
            QVERIFY(error.contains("Invalid"));QCOMPARE(budget->snapshot().charged,0);QCOMPARE(source,unchanged);
        }
        auto valid=edits();valid.look.mode="calibrated";valid.look.lut=LookLut::identity(2);
        auto request=BrushRenderRequest::create(source,valid,spec,{},&error,budget);QVERIFY2(request,qPrintable(error));
        QVERIFY(request->render({},&error));request.reset();QCOMPARE(budget->snapshot().charged,0);
    }
    void allExportFormatsAndSharedScopesMatchIndependentLegalReferences() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto source=scene(271,199);auto state=edits();
        state.geometry.straighten=.8;state.geometry.crop={.02,.04,.95,.91};state.look.mode="manual";state.look.code="FL";state.look.parameters={{"sharpness",4},{"clarity",2}};
        for(const auto space:{ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::AdobeRgb,ColorManagement::OutputSpace::ProPhotoRgb}) {
            BrushRenderRequest::Spec spec;spec.output=space;const auto reference=oracle(source,state,spec);QVERIFY(!reference.isNull());QString error;
            const auto prefix=ColorManagement::key(space);
            for(const auto format:{RasterFormat::Png16,RasterFormat::Tiff16,RasterFormat::WebP8}) {
                const auto extension=format==RasterFormat::Png16?"png":format==RasterFormat::Tiff16?"tif":"webp";
                const auto actual=dir.filePath(prefix+"-actual."+extension),legacy=dir.filePath(prefix+"-legacy."+extension);
                auto request=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(request,qPrintable(error));
                QVERIFY2(exportRaster(*request,actual,format,100,{},&error),qPrintable(error));
                QVERIFY2(exportRaster(source,state,legacy,space,format,100,{},&error,false,0),qPrintable(error));
                const QImage encoded(actual),expected(legacy);QVERIFY(!encoded.isNull());
                QCOMPARE(encoded.convertToFormat(QImage::Format_RGBA64),expected.convertToFormat(QImage::Format_RGBA64));QCOMPARE(encoded.colorSpace(),expected.colorSpace());
                if(format==RasterFormat::Png16)QCOMPARE(encoded.convertToFormat(QImage::Format_RGBA64),reference);
            }
            spec.layout=BrushRenderRequest::Layout::Stripes128;
            auto jpeg=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(jpeg,qPrintable(error));
            const auto actual=dir.filePath(prefix+"-actual.jpg"),legacy=dir.filePath(prefix+"-legacy.jpg");
            QVERIFY2(exportJpegTiled(*jpeg,actual,95,{},&error),qPrintable(error));QVERIFY2(exportJpegTiled(source,state,legacy,space,95,{},&error),qPrintable(error));QCOMPARE(fileBytes(actual),fileBytes(legacy));
            auto scope=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(scope,qPrintable(error));std::array<ScopePlotResult,3> plots;
            const auto histogram=ScopesEngine::analyzeAdmitted(*scope,{},&error,&plots),expected=ScopesEngine::analyze(reference);QCOMPARE(histogram.pixelCount,expected.pixelCount);
            QCOMPARE(histogram.red,expected.red);QCOMPARE(histogram.green,expected.green);QCOMPARE(histogram.blue,expected.blue);QCOMPARE(histogram.luma,expected.luma);
            QCOMPARE(histogram.shadowClipPercent,expected.shadowClipPercent);QCOMPARE(histogram.highlightClipPercent,expected.highlightClipPercent);
            const auto ink=renderEncodedScopePlots(reference);for(int i=0;i<3;++i){QCOMPARE(plots[i].pixels,ink[i].pixels);QCOMPARE(plots[i].image,ink[i].image);}
        }
    }
    void cancelledOrWrongLayoutExportsPreserveExistingTargetsAndScopeResults() {
        QTemporaryDir dir;const auto source=scene(513,401);const auto state=edits();BrushRenderRequest::Spec spec;QString error;
        const auto target=dir.filePath("keep.jpg");{QFile file(target);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("preserved"),9);}
        auto wrong=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(wrong,qPrintable(error));QVERIFY(!exportJpegTiled(*wrong,target,95,{},&error));QCOMPARE(fileBytes(target),QByteArray("preserved"));
        spec.layout=BrushRenderRequest::Layout::Stripes128;auto jpeg=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(jpeg,qPrintable(error));
        auto cancel=std::make_shared<std::atomic_bool>(false);int progress=0;
        QVERIFY(!exportJpegTiled(*jpeg,target,95,cancel,&error,[&](int value){progress=value;cancel->store(true);}));QVERIFY(progress>0);QVERIFY(error.contains("Cancelled"));QCOMPARE(fileBytes(target),QByteArray("preserved"));
        auto rasterWrong=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(rasterWrong,qPrintable(error));
        QVERIFY(!exportRaster(*rasterWrong,target,RasterFormat::Png16,92,{},&error));QCOMPARE(fileBytes(target),QByteArray("preserved"));
        auto scope=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(scope,qPrintable(error));
        std::array<ScopePlotResult,3> plots;for(auto &plot:plots){plot.image=QImage(2,2,QImage::Format_ARGB32);plot.image.fill(Qt::red);plot.pixels=17;plot.error="keep";}
        const auto before=plots;QCOMPARE(ScopesEngine::analyzeAdmitted(*scope,cancel,&error,&plots).pixelCount,quint64(0));
        for(int i=0;i<3;++i){QCOMPARE(plots[i].image,before[i].image);QCOMPARE(plots[i].pixels,quint64(17));QCOMPARE(plots[i].error,QString("keep"));}
        QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot),QStringList{"keep.jpg"});
    }
};
QTEST_MAIN(BrushAdmissionTests)
#include "BrushAdmissionTests.moc"
