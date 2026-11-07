#include "XmpCapture.h"
#include "core/look/LookLut.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <filesystem>

namespace XmpCapture {
struct Captured::Data {
    Target target;
    QString locator;
    QByteArray bytes, digest;
    XmpSidecar::Document parsed;
};
namespace {
bool text(const QString &s) { return !s.trimmed().isEmpty() && !s.contains(QChar(0)); }
std::filesystem::path fsPath(const QString &s) {
#ifdef Q_OS_WIN
    return std::filesystem::path(s.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(s).constData());
#endif
}
bool aliases(const QString &a, const QString &b) {
    if (b.isEmpty()) return false;
    std::error_code error;
    return std::filesystem::equivalent(fsPath(a), fsPath(b), error) && !error;
}
bool sameObservedFile(const QFileInfo &a, const QFileInfo &b) {
    return b.exists() && b.isFile() && !b.isSymLink()
        && a.canonicalFilePath() == b.canonicalFilePath() && a.size() == b.size()
        && a.lastModified() == b.lastModified() && a.birthTime() == b.birthTime()
        && a.metadataChangeTime() == b.metadataChangeTime();
}
}

bool Target::valid() const {
    return text(projectLocator) && text(sourceKey) && text(storageKey) && text(sourceIdentity)
        && !copyKey.contains(QChar(0)) && !controller.isNull()
        && catalogEpoch && photoEpoch && sequence;
}
Target Captured::target() const { return m_data ? m_data->target : Target{}; }
QString Captured::locator() const { return m_data ? m_data->locator : QString{}; }
QByteArray Captured::sha256() const { return m_data ? m_data->digest : QByteArray{}; }
qint64 Captured::byteCount() const { return m_data ? m_data->bytes.size() : 0; }
QByteArray Captured::bytes() const { return m_data ? m_data->bytes : QByteArray{}; }
XmpSidecar::Document Captured::document() const {
    if (!m_data) return {};
    auto value = m_data->parsed;
    if (value.adjustments.look.lut)
        value.adjustments.look.lut = std::make_shared<const LookLut>(*value.adjustments.look.lut);
    return value;
}

Result capture(const Request &request, const CancelToken &cancel) {
    Result result;
    auto fail = [&](const QString &error) { result.error = error; return result; };
    result.workerThread = QCoreApplication::instance()
        && QThread::currentThread() != QCoreApplication::instance()->thread();
    if (!result.workerThread) return fail(QStringLiteral("XMP capture requires a CPU worker"));
    if (!request.target.valid()) return fail(QStringLiteral("Missing actual XMP review target identity"));
    if (cancelled(cancel)) { result.cancelled = true; return fail(QStringLiteral("XMP read cancelled")); }
    const QFileInfo before(request.sidecarPath);
    if (!before.exists() || !before.isFile() || before.isSymLink() || !before.isReadable()
        || before.suffix().compare(QStringLiteral("xmp"), Qt::CaseInsensitive) != 0
        || before.size() <= 0 || before.size() > MaximumBytes
        || before.canonicalFilePath().isEmpty()
        || aliases(request.sidecarPath, request.protectedPhotoPath))
        return fail(QStringLiteral("Unsafe or unreadable XMP sidecar (64 MiB limit)"));
    QFile file(request.sidecarPath);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cannot open XMP sidecar"));
    ++result.fileOpens;
    if (file.isSequential() || file.size() <= 0 || file.size() > MaximumBytes)
        return fail(QStringLiteral("Unsafe or oversized XMP file"));
    QByteArray bytes;
    bytes.reserve(int(file.size()));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancelled(cancel)) { result.cancelled = true; return fail(QStringLiteral("XMP read cancelled")); }
        const QByteArray chunk = file.read(128 * 1024);
        if (chunk.isEmpty()) return fail(QStringLiteral("Incomplete XMP read"));
        result.bytesRead += chunk.size();
        if (result.bytesRead > MaximumBytes) return fail(QStringLiteral("XMP exceeds 64 MiB"));
        hash.addData(chunk);
        bytes.append(chunk);
    }
    if (file.error() != QFileDevice::NoError || result.bytesRead != before.size()
        || !sameObservedFile(before, QFileInfo(request.sidecarPath)))
        return fail(QStringLiteral("XMP changed during capture; review it again"));
    XmpSidecar::Document parsed;
    if (!XmpSidecar::readBytes(bytes, &parsed, &result.error, cancel.get())) {
        result.cancelled = cancelled(cancel);
        return result;
    }
    if (cancelled(cancel)) { result.cancelled = true; return fail(QStringLiteral("XMP read cancelled")); }
    auto data = std::make_shared<Captured::Data>();
    data->target = request.target;
    data->locator = before.canonicalFilePath();
    data->bytes = std::move(bytes);
    data->digest = hash.result().toHex();
    data->parsed = std::move(parsed);
    result.sidecar.m_data = std::move(data);
    return result;
}

Job::Job(Deliver deliver)
    : m_deliver(std::move(deliver)),
      m_job([](const Request &request, const CancelToken &token) { return capture(request, token); },
            [this](const Request &request, Result result) {
                if (m_retired || !m_active || !(request.target == m_current)) return;
                if (result.sidecar.valid() && !(result.sidecar.target() == m_current)) return;
                m_active = false;
                if (m_deliver) m_deliver(request, std::move(result));
            }) {}
bool Job::submit(Request request) {
    if (m_retired || !request.target.valid() || request.target.sequence <= m_lastSequence) return false;
    m_lastSequence = request.target.sequence;
    m_current = request.target;
    m_active = true;
    m_job.submit(std::move(request));
    return true;
}
void Job::cancel() { m_active = false; m_job.cancel(); }
void Job::retire() { m_retired = true; cancel(); }
}
