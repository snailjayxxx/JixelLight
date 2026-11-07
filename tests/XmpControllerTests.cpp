#include "app/PhotoController.h"
#include "diagnostics/ActionTrace.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/metadata/XmpSidecar.h"
#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QSemaphore>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include <stdexcept>

namespace {
void require(bool ok,const QString &message) {if(!ok)throw std::runtime_error(message.toStdString());}
QByteArray read(const QString &path) {QFile file(path);require(file.open(QIODevice::ReadOnly),file.errorString());return file.readAll();}
void write(const QString &path,const QByteArray &data) {QFile file(path);require(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(data)==data.size(),"owned fixture write");}
QByteArray hash(const QString &p){return QCryptographicHash::hash(read(p),QCryptographicHash::Sha256).toHex();}
bool spin(const std::function<bool()> &ready,int ms=10000) {QElapsedTimer clock;clock.start();while(!ready()&&clock.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QTest::qWait(2);}return ready();}
QList<QVariantList> sql(const QString &folder,const QStringList &statements,bool mutation=false) {
 const auto name=QUuid::createUuid().toString();QList<QVariantList> rows;
 {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(folder+"/Project.db");if(!mutation)db.setConnectOptions("QSQLITE_OPEN_READONLY");
 require(db.open(),db.lastError().text());{QSqlQuery q(db);for(const auto &s:statements){require(q.exec(s),q.lastError().text()+s);while(q.next()){QVariantList row;for(int i=0;i<q.record().count();++i)row<<q.value(i);rows<<row;}}}db.close();}
 QSqlDatabase::removeDatabase(name);return rows;
}
QDialog *reviewDialog() {for(auto *w:QApplication::topLevelWidgets())if(w->isVisible()&&w->objectName()=="xmp-merge-review")return qobject_cast<QDialog*>(w);return nullptr;}
QProgressDialog *progressDialog() {for(auto *w:QApplication::topLevelWidgets())if(w->isVisible()&&w->objectName()=="xmp-merge-progress")return qobject_cast<QProgressDialog*>(w);return nullptr;}
void chooseIncoming(QDialog *dialog,bool pointer=false) {
 require(dialog,"review must exist");const auto combos=dialog->findChildren<QComboBox*>();require(!combos.isEmpty(),"actual conflict choices missing");
 for(auto *combo:combos){const auto index=combo->findData(int(XmpMerge::Choice::UseSidecar));require(index>0,"incoming choice missing");if(pointer){QTest::mouseClick(combo,Qt::LeftButton);QTest::keyClick(combo,Qt::Key_End);QTest::keyClick(combo,Qt::Key_Return);}else combo->setCurrentIndex(index);require(combo->currentIndex()==index,"actual selected choice");}
}
void apply(QDialog *dialog) {auto *box=dialog->findChild<QDialogButtonBox*>();require(box&&box->button(QDialogButtonBox::Apply)->isEnabled(),"complete explicit Apply");QTest::mouseClick(box->button(QDialogButtonBox::Apply),Qt::LeftButton);}
struct Fixture {
 QTemporaryDir dir;ProcessedImageProvider provider;std::unique_ptr<PhotoController> c;
 QString original,other,sidecar,project;QByteArray originalHash,sidecarHash;
 Fixture(const QString &language="en_US") {
  require(dir.isValid(),"temporary directory");original=dir.filePath("owned-a.png");other=dir.filePath("owned-b.png");sidecar=dir.filePath("incoming.xmp");
  QImage image(128,96,QImage::Format_RGBA64);image.fill(QColor(30,80,180));require(image.save(original),"owned image save");image.fill(QColor(140,30,70));require(image.save(other),"owned second image save");
  {ProjectDatabase db;require(db.create(dir.filePath("Owned.jlp"),"Owned"),db.lastError());project=db.projectPath();
   for(const auto &p:{original,other}){AdjustmentState s;s.look.mode="off";EditHistory h;h.initialize(s);require(db.addOrUpdatePhoto(p,s)&&db.updateBatch({{p,s}},{{p,h}})&&db.updateCurationBatch({{p,{0,"none"}}})&&db.updateTagsBatch({{p,{}}})&&db.flush(),db.lastError());auto bytes=read(p);require(db.recordOriginalIdentity(p,{bytes.size(),QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()}),db.lastError());}}
  originalHash=hash(original);
  AdjustmentState incoming;incoming.look.mode="off";incoming.exposure=1.25;CatalogTags tags;tags.keywords={"incoming"};tags.albums={"External"};tags.label="red";QString error;
  require(XmpSidecar::writeNew(sidecar,incoming,tags,4,"pick",&error),error);sidecarHash=hash(sidecar);
  c=std::make_unique<PhotoController>(&provider);c->setLanguage(language);c->setGpuEnabled(false);require(c->openProject(QUrl::fromLocalFile(project)),c->statusMessage());
  require(spin([&]{return c->currentFile()==original&&c->previewReady()&&!c->loading()&&!c->rendering();}),"actual controller image ready");
  c->finishInteraction();require(spin([&]{return !c->rendering();}),"finished initial preview");
 }
 QDialog *start() {require(c->requestXmpMerge(QUrl::fromLocalFile(sidecar)),"normal explicit request refused: "+c->statusMessage());require(spin([&]{return reviewDialog()||!c->xmpMergeBusy();}),"review delivery timeout");require(reviewDialog(),"actual review absent: "+c->statusMessage());return reviewDialog();}
 void done() {require(spin([&]{return !c->xmpMergeBusy();}),"durable merge timeout: "+c->statusMessage());}
 QList<QVariantList> rows() {return sql(project,{"SELECT path,adjustment_json FROM photos ORDER BY path","SELECT path,rating,flag FROM curation ORDER BY path","SELECT path,json FROM catalog_tags ORDER BY path"});}
 ProjectDatabase::SavedPhoto stored() {ProjectDatabase db;QVector<ProjectDatabase::SavedPhoto> photos;require(db.readSnapshot(project,&photos),db.lastError());for(auto &p:photos)if(p.path==original)return p;throw std::runtime_error("stored original absent");}
 ~Fixture(){if(c)c->cancelXmpMerge();c.reset();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
};
}

class XmpControllerTests:public QObject {
 Q_OBJECT
private slots:
 void initTestCase() {QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("JIXEL_XMP_EVIDENCE_DIR")+"/settings");QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("XmpControllerPrivate");QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qputenv("JIXELLIGHT_FORCE_CPU","1");require(!qEnvironmentVariable("JIXEL_XMP_EVIDENCE_DIR").isEmpty()&&QDir().mkpath(qEnvironmentVariable("JIXEL_XMP_EVIDENCE_DIR")),"owned native XMP evidence directory");
#ifdef Q_OS_WIN
  const QString expectedQpa="windows";
#elif defined(Q_OS_MACOS)
  const QString expectedQpa="cocoa";
#else
  const QString expectedQpa="offscreen";
#endif
  require(QGuiApplication::platformName()==expectedQpa,"actual native widget QPA required");
  qInfo().noquote()<<QString("XMP_CONTROLLER_NATIVE_V1 qpa=%1").arg(QGuiApplication::platformName());}
 void normalDialogActionBilingual_data(){QTest::addColumn<QString>("language");QTest::newRow("English")<<QString("en_US");QTest::newRow("Chinese")<<QString("zh_CN");}
 void normalDialogActionBilingual() {
  QFETCH(QString,language);Fixture f(language);bool picked=false;
  QTimer::singleShot(0,f.c.get(),[&]{for(auto *w:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(w)){dialog->selectFile(f.sidecar);picked=QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);return;}});
  f.c->openXmpImportDialog();QVERIFY(picked);QVERIFY(spin([&]{return reviewDialog()!=nullptr;}));auto *dialog=reviewDialog();
  QCOMPARE(dialog->windowTitle(),language=="en_US"?QString("Review XMP import fields"):QString::fromUtf8("逐项检查 XMP 导入"));
  chooseIncoming(dialog,true);QVERIFY(dialog->grab().save(qEnvironmentVariable("JIXEL_XMP_EVIDENCE_DIR")+"/normal-review-"+language+".png"));apply(dialog);f.done();
  QCOMPARE(f.c->exposure(),1.25);QCOMPARE(f.c->currentRating(),4);QCOMPARE(f.c->currentFlag(),QString("pick"));QCOMPARE(f.c->currentKeywords(),QStringList{"incoming"});
  QCOMPARE(f.c->currentAlbums(),QStringList{"External"});QCOMPARE(f.c->currentColorLabel(),QString("red"));auto stored=f.stored();QCOMPARE(stored.adjustments.exposure,1.25);QCOMPARE(stored.rating,4);QVERIFY(stored.history.canUndo());
  QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);QVERIFY(f.c->canUndo());f.c->undo();QCOMPARE(f.c->exposure(),0.0);QCOMPARE(f.c->currentRating(),4);QVERIFY(f.c->prepareToClose());
  stored=f.stored();QCOMPARE(stored.adjustments.exposure,0.0);QVERIFY(stored.history.canRedo());QCOMPARE(stored.tags.keywords,QStringList{"incoming"});
 }
 void cancelReviewPreservesRecordAndBothFiles() {Fixture f;const auto before=f.rows();auto *d=f.start();auto *box=d->findChild<QDialogButtonBox*>();QTest::mouseClick(box->button(QDialogButtonBox::Cancel),Qt::LeftButton);f.done();QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);}
 void durableBarrierRejectsEditsVersionsSelectionAndBrush() {
  Fixture f;const auto before=f.rows();f.start();const int index=f.c->currentIndex();f.c->setExposure(9);f.c->setRating(5);f.c->setFlag("reject");f.c->undo();f.c->redo();f.c->selectPhoto(1);QCOMPARE(f.c->currentIndex(),index);
  QVERIFY(!f.c->setSelectionRating(5));QVERIFY(!f.c->setSelectionFlag("reject"));QVERIFY(!f.c->setSelectionKeywords("forbidden"));QVERIFY(!f.c->setSelectionLabel("blue"));QVERIFY(!f.c->beginCrop());
  QVERIFY(!f.c->beginBrushStrokeModePressure("",.2,.2,.03,.5,1,1,"paint",.5));QVERIFY(!f.c->createProject(QUrl::fromLocalFile(f.dir.filePath("Other.jlp")),"Other"));QVERIFY(!f.c->openProject(QUrl::fromLocalFile(f.project)));QVERIFY(!f.c->prepareToClose()||!f.c->xmpMergeBusy());
  QCOMPARE(f.c->exposure(),0.0);QCOMPARE(f.c->currentRating(),0);QCOMPARE(f.rows(),before);
 }
 void reviewedSidecarReplacementCannotChangeAppliedBytes() {Fixture f;auto *d=f.start();chooseIncoming(d);write(f.sidecar,"<owned-new-content-not-the-reviewed-document/>");const auto replacement=hash(f.sidecar);apply(d);f.done();QCOMPARE(f.c->exposure(),1.25);QCOMPARE(f.stored().rating,4);QCOMPARE(hash(f.sidecar),replacement);QCOMPARE(hash(f.original),f.originalHash);}
 void originalChangedAfterReviewRefusesApply() {Fixture f;const auto before=f.rows();auto *d=f.start();chooseIncoming(d);write(f.original,read(f.other));apply(d);f.done();QCOMPARE(f.rows(),before);QCOMPARE(f.c->exposure(),0.0);QCOMPARE(f.c->currentRating(),0);QCOMPARE(hash(f.sidecar),f.sidecarHash);QVERIFY(f.c->statusMessage().contains("Original differs"));}
 void staleCatalogRefusalCannotRequeueOldGuiRows() {Fixture f;auto *d=f.start();chooseIncoming(d);sql(f.project,{"UPDATE curation SET rating=2 WHERE path='"+f.original+"'"},true);apply(d);f.done();QCOMPARE(f.c->currentRating(),0);QVERIFY(f.c->prepareToClose());QCOMPARE(f.stored().rating,2);QCOMPARE(f.stored().adjustments.exposure,0.0);QCOMPARE(hash(f.sidecar),f.sidecarHash);}
 void lateSqlFailureRollsBackWithoutGuiPublication() {Fixture f;const auto before=f.rows();auto *d=f.start();chooseIncoming(d);sql(f.project,{"CREATE TRIGGER owned_deny_merge BEFORE UPDATE ON catalog_tags BEGIN SELECT RAISE(ABORT,'owned late SQL failure'); END"},true);apply(d);f.done();QCOMPARE(f.rows(),before);QCOMPARE(f.c->exposure(),0.0);QCOMPARE(f.c->currentRating(),0);QVERIFY(f.c->prepareToClose());QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);}
 void reentrantCancellationDoesNotStartAnotherRequest() {Fixture f;const auto before=f.rows();bool cancelled=false;auto connection=connect(f.c.get(),&PhotoController::xmpMergeChanged,f.c.get(),[&]{if(f.c->xmpMergeBusy()){cancelled=true;QVERIFY(f.c->cancelXmpMerge());}});QVERIFY(!f.c->requestXmpMerge(QUrl::fromLocalFile(f.sidecar)));disconnect(connection);QVERIFY(cancelled);QVERIFY(!f.c->xmpMergeBusy());QCoreApplication::processEvents();QVERIFY(!reviewDialog());QCOMPARE(f.rows(),before);}
 void reentrantSelectionCannotRetargetImport() {Fixture f;const auto before=f.rows();bool changed=false;auto connection=connect(f.c.get(),&PhotoController::libraryChanged,f.c.get(),[&]{if(!changed){changed=true;f.c->selectPhoto(1);}});QVERIFY(!f.c->requestXmpMerge(QUrl::fromLocalFile(f.sidecar)));disconnect(connection);QVERIFY(changed);QCOMPARE(f.c->currentFile(),f.other);QVERIFY(!f.c->xmpMergeBusy());QVERIFY(!reviewDialog());QCOMPARE(f.rows(),before);}
 void wideSqlIntegerIsRejectedBeforeNarrowing() {Fixture f;sql(f.project,{"PRAGMA ignore_check_constraints=ON","UPDATE curation SET rating=4294967296 WHERE path='"+f.original+"'"},true);QVERIFY(f.c->requestXmpMerge(QUrl::fromLocalFile(f.sidecar)));f.done();QVERIFY(!reviewDialog());QVERIFY(f.c->statusMessage().contains("Invalid stored rating"));QCOMPARE(sql(f.project,{"SELECT rating FROM curation WHERE path='"+f.original+"'"}).first().first().toLongLong(),4294967296LL);}
 void catalogDateWorkerDeliveryIsDeferredUntilDurableReload() {
  Fixture f;auto *d=f.start();chooseIncoming(d);const auto before=f.c->library();
  f.c->m_catalogDatesJob->submit({f.c->m_catalogEpoch,{f.original,f.other}});
  QVERIFY(spin([&]{for(const auto &event:ActionTrace::instance().snapshot()){const auto o=event.toObject();if(o.value("action")=="xmp_merge_metadata_deferred"&&o.value("project")==f.project&&o.value("photos").toInt()>0)return true;}return false;}));
  QVERIFY(f.c->xmpMergeBusy());QCOMPARE(f.c->library(),before);QCOMPARE(f.c->exposure(),0.0);apply(d);f.done();QCOMPARE(f.c->exposure(),1.25);QVERIFY(f.c->prepareToClose());
  auto stored=f.stored();QCOMPARE(stored.adjustments.exposure,1.25);QCOMPARE(stored.rating,4);QVERIFY(stored.timeline.captureChecked);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
 }
 void ownerDestructionPreservesAlreadyAuthorizedCommit() {
  Fixture f;auto *d=f.start();chooseIncoming(d);
  QSemaphore release;std::atomic_bool actualDurable=false,postCommitReplyHeld=false;
  bool ownerDestroyed=false,releasedDuringDestruction=false,authorized=false;
  struct Unlock {QSemaphore &semaphore;~Unlock(){semaphore.release();}} unlock{release};
  auto *controller=f.c.get();
  connect(controller->m_xmpMerge->dialog.data(),&QDialog::accepted,this,[&]{authorized=true;});
  connect(&controller->m_project,&ProjectDatabase::saved,this,[&](int count){
   actualDurable.store(count==1,std::memory_order_release);
   QMetaObject::invokeMethod(this,[&]{
    require(f.c&&f.c->m_xmpMerge&&f.c->m_xmpMerge->phase==PhotoController::XmpMergePhase::Committing,"durable transaction still has no GUI acknowledgement");
    auto *progress=f.c->m_xmpMerge->progress.data();require(progress,"actual committing progress");
    connect(progress,&QObject::destroyed,this,[&]{releasedDuringDestruction=true;release.release();},Qt::DirectConnection);
    f.c.reset();ownerDestroyed=true;
   },Qt::QueuedConnection);
   postCommitReplyHeld.store(release.tryAcquire(1,10000),std::memory_order_release);
  },Qt::DirectConnection);
  apply(d);QVERIFY(spin([&]{return ownerDestroyed;}));QVERIFY(authorized);QVERIFY(actualDurable.load(std::memory_order_acquire));QVERIFY(postCommitReplyHeld.load(std::memory_order_acquire));QVERIFY(releasedDuringDestruction);
  auto saved=f.stored();QCOMPARE(saved.adjustments.exposure,1.25);QCOMPARE(saved.rating,4);QCOMPARE(saved.tags.keywords,QStringList{"incoming"});QVERIFY(saved.history.canUndo());QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);

 }
};
QTEST_MAIN(XmpControllerTests)
#include "XmpControllerTests.moc"
