#include <QtTest>
#include <QElapsedTimer>
#include <QFile>
#include <QJSEngine>
#include <cmath>

class BrushGuideTests:public QObject {
    Q_OBJECT
    static bool load(QJSEngine &engine){QFile file(QFINDTESTDATA("../ui/BrushGuide.js"));if(!file.open(QIODevice::ReadOnly))return false;auto code=QString::fromUtf8(file.readAll());code.remove(".pragma library");return !engine.evaluate(code,file.fileName()).isError();}
    static QJSValue append(QJSEngine &engine,const QJSValue &points,double x,double y){return engine.globalObject().property("appendVisualPoint").call({points,x,y});}
private slots:
    void tinyJitterAvoidsCopyingThousandsOfGuideArrays(){
        QJSEngine engine;QVERIFY(load(engine));auto points=append(engine,engine.newArray(),10,20);QVERIFY(points.isArray());int accepted=1;
        for(int i=0;i<36000;++i){const auto next=append(engine,points,10+.1*(i%9000)/9000.,20);QVERIFY(!next.isError());if(!next.isNull()){points=next;++accepted;}}
        QCOMPARE(accepted,1);QCOMPARE(points.property("length").toInt(),1);QCOMPARE(points.property(0).property("x").toNumber(),10.);
    }
    void accumulatedSlowMovementAndCornersRemainWithinHalfLogicalPixel(){
        QJSEngine engine;QVERIFY(load(engine));auto points=engine.newArray();int accepted=0;double maximum=0;
        for(int i=0;i<10000;++i){const double x=.01*i,y=.1*std::sin(i*.03);const auto next=append(engine,points,x,y);QVERIFY(!next.isError());
            if(!next.isNull()){points=next;++accepted;}const auto last=points.property(points.property("length").toUInt()-1);
            maximum=std::max(maximum,std::hypot(x-last.property("x").toNumber(),y-last.property("y").toNumber()));}
        QVERIFY(accepted>150);QVERIFY(accepted<250);QVERIFY(maximum<.5);QVERIFY(points.property(points.property("length").toUInt()-1).property("x").toNumber()>99.4);
    }
    void halfPixelBoundaryAndLargeMovesPreserveCoordinates(){
        QJSEngine engine;QVERIFY(load(engine));auto points=append(engine,engine.newArray(),0,0);QVERIFY(append(engine,points,.499,0).isNull());
        points=append(engine,points,.5,0);QCOMPARE(points.property("length").toInt(),2);points=append(engine,points,.5,4);QCOMPARE(points.property("length").toInt(),3);
        QCOMPARE(points.property(2).property("x").toNumber(),.5);QCOMPARE(points.property(2).property("y").toNumber(),4.);
    }
    void longGuideRetainsItsFirstAndLatestPointsWithinExistingLimit(){
        QJSEngine engine;QVERIFY(load(engine));auto points=engine.newArray();
        for(int i=0;i<5000;++i){points=append(engine,points,i,0);QVERIFY(points.isArray());QVERIFY(points.property("length").toInt()<=1024);}
        QCOMPARE(points.property(0).property("x").toNumber(),0.);QCOMPARE(points.property(points.property("length").toUInt()-1).property("x").toNumber(),4999.);
        for(uint i=1;i<points.property("length").toUInt();++i)QVERIFY(points.property(i).property("x").toNumber()>points.property(i-1).property("x").toNumber());
    }
    void invalidCoordinatesNeverEnterTheCanvasPath(){
        QJSEngine engine;QVERIFY(load(engine));const auto points=append(engine,engine.newArray(),1,2);
        QVERIFY(append(engine,points,std::numeric_limits<double>::quiet_NaN(),2).isNull());QVERIFY(append(engine,points,1,std::numeric_limits<double>::infinity()).isNull());QCOMPARE(points.property("length").toInt(),1);
    }
    void originalAndSimplifiedGuideCostUsesSameSubmittedCoordinates(){
        QJSEngine engine;QVERIFY(load(engine));engine.evaluate("function original(points,x,y){let next=points.slice();if(next.length>=1024)next=next.filter(function(p,i){return i%2===0});next.push({x:x,y:y});return next}");
        QElapsedTimer timer;auto points=engine.newArray();const auto original=engine.globalObject().property("original");timer.start();
        for(int i=0;i<36000;++i)points=original.call({points,10+.1*(i%9000)/9000.,20});const auto oldNs=timer.nsecsElapsed();QVERIFY(points.property("length").toInt()<=1024);
        points=engine.newArray();timer.restart();for(int i=0;i<36000;++i){const auto next=append(engine,points,10+.1*(i%9000)/9000.,20);if(!next.isNull())points=next;}const auto newNs=timer.nsecsElapsed();
        QCOMPARE(points.property("length").toInt(),1);qInfo()<<"Owned Qt JS guide benchmark:36000 identical submitted coordinates; original_ms"<<oldNs/1.e6<<"simplified_ms"<<newNs/1.e6<<"no pressure/controller filtering";
    }
};
QTEST_GUILESS_MAIN(BrushGuideTests)
#include "BrushGuideTests.moc"
