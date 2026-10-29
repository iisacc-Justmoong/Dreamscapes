#pragma once
#include "../../ViewModels/Native/ImageSelection.h"
#include "../Result/GenerationReviewModel.h"
#include <QPointer>
class ImageSelectionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *backend READ backend WRITE setBackend NOTIFY bindingsChanged)
    Q_PROPERTY(QVariantList entries READ entries WRITE setEntries NOTIFY bindingsChanged)
    Q_PROPERTY(bool selectionMode READ selectionMode WRITE setSelectionMode NOTIFY changed)
    Q_PROPERTY(QStringList selectedKeys READ selectedKeys WRITE setSelectedKeys NOTIFY changed)
    Q_PROPERTY(QString anchorKey READ anchorKey WRITE setAnchorKey NOTIFY changed)
    Q_PROPERTY(bool toggleOnClick READ toggleOnClick WRITE setToggleOnClick NOTIFY changed)
    Q_PROPERTY(QVariantList images READ images NOTIFY changed)
    Q_PROPERTY(QVariantList selectedImages READ selectedImages NOTIFY changed)
  public:
    explicit ImageSelectionModel(QObject *parent = nullptr) : QObject(parent) {
        refresh();
    }
    QObject *backend() const {
        return backend_;
    }
    void setBackend(QObject *);
    QVariantList entries() const {
        return entries_;
    }
    void setEntries(const QVariantList &);
    bool selectionMode() const;
    void setSelectionMode(bool);
    QStringList selectedKeys() const;
    void setSelectedKeys(const QStringList &);
    QString anchorKey() const;
    void setAnchorKey(const QString &);
    bool toggleOnClick() const {
        return toggle_;
    }
    void setToggleOnClick(bool value) {
        if (toggle_ != value) {
            toggle_ = value;
            emit changed();
        }
    }
    QVariantList images() const {
        return state_.value("images").toList();
    }
    QVariantList selectedImages() const;
    Q_INVOKABLE bool isSelected(const QVariant &) const;
    Q_INVOKABLE void toggle(const QVariant &);
    Q_INVOKABLE void selectAt(int index, int modifiers, int previous = -1);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void toggleMode();
  signals:
    void bindingsChanged();
    void changed();

  private:
    void refresh();
    void adoptPresentation(const dreamscapes::vm::Value &);
    void send(const dreamscapes::vm::ImageSelectionCommand &);
    QPointer<GenerationReviewModel> backend_;
    QVariantList entries_;
    QVariantMap state_;
    bool toggle_ = false;
    dreamscapes::vm::ImageSelection model_;
};
