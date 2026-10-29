#pragma once
#include "AlignmentGeometry.h"
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class EditorViewOptions : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantMap snap READ snap NOTIFY changed)
    Q_PROPERTY(QVariantMap grid READ grid NOTIFY changed)
    Q_PROPERTY(QVariantMap guides READ guides NOTIFY changed)
    Q_PROPERTY(QVariantMap display READ display NOTIFY changed)
    Q_PROPERTY(QString rulerUnit READ rulerUnit CONSTANT)
    Q_PROPERTY(int guideCount READ guideCount NOTIFY guidesChanged)
  public:
    explicit EditorViewOptions(QObject *parent = nullptr);
    QString rulerUnit() const { return QStringLiteral("px"); }
    QVariantMap snap() const {
        return m_snap;
    }
    QVariantMap grid() const {
        return m_grid;
    }
    QVariantMap guides() const {
        return m_guides;
    }
    QVariantMap display() const {
        return m_display;
    }
    Q_INVOKABLE bool editText(const QString &family, const QString &key, const QString &input);
    Q_INVOKABLE bool edit(const QString &family, const QString &key, const QVariant &value);
    Q_INVOKABLE void reset(const QString &family);
    Q_INVOKABLE bool preset(const QString &name);
    Q_INVOKABLE void clearGuides();
    dreamscapes::alignment::Grid gridGeometry(dreamscapes::alignment::Rect reference, double ppi) const;
    dreamscapes::alignment::Guide guideGeometry(dreamscapes::alignment::Rect reference, double ppi) const;
    struct PlacedGuide {
        dreamscapes::alignment::Line line;
        QString artboard;
    };
    const std::vector<PlacedGuide> &placedGuides() const {
        return m_placedGuides;
    }
    void addGuides(std::vector<PlacedGuide>);
    bool moveGuide(int index, dreamscapes::alignment::Point delta);
    bool removeGuide(int index);
    int guideCount() const {
        return int(m_placedGuides.size());
    }
  signals:
    void changed();
    void guidesChanged();

  private:
    QVariantMap m_snap, m_grid, m_guides, m_display, m_schema;
    std::vector<PlacedGuide> m_placedGuides;
    QVariantMap *family(const QString &);
};
