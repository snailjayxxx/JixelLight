#include <QtTest>
#include <QJsonDocument>
#include <QDir>
#include <limits>
#include "core/commands/EditRecipe.h"
#include "core/commands/CommandSession.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/look/SonyLookMetadata.h"

class EditRecipeTests:public QObject {
    Q_OBJECT
    static AdjustmentState complexState() {
        AdjustmentState s;
        for(const auto &p:CommandRegistry::parameters())s.*p.member=p.minimum+(p.maximum-p.minimum)*.63;
        s.geometry.crop={.1,.15,.8,.75};s.geometry.quarterTurns=3;s.geometry.flipHorizontal=true;s.geometry.flipVertical=true;s.geometry.straighten=-4.25;
        for(const auto &p:CommandRegistry::geometryParameters())s.geometry.*p.member=p.maximum*.3;
        for(int i=0;i<8;++i){s.hslHue[i]=i*7-21;s.hslSaturation[i]=i*4-11;s.hslLuminance[i]=i*3-8;s.bwMix[i]=i*8-28;}
        s.masterCurve={.01,.2,.6,.8,.99};s.redCurve={.03,.26,.52,.8,1};s.greenCurve={0,.28,.55,.78,.97};s.blueCurve={.01,.26,.54,.79,.98};
        s.blackWhite=true;s.gradingEnabled=true;s.gradingHue={23,184,332};s.gradingSaturation={15,30,45};s.gradingBalance=-17;
        s.pointColorLab={.7,.13,-.2};s.pointColorEnabled=true;
        s.linearMaskEnabled=true;s.linearMaskExposure=.7;s.linearMaskCenter=.3;s.linearMaskFeather=.17;s.linearMaskAngle=23;
        s.radialMaskEnabled=true;s.radialMaskInverted=true;s.radialMaskExposure=-.5;s.radialMaskCenterX=.4;s.radialMaskCenterY=.6;
        s.radialMaskRadiusX=.2;s.radialMaskRadiusY=.4;s.radialMaskFeather=.3;s.radialMaskAngle=-46;
        s.colorMaskEnabled=true;s.colorMaskInverted=true;s.colorMaskLab={.51,-.12,.24};s.colorMaskExposure=.4;
        s.colorMaskHueRange=66;s.colorMaskChromaRange=.2;s.colorMaskLightnessRange=.3;
        s.luminanceMaskEnabled=true;s.luminanceMaskInverted=true;s.luminanceMaskExposure=.8;
        s.luminanceMaskLower=1.1;s.luminanceMaskUpper=1.7;s.luminanceMaskFeather=.23;
        BrushMaskGeometry::Stroke paint{{.12,.6,.8},{{{.3,.4},.7},{{.4,.5},1}}};
        auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;
        s.brushMasks.layers={{"paint",true,.6,{paint,erase}},{"hidden",false,-.8,{paint}}};
        s.look.mode="manual";s.look.code="FL";s.look.strength=.73;
        for(const auto &name:SonyLookMetadata::parameterNames()) {
            const auto range=SonyLookMetadata::parameterRange(name);s.look.parameters[name]=range.first+(range.second-range.first)/2;
        }
        return s;
    }
    static QJsonArray commands(const EditRecipe::Result &result){return QJsonDocument::fromJson(result.text.toUtf8()).array();}
private slots:
    void defaultRecipeReplacesEveryPriorAdjustmentWithoutMutatingSource() {
        const AdjustmentState source;const auto before=source.toJson();const auto result=EditRecipe::encode(source,false);
        QVERIFY(result);QVERIFY(result.text.size()<=EditRecipe::MaximumTextLength);QCOMPARE(result.text,EditRecipe::encode(source,false).text);
        auto target=complexState();QVERIFY(CommandRegistry::executeBatch(target,commands(result)));
        QCOMPARE(target.toJson(),before);QCOMPARE(source.toJson(),before);QCOMPARE(commands(result).first().toObject()["command"].toString(),QString("develop.reset"));
    }
    void fullStateSurvivesTextRoundtripAndHasIdenticalPixels() {
        const auto source=complexState();const auto before=source.toJson();const auto result=EditRecipe::encode(source,true);QVERIFY(result);
        AdjustmentState target;target.exposure=-5;QVERIFY(CommandRegistry::executeBatch(target,commands(result)));
        QCOMPARE(target.toJson(),before);QCOMPARE(source.toJson(),before);
        QImage image(32,24,QImage::Format_RGBA64);
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)reinterpret_cast<QRgba64*>(image.scanLine(y))[x]=QRgba64::fromRgba64(x*1900,y*2400,(x+y)*1100,((x+y)%3)*32767);
        const auto expected=ImagePipeline::process(image,source);const auto actual=ImagePipeline::process(image,target);
        QVERIFY(!expected.isNull());QCOMPARE(actual,expected);
    }
    void disabledSelectionsAndHiddenLookControlsArePreserved() {
        auto source=complexState();source.pointColorEnabled=false;source.linearMaskEnabled=false;source.radialMaskEnabled=false;
        source.colorMaskEnabled=false;source.luminanceMaskEnabled=false;source.blackWhite=false;source.gradingEnabled=false;source.look.mode="off";
        const auto result=EditRecipe::encode(source,false);QVERIFY(result);AdjustmentState target;
        QVERIFY(CommandRegistry::executeBatch(target,commands(result)));QCOMPARE(target.toJson(),source.toJson());
    }
    void asShotStaysSymbolicAndUsesDestinationRawMetadata() {
        AdjustmentState source;source.look.mode="as-shot";source.look.strength=.6;
        const auto result=EditRecipe::encode(source,true);QVERIFY(result);QVERIFY(!EditRecipe::encode(source,false));
        const auto edits=commands(result);QVERIFY(CommandRegistry::requiresLookContext(AdjustmentState{},edits));
        for(const auto &code:QStringList{"FL","PT"}) {
            const CommandRegistry::LookContext context{true,{{"sonyLook",QVariantMap{{"autoEligible",true},{"code",code}}}}};
            AdjustmentState target;QVERIFY(CommandRegistry::executeBatch(target,edits,nullptr,&context));QCOMPARE(target.toJson(),source.toJson());
            QCOMPARE(LookProfiles::resolveAsShot(target,context.metadata,true).look.code,code);
        }
    }
    void invalidOrLossyStatesReturnNoPartialRecipe() {
        QList<AdjustmentState> invalid;
        auto s=complexState();s.exposure=std::numeric_limits<double>::quiet_NaN();invalid<<s;
        s=complexState();s.geometry.crop.setWidth(2);invalid<<s;
        s=complexState();s.luminanceMaskLower=1.9;invalid<<s;
        s=complexState();s.brushMasks.layers[0].id="invalid identity";invalid<<s;
        s=complexState();s.look.parameters["fade"]=2.6;invalid<<s;
        s=complexState();s.look.error="missing profile";invalid<<s;
        s=complexState();s.look.mode="calibrated";invalid<<s;
        s=complexState();s.look.lut=std::make_shared<LookLut>();invalid<<s;
        for(const auto &bad:invalid){const auto result=EditRecipe::encode(bad,true);QVERIFY(!result);QVERIFY(result.text.isEmpty());QCOMPARE(result.commandCount,qsizetype(0));}
    }
    void largeCanonicalBrushCannotExceedClipboardRecipeLimit() {
        AdjustmentState source;BrushMaskGeometry::Stroke stroke;
        stroke.dabs.resize(4096,{{1.2345678901234567e-100,8.765432109876543e-101},7.654321098765432e-102});
        source.brushMasks.layers={{"large",true,.7,{stroke,stroke,stroke,stroke}}};
        QVERIFY(BrushMaskDocument::validDocument(source.brushMasks));const auto result=EditRecipe::encode(source,false);
        QCOMPARE(result.error,EditRecipe::Error::TooLong);QVERIFY(result.text.isEmpty());
    }
    void copiedRecipeWorksAsOneOfflineSessionHistoryStep() {
        const auto source=complexState();const auto result=EditRecipe::encode(source,true);QVERIFY(result);
        const auto initial=QJsonObject{{"command","develop.set"},{"parameter","exposure"},{"value",-1}};
        const auto save=[](const char *name){return QJsonObject{{"command","export.run"},{"destination",QLatin1String(name)}};};
        const QJsonObject plan{{"schema",1},{"engine",QLatin1String(ProcessingPlan::EngineVersion)},
            {"commands",QJsonArray{QJsonObject{{"command","photo.import"},{"photo","a"},{"source","a.ARW"}},initial,
                QJsonObject{{"command","edit.batch"},{"commands",commands(result)}},save("copied.png"),
                QJsonObject{{"command","history.undo"}},save("undo.png"),QJsonObject{{"command","history.redo"}},save("redo.png")}}};
        const auto context=[](const QString &,bool,QString *)->std::optional<CommandRegistry::LookContext>{return CommandRegistry::LookContext{true,{}};};
        CommandSession session;QString error;QVERIFY2(CommandSession::compile(plan,QDir::tempPath(),context,&session,&error),qPrintable(error));
        QCOMPARE(session.exports.size(),3);QCOMPARE(session.exports[0].state.toJson(),source.toJson());
        AdjustmentState prior;prior.look.mode="as-shot";prior.exposure=-1;QCOMPARE(session.exports[1].state.toJson(),prior.toJson());QCOMPARE(session.exports[2].state.toJson(),source.toJson());
    }
};
QTEST_GUILESS_MAIN(EditRecipeTests)
#include "EditRecipeTests.moc"
