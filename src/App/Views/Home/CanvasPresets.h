#pragma once
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// QML adapter for the bundled design catalogue and canvas input validation.
class CanvasPresets : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    Q_PROPERTY(int count READ count CONSTANT)
public:
    explicit CanvasPresets(QObject *parent = nullptr);
    QVariantList categories() const { return m_categories; }
    int count() const { return m_presets.size(); }
    Q_INVOKABLE QVariantList sections(int category, const QString &query = {}) const;
    Q_INVOKABLE QVariantMap preset(const QString &id) const;
    Q_INVOKABLE QVariantMap specification(double width, double height, const QString &unit,
                                          double ppi = 300, const QString &background = "White") const;
    Q_INVOKABLE double convert(double value, const QString &from, const QString &to, double ppi) const;
private:
    QVariantList m_categories;
    QVariantList m_presets;
};
