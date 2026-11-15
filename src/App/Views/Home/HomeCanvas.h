#include <iiFileProviderDevice.h>
#pragma once
#include "App/Generation/HomeGenerationInputs.h"
#include "App/Generation/HomeReferenceStore.h"
#include <iiSharedCanvas/QtAdapter/CanvasItem.h>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Consumer-owned bridge. Canvas pixels, editing and history remain in iiSharedCanvas.
class HomeCanvas : public iiSharedCanvas::CanvasItem, public dreamscapes::HomeGenerationInputs {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList attachments READ attachments NOTIFY attachmentsChanged)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY contentChanged)
    Q_PROPERTY(QString inputError READ inputError NOTIFY inputErrorChanged)
public:
    explicit HomeCanvas(QQuickItem *parent=nullptr);
    ~HomeCanvas() override;
    QVariantList attachments() const override { return m_references.attachments(); }
    bool hasContent() const override;
    QString inputError() const override { return m_error; }
    Q_INVOKABLE bool addAttachment(const QUrl &source) override;
    Q_INVOKABLE bool removeAttachment(int index);
    Q_INVOKABLE bool setAspectRatio(const QString &ratio);
    Q_INVOKABLE bool pasteAttachment(const QUrl &source,qreal itemX,qreal itemY);
    Q_INVOKABLE QVariantMap generationParameters(const QString &prompt,const QString &model,
                                                 const QString &ratio,int count) override;
signals:
    void attachmentsChanged();
    void contentChanged();
    void inputErrorChanged();
protected:
    void wheelEvent(QWheelEvent *event) override;
private:
    bool fail(const QString &message);
    dreamscapes::HomeReferenceStore m_references;
    QString m_error;
    iiFileProvider::TemporaryDirectory m_snapshots;
};
