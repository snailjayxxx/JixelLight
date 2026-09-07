#pragma once
#include <QImage>
#include <QJsonObject>
#include <QVector>
#include <array>
#include <memory>

// Immutable after construction. LUT domain/output: normalized display sRGB.
// Values use .cube ordering: red fastest, then green, then blue.
class LookLut {
public:
    int size=0;
    QString title;
    QVector<float> rgb;
    QJsonObject evidence;
    QString digest;
    std::array<float,3> sample(float r,float g,float b) const;
    QImage atlas() const;
    bool validate(QString *error=nullptr) const;
    QJsonObject toJson() const;
    void updateDigest();
    static std::shared_ptr<const LookLut> fromJson(const QJsonObject &json,QString *error=nullptr);
    static std::shared_ptr<const LookLut> fromCube(const QByteArray &text,QString *error=nullptr);
    static std::shared_ptr<const LookLut> identity(int size=17);
};
