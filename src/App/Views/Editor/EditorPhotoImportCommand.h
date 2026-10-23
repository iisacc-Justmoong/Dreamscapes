#pragma once
#include "EditorPhotoImport.h"
#include <QObject>
#include <QPointer>
#include <QFutureWatcher>
#include <QString>
#include <optional>
class EditorCanvas;
class EditorCameraPhoto;

// GUI adoption boundary for one accepted photo batch. Workers own values only.
class EditorPhotoImportCommand final : public QObject {
    Q_OBJECT
public:
    EditorPhotoImportCommand(EditorCanvas *, EditorCameraPhoto *, QObject *parent = nullptr);
    EditorPhotoImportCommand(EditorCanvas *, EditorCameraPhoto *, dreamscapes::PhotoImportOperation, QObject *parent = nullptr);
    ~EditorPhotoImportCommand() override;
private:
    struct Target {
        qulonglong token = 0, documentSequence = 0, revision = 0;
        QString path, selectedLayer;
        std::string artboard;
        iiSharedCanvas::CanvasExtent extent;
        iiSharedCanvas::FrameRate frameRate;
        std::filesystem::path storageRoot;
    };
    struct Job {
        Target target;
        dreamscapes::PhotoImportInput input;
    };
    bool matches(const Target &, bool afterApply = false) const;
    void capture(qulonglong token);
    void prepare(qulonglong token, const dreamscapes::PhotoOriginals &);
    void retire(const QString &error = {}, bool notify = true);
    void launch();
    void complete();
    QPointer<EditorCanvas> m_canvas;
    QPointer<EditorCameraPhoto> m_sources;
    dreamscapes::PhotoImportOperation m_operation;
    std::optional<Target> m_target;
    std::optional<Job> m_running, m_pending;
    QFutureWatcher<dreamscapes::PhotoImportOutput> m_completion;
    bool m_adopting = false;
};
