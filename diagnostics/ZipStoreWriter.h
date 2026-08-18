#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <vector>

class ZipStoreWriter {
public:
    explicit ZipStoreWriter(const QString &filePath);
    bool open();
    bool addFile(const QString &name, const QByteArray &data);
    bool close();

private:
    struct Entry { QByteArray name; quint32 crc = 0; quint32 size = 0; quint32 offset = 0; quint16 time = 0; quint16 date = 0; };
    QFile m_file;
    std::vector<Entry> m_entries;
    static quint32 crc32(const QByteArray &data);
    static void put16(QByteArray &out, quint16 v);
    static void put32(QByteArray &out, quint32 v);
};
