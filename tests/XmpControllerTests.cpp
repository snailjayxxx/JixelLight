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
#include <QPointer>
#include <thread>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include <QLineEdit>
#include <QInputMethodEvent>
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
// A static file dialog may enter its event loop before its filesystem model
// can accept the owned selection. Observe actual acceptance with a deadline.
struct XmpFilePicker {
 QTimer timer;QElapsedTimer clock,visibleClock;QPointer<QFileDialog> observed;QString path;
 bool accepted=false,expired=false,select=true;int attempts=0,visibleTicks=0,deadlineMs=10000,minVisibleTicks=1,timerTicks=0;
 qint64 firstTimerMs=-1,firstVisibleMs=-1;
 explicit XmpFilePicker(QString value,int deadline=10000,int holdTicks=1,bool selectFile=true)
  :path(std::move(value)),select(selectFile),deadlineMs(deadline),minVisibleTicks(holdTicks) {
  timer.setInterval(5);
  QObject::connect(&timer,&QTimer::timeout,&timer,[this] {
   ++timerTicks;if(firstTimerMs<0)firstTimerMs=clock.elapsed();
   // For the unanswered-dialog check, construction/readiness is distinct from
   // the unchanged 150ms visible response window. A dialog that never becomes
   // visible still fails within the existing 10s picker readiness allowance.
   const bool timedOut=select?clock.elapsed()>=deadlineMs
       :visibleClock.isValid()?visibleClock.elapsed()>=deadlineMs:clock.elapsed()>=10000;
   if(timedOut) {
    expired=true;timer.stop();
    for(auto *w:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QFileDialog*>(w))d->reject();
    qInfo()<<"XMP_FILE_PICKER_V1 expired=true attempts="<<attempts<<"visible_ticks="<<visibleTicks<<"elapsed_ms="<<clock.elapsed()<<"timer_ticks="<<timerTicks<<"first_tick_ms="<<firstTimerMs<<"first_visible_ms="<<firstVisibleMs;return;
   }
   for(auto *w:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QFileDialog*>(w);d&&d->isVisible()) {
    if(firstVisibleMs<0)firstVisibleMs=clock.elapsed();
    if(observed!=d) {observed=d;QObject::connect(d,&QDialog::accepted,&timer,[this] {
      accepted=true;timer.stop();qInfo()<<"XMP_FILE_PICKER_V1 accepted=true attempts="<<attempts<<"visible_ticks="<<visibleTicks<<"elapsed_ms="<<clock.elapsed()<<"timer_ticks="<<timerTicks<<"first_tick_ms="<<firstTimerMs<<"first_visible_ms="<<firstVisibleMs;
    });}
    ++visibleTicks;
    if(!select){if(!visibleClock.isValid())visibleClock.start();return;}
    if(visibleTicks<minVisibleTicks)return;
    ++attempts;
    // Qt6.8.3 selectFile leaves a visible focused filename editor unchanged.
    // Enter the owned path through its widget input event, then require the
    // actual selected file and the real accepted signal. No fabricated result.
    auto *fileName=d->findChild<QLineEdit*>("fileNameEdit");
    if(!fileName)return;
    if(fileName->text()!=path) {
     fileName->setFocus(Qt::OtherFocusReason);fileName->selectAll();
     QInputMethodEvent input;input.setCommitString(path);
     QCoreApplication::sendEvent(fileName,&input);
    }
    if(d->selectedFiles()!=QStringList{path})return;
    qInfo()<<"XMP_PICKER_WIDGET_INPUT_V1 actual_selected_owned_path=true";
    QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);return;
   }
  });
 }
 void start(){clock.start();timer.start();}
 void stop(){timer.stop();}
};
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
  QFETCH(QString,language);qInfo().noquote()<<"XMP_NORMAL_REVIEW_PHASE fixture-start"<<language;
  Fixture f(language);qInfo()<<"XMP_NORMAL_REVIEW_PHASE fixture-ready";
  XmpFilePicker picker(f.sidecar);picker.start();qInfo()<<"XMP_NORMAL_REVIEW_PHASE picker-start";
  f.c->openXmpImportDialog();picker.stop();QVERIFY2(picker.accepted&&!picker.expired,"Actual file picker acceptance required within the owned deadline");
  qInfo()<<"XMP_NORMAL_REVIEW_PHASE picker-accepted";QVERIFY(spin([&]{return reviewDialog()!=nullptr;}));auto *dialog=reviewDialog();
  bool commandTraced=false;for(const auto &value:ActionTrace::instance().snapshot()) {
   const auto row=value.toObject(),command=row["command"].toObject();
   if(row["action"]=="xmp_command"&&command["command"]=="xmp.review"&&command["source"].toString()==f.sidecar) {
    QCOMPARE(row["project"].toString(),f.project);QCOMPARE(row["completion"].toString(),QString("queued-for-review"));commandTraced=true;
   }
  }
  QVERIFY(commandTraced);
  QCOMPARE(dialog->windowTitle(),language=="en_US"?QString("Review XMP import fields"):QString::fromUtf8("逐项检查 XMP 导入"));
  chooseIncoming(dialog,true);QVERIFY(dialog->grab().save(qEnvironmentVariable("JIXEL_XMP_EVIDENCE_DIR")+"/normal-review-"+language+".png"));apply(dialog);f.done();
  QCOMPARE(f.c->exposure(),1.25);QCOMPARE(f.c->currentRating(),4);QCOMPARE(f.c->currentFlag(),QString("pick"));QCOMPARE(f.c->currentKeywords(),QStringList{"incoming"});
  QCOMPARE(f.c->currentAlbums(),QStringList{"External"});QCOMPARE(f.c->currentColorLabel(),QString("red"));auto stored=f.stored();QCOMPARE(stored.adjustments.exposure,1.25);QCOMPARE(stored.rating,4);QVERIFY(stored.history.canUndo());
  QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);QVERIFY(f.c->canUndo());f.c->undo();QCOMPARE(f.c->exposure(),0.0);QCOMPARE(f.c->currentRating(),4);QVERIFY(f.c->prepareToClose());
  stored=f.stored();QCOMPARE(stored.adjustments.exposure,0.0);QVERIFY(stored.history.canRedo());QCOMPARE(stored.tags.keywords,QStringList{"incoming"});
 }
 void cancelReviewPreservesRecordAndBothFiles() {Fixture f;const auto before=f.rows();auto *d=f.start();const auto id=f.c->xmpMergeOperationId();QVERIFY(!id.isEmpty());auto *box=d->findChild<QDialogButtonBox*>();QTest::mouseClick(box->button(QDialogButtonBox::Cancel),Qt::LeftButton);f.done();QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
  bool traced=false;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject(),command=row["command"].toObject();if(row["action"]=="operation_cancel_requested"&&command["command"]=="xmp.cancel"&&command["operation_id"]==id){QCOMPARE(row["kind"].toString(),QString("xmp-review"));traced=true;}}QVERIFY(traced);QVERIFY(f.c->xmpMergeOperationId().isEmpty());}
 void cancellationCommandsCannotReachAnotherReviewOrRunFromForeignThreads() {
  Fixture f;const auto before=f.rows();f.start();const auto first=f.c->xmpMergeOperationId();QVERIFY(!first.isEmpty());QCOMPARE(f.c->property("xmpMergeOperationId").toString(),first);
  const QVariantMap cancel{{"command","xmp.cancel"},{"operation_id",first}};
  bool foreign=false,accepted=true,legacy=true;std::thread worker([&]{foreign=QThread::currentThread()!=f.c->thread();accepted=f.c->executeCommand(cancel);legacy=f.c->cancelXmpMerge();});worker.join();QVERIFY(foreign);QVERIFY(!accepted);QVERIFY(!legacy);QVERIFY(f.c->xmpMergeBusy());QCOMPARE(f.rows(),before);
  auto malformed=cancel;malformed["all"]=true;QVERIFY(!f.c->executeCommand(malformed));QVERIFY(f.c->xmpMergeBusy());QVERIFY(f.c->executeCommand(cancel));f.done();QVERIFY(f.c->xmpMergeOperationId().isEmpty());QVERIFY(!f.c->executeCommand(cancel));QVERIFY(f.c->cancelXmpMerge());
  auto *d=f.start();const auto second=f.c->xmpMergeOperationId();QVERIFY(!second.isEmpty());QVERIFY(second!=first);QVERIFY(!f.c->executeCommand(cancel));QCOMPARE(f.c->xmpMergeOperationId(),second);QVERIFY(f.c->xmpMergeBusy());
  chooseIncoming(d);apply(d);f.done();QCOMPARE(f.c->exposure(),1.25);QCOMPARE(f.c->currentRating(),4);QCOMPARE(f.c->currentFlag(),QString("pick"));QVERIFY(f.c->xmpMergeOperationId().isEmpty());QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
 }
 void cancellationTraceSurvivesOwnerDestructionDuringItsCompletionSignal() {
  Fixture f;const auto before=f.rows();f.start();const auto id=f.c->xmpMergeOperationId();const QVariantMap command{{"command","xmp.cancel"},{"operation_id",id}};
  QObject observer;connect(f.c.get(),&PhotoController::xmpMergeChanged,&observer,[&]{if(f.c&&!f.c->xmpMergeBusy())f.c.reset();});
  auto *owner=f.c.get();QVERIFY(owner->executeCommand(command));QVERIFY(!f.c);QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
  bool traced=false;for(const auto &v:ActionTrace::instance().snapshot()){const auto row=v.toObject();if(row["action"]=="operation_cancel_requested"&&row["command"].toObject()==QJsonObject::fromVariantMap(command)){QCOMPARE(row["completion"].toString(),QString("cancelled-before-commit"));QCOMPARE(row["project"].toString(),f.project);traced=true;}}QVERIFY(traced);
 }
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
 void returnInIncompleteReviewThenEscapePreservesCatalogAndFiles() {
  Fixture f;const auto before=f.rows();auto *d=f.start();auto combos=d->findChildren<QComboBox*>();QVERIFY(combos.size()==6);
  auto *box=d->findChild<QDialogButtonBox*>();QVERIFY(box);auto *applyButton=box->button(QDialogButtonBox::Apply);auto *cancelButton=box->button(QDialogButtonBox::Cancel);
  QVERIFY(!applyButton->autoDefault()&&!applyButton->isDefault());QVERIFY(!cancelButton->autoDefault()&&!cancelButton->isDefault());
  QSignalSpy accepted(d,&QDialog::accepted),rejected(d,&QDialog::rejected);
  QTest::mouseClick(combos.front(),Qt::LeftButton);QTest::keyClick(combos.front(),Qt::Key_End);QTest::keyClick(combos.front(),Qt::Key_Return);
  QVERIFY(d->isVisible());QVERIFY(f.c->xmpMergeBusy());QCOMPARE(accepted.count(),0);QCOMPARE(rejected.count(),0);QVERIFY(!applyButton->isEnabled());QCOMPARE(f.rows(),before);
  QTest::keyClick(d,Qt::Key_Escape);f.done();QCOMPARE(accepted.count(),0);QVERIFY(rejected.count()>=1);QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
  qInfo()<<"XMP_EXPLICIT_ACTIONS incomplete_return_kept_review=true escape_cancelled=true catalog_and_files_unchanged=true";
 }
 void returnInCompleteReviewThenExplicitSpaceAppliesDurably() {
  Fixture f;const auto before=f.rows();auto *d=f.start();chooseIncoming(d);auto *box=d->findChild<QDialogButtonBox*>();QVERIFY(box);auto *applyButton=box->button(QDialogButtonBox::Apply);QVERIFY(applyButton->isEnabled());
  QSignalSpy accepted(d,&QDialog::accepted),rejected(d,&QDialog::rejected),clicked(applyButton,&QPushButton::clicked);
  QTest::keyClick(d,Qt::Key_Return);QVERIFY(d->isVisible());QVERIFY(f.c->xmpMergeBusy());QCOMPARE(accepted.count(),0);QCOMPARE(rejected.count(),0);QCOMPARE(clicked.count(),0);QCOMPARE(f.rows(),before);
  applyButton->setFocus(Qt::TabFocusReason);QTest::keyClick(applyButton,Qt::Key_Space);QCOMPARE(clicked.count(),1);f.done();QVERIFY(accepted.count()>=1);
  QCOMPARE(f.c->exposure(),1.25);QCOMPARE(f.c->currentRating(),4);QCOMPARE(f.c->currentFlag(),QString("pick"));QCOMPARE(f.c->currentKeywords(),QStringList{"incoming"});QCOMPARE(f.c->currentAlbums(),QStringList{"External"});QCOMPARE(f.c->currentColorLabel(),QString("red"));
  const auto stored=f.stored();QCOMPARE(stored.adjustments.exposure,1.25);QCOMPARE(stored.rating,4);QCOMPARE(stored.flag,QString("pick"));QCOMPARE(stored.tags.keywords,QStringList{"incoming"});QCOMPARE(stored.tags.albums,QStringList{"External"});QCOMPARE(stored.tags.label,QString("red"));QVERIFY(stored.history.canUndo());QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
  qInfo()<<"XMP_EXPLICIT_ACTIONS complete_return_kept_review=true explicit_space_clicked=true durable_six_fields=true files_unchanged=true";
 }
 void explicitCancelAfterCompleteChoicesPreservesCatalogAndFiles() {
  Fixture f;const auto before=f.rows();auto *d=f.start();chooseIncoming(d,true);QVERIFY(d->isVisible());QVERIFY(f.c->xmpMergeBusy());auto *box=d->findChild<QDialogButtonBox*>();QVERIFY(box&&box->button(QDialogButtonBox::Apply)->isEnabled());
  QSignalSpy accepted(d,&QDialog::accepted),rejected(d,&QDialog::rejected);QTest::mouseClick(box->button(QDialogButtonBox::Cancel),Qt::LeftButton);f.done();QCOMPARE(accepted.count(),0);QVERIFY(rejected.count()>=1);QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
  qInfo()<<"XMP_EXPLICIT_ACTIONS explicit_cancel_clicked=true complete_choices_not_applied=true catalog_and_files_unchanged=true";
 }

 void filePickerWaitsForVisibleDialogAndRealAcceptance() {
  Fixture f;const auto before=f.rows();XmpFilePicker picker(f.sidecar,10000,3);picker.start();f.c->openXmpImportDialog();picker.stop();
  QVERIFY(picker.accepted);QVERIFY(!picker.expired);QVERIFY(picker.visibleTicks>=3);QVERIFY(picker.attempts>=1);QVERIFY(spin([&]{return reviewDialog()!=nullptr;}));
  QVERIFY(f.c->cancelXmpMerge());f.done();QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
  qInfo()<<"XMP_EXPLICIT_ACTIONS delayed_picker_real_acceptance=true review_cancelled=true catalog_and_files_unchanged=true";
 }
 void unansweredFilePickerDeadlineClosesWithoutCatalogMutation() {
  Fixture f;const auto before=f.rows();XmpFilePicker picker(f.sidecar,150,1,false);picker.start();f.c->openXmpImportDialog();picker.stop();
  QVERIFY(!picker.accepted);QVERIFY(picker.expired);QVERIFY(picker.visibleTicks>=1);QCOMPARE(picker.attempts,0);QVERIFY(!f.c->xmpMergeBusy());QCOMPARE(f.rows(),before);QCOMPARE(hash(f.original),f.originalHash);QCOMPARE(hash(f.sidecar),f.sidecarHash);
  QVERIFY(picker.visibleClock.isValid());QVERIFY(picker.visibleClock.elapsed()>=150);QVERIFY(!picker.observed||!picker.observed->isVisible());
  qInfo()<<"XMP_EXPLICIT_ACTIONS unanswered_picker_deadline_closed=true no_acceptance=true catalog_and_files_unchanged=true";
 }

};
QTEST_MAIN(XmpControllerTests)
#include "XmpControllerTests.moc"
