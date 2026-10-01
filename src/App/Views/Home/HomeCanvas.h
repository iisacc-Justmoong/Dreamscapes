#pragma once
#include <iiSharedCanvas/QtAdapter/CanvasItem.h>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Consumer-owned bridge. Canvas pixels, editing and history remain in iiSharedCanvas.
class HomeCanvas : public iiSharedCanvas::CanvasItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList attachments READ attachments NOTIFY attachmentsChanged)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY contentChanged)
    Q_PROPERTY(QString inputError READ inputError NOTIFY inputErrorChanged)
public:
    explicit HomeCanvas(QQuickItem *parent=nullptr);
    QVariantList attachments() const { return m_attachments; }
    bool hasContent() const;
    QString inputError() const { return m_error; }
    Q_INVOKABLE bool addAttachment(const QUrl &source);
    Q_INVOKABLE bool removeAttachment(int index);
    Q_INVOKABLE bool setAspectRatio(const QString &ratio);
    Q_INVOKABLE bool pasteAttachment(const QUrl &source,qreal itemX,qreal itemY);
    Q_INVOKABLE QVariantMap generationParameters(const QString &prompt,const QString &model,
                                                 const QString &ratio,int count);
signals:
    void attachmentsChanged();
    void contentChanged();
    void inputErrorChanged();
protected:
    void wheelEvent(QWheelEvent *event) override;
private:
    bool fail(const QString &message);
    QVariantList m_attachments;
    QString m_error;
    QTemporaryDir m_snapshots;
};
