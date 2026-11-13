#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"

namespace {
QByteArray sourceHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);return hash.addData(&file)?hash.result().toHex():QByteArray{};
}
}

class CatalogNotificationTests final:public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("JixelLightCatalogOwner-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,m_settings.path());
        QSettings settings;QVERIFY(settings.fileName().startsWith(m_settings.path()+"/"));
    }
    void clipboardNotificationAcceptedCloseKeepsTerminalStatus() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(64,48,QImage::Format_RGBA64);image.fill(QColor(80,120,160));
        const auto path=dir.filePath("clipboard.png");QVERIFY(image.save(path));const auto original=sourceHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"ClipboardOwner"));c.setExposure(.75);c.finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QVERIFY(c.flushEdits());
        bool notified=false,closed=false;QString stoppedStatus;quint64 stoppedRevision=0;
        QObject::connect(&c,&PhotoController::adjustmentClipboardChanged,&c,[&]{if(notified)return;notified=true;closed=c.beginClose();stoppedStatus=c.statusMessage();stoppedRevision=c.m_requestedRevision;});
        c.copyAdjustments();QVERIFY(notified);QVERIFY(closed);QVERIFY(c.m_closing);
        QCOMPARE(c.statusMessage(),stoppedStatus);QCOMPARE(c.m_requestedRevision,stoppedRevision);
        ProjectDatabase snapshot;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(snapshot.readSnapshot(c.projectPath(),&rows));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].adjustments.exposure,.75);QCOMPARE(sourceHash(path),original);
    }
};
QTEST_MAIN(CatalogNotificationTests)
#include "CatalogNotificationTests.moc"
