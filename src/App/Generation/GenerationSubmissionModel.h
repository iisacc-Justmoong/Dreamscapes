#pragma once
#include <QObject>
#include <QPointer>
#include <QStringList>
class GenerationController;
class AdvancedImageParameters;
class VideoDraftModel;
class GenerationSubmissionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *generation READ generation WRITE setGeneration NOTIFY bindingsChanged)
    Q_PROPERTY(QObject *imageParameters READ imageParameters WRITE setImageParameters NOTIFY bindingsChanged)
    Q_PROPERTY(QObject *videoParameters READ videoParameters WRITE setVideoParameters NOTIFY bindingsChanged)
    Q_PROPERTY(QString mediaType READ mediaType WRITE setMediaType NOTIFY bindingsChanged)
    Q_PROPERTY(QStringList jobIds READ jobIds WRITE setJobIds NOTIFY changed)
    Q_PROPERTY(bool submitting READ submitting NOTIFY changed)
    Q_PROPERTY(bool invalidInputSubmission READ invalidInputSubmission NOTIFY changed)
    Q_PROPERTY(QString requestError READ requestError WRITE setRequestError NOTIFY changed)
    Q_PROPERTY(QString selectedResultId READ selectedResultId WRITE setSelectedResultId NOTIFY changed)
    Q_PROPERTY(QString selectedImageSource READ selectedImageSource WRITE setSelectedImageSource NOTIFY changed)
  public:
    explicit GenerationSubmissionModel(QObject *parent = nullptr) : QObject(parent) {}
    QObject *generation() const;
    void setGeneration(QObject *);
    QObject *imageParameters() const;
    void setImageParameters(QObject *);
    QObject *videoParameters() const;
    void setVideoParameters(QObject *);
    QString mediaType() const {
        return mediaType_;
    }
    void setMediaType(QString value) {
        if (mediaType_ != value) {
            mediaType_ = std::move(value);
            emit bindingsChanged();
        }
    }
    QStringList jobIds() const {
        return ids_;
    }
    void setJobIds(QStringList value) {
        if (ids_ != value) {
            ids_ = std::move(value);
            emit changed();
        }
    }
    bool submitting() const {
        return submitting_;
    }
    bool invalidInputSubmission() const {
        return invalid_;
    }
    QString requestError() const {
        return error_;
    }
    void setRequestError(QString value) {
        if (error_ != value) {
            error_ = std::move(value);
            emit changed();
        }
    }
    QString selectedResultId() const {
        return result_;
    }
    void setSelectedResultId(QString value) {
        if (result_ != value) {
            result_ = std::move(value);
            emit changed();
        }
    }
    QString selectedImageSource() const {
        return source_;
    }
    void setSelectedImageSource(QString value) {
        if (source_ != value) {
            source_ = std::move(value);
            emit changed();
        }
    }
    Q_INVOKABLE bool submit();
    Q_INVOKABLE void cancelBatch();
  signals:
    void bindingsChanged();
    void changed();
    void selectionResetRequested();

  private:
    void clearCorrectedInputError();
    QPointer<GenerationController> generation_;
    QPointer<AdvancedImageParameters> image_;
    QPointer<VideoDraftModel> video_;
    QString mediaType_ = QStringLiteral("Image"), error_, result_, source_;
    QStringList ids_;
    bool submitting_ = false, invalid_ = false;
};
