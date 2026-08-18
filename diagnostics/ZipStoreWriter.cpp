#include "diagnostics/ZipStoreWriter.h"
#include <QDateTime>
#include <algorithm>

ZipStoreWriter::ZipStoreWriter(const QString &filePath) : m_file(filePath) {}
bool ZipStoreWriter::open() { return m_file.open(QIODevice::WriteOnly | QIODevice::Truncate); }
void ZipStoreWriter::put16(QByteArray &o, quint16 v) { o.append(char(v & 0xff)); o.append(char((v >> 8) & 0xff)); }
void ZipStoreWriter::put32(QByteArray &o, quint32 v) { put16(o, quint16(v & 0xffff)); put16(o, quint16((v >> 16) & 0xffff)); }
quint32 ZipStoreWriter::crc32(const QByteArray &data) {
    quint32 crc = 0xffffffffu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int i=0;i<8;++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool ZipStoreWriter::addFile(const QString &name, const QByteArray &data) {
    if (!m_file.isOpen()) return false;
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date(); const QTime t = now.time();
    Entry e; e.name = name.toUtf8(); e.crc = crc32(data); e.size = quint32(data.size()); e.offset = quint32(m_file.pos());
    e.time = quint16((t.hour()<<11) | (t.minute()<<5) | (t.second()/2));
    e.date = quint16(((std::max(1980,d.year())-1980)<<9) | (d.month()<<5) | d.day());
    QByteArray h; put32(h,0x04034b50); put16(h,20); put16(h,0); put16(h,0); put16(h,e.time); put16(h,e.date);
    put32(h,e.crc); put32(h,e.size); put32(h,e.size); put16(h,quint16(e.name.size())); put16(h,0); h += e.name;
    if (m_file.write(h) != h.size() || m_file.write(data) != data.size()) return false;
    m_entries.push_back(e); return true;
}

bool ZipStoreWriter::close() {
    if (!m_file.isOpen()) return false;
    const quint32 centralOffset = quint32(m_file.pos());
    for (const auto &e : m_entries) {
        QByteArray h; put32(h,0x02014b50); put16(h,20); put16(h,20); put16(h,0); put16(h,0); put16(h,e.time); put16(h,e.date);
        put32(h,e.crc); put32(h,e.size); put32(h,e.size); put16(h,quint16(e.name.size())); put16(h,0); put16(h,0); put16(h,0); put16(h,0); put32(h,0); put32(h,e.offset); h += e.name;
        if (m_file.write(h) != h.size()) return false;
    }
    const quint32 centralSize = quint32(m_file.pos()) - centralOffset;
    QByteArray end; put32(end,0x06054b50); put16(end,0); put16(end,0); put16(end,quint16(m_entries.size())); put16(end,quint16(m_entries.size()));
    put32(end,centralSize); put32(end,centralOffset); put16(end,0);
    const bool ok = m_file.write(end) == end.size(); m_file.close(); return ok;
}
