#pragma once
#include <iiSharedCanvas/Document/Document.h>
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

struct EditorAudioRequest {
    std::shared_ptr<const iiSharedCanvas::Document> document;
    iiSharedCanvas::FrameIndex firstFrame=0;
    std::optional<std::string> assetId; // Empty optional mixes the timeline.
};
struct EditorAudioBuffer {
    std::shared_ptr<const iiSharedCanvas::AudioAsset> audio;
    std::vector<char> portableBytes;
    std::string error;
    const char *data() const;
    std::size_t byteSize() const;
};
// GUI-thread device boundary. The native buffer is retained without a PCM copy.
// Events identify their request, including events delivered after replacement.
class EditorAudioOutput {
public:
    enum class State {Started,Finished,Failed};
    using Callback=std::function<void(qulonglong,State,std::string)>;
    virtual ~EditorAudioOutput()=default;
    virtual std::string start(qulonglong,std::shared_ptr<const EditorAudioBuffer>)=0;
    virtual void stop()=0;
    virtual qint64 processedUsecs() const=0;
    void setCallback(Callback callback){m_callback=std::move(callback);}
protected:
    void notify(qulonglong id,State state,std::string error={}) {
        const auto callback=m_callback;if(callback)callback(id,state,std::move(error));
    }
private:
    Callback m_callback;
};
class EditorAudioPlayback : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool preparing READ preparing NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(qulonglong requestId READ requestId NOTIFY changed)
public:
    using PrepareOperation=std::function<EditorAudioBuffer(const EditorAudioRequest &,std::stop_token)>;
    explicit EditorAudioPlayback(QObject *parent=nullptr);
    EditorAudioPlayback(PrepareOperation,std::unique_ptr<EditorAudioOutput>,QObject *parent);
    ~EditorAudioPlayback() override;
    bool preparing() const{return m_preparing;}
    bool playing() const{return m_playing;}
    QString error() const{return m_error;}
    QString lastRejection() const{return m_rejection;}
    qulonglong requestId() const{return m_id;}
    qint64 processedUsecs() const;
    qulonglong play(EditorAudioRequest);
    Q_INVOKABLE void stop();
    bool stopRequest(qulonglong id);
    static EditorAudioBuffer prepareAudio(const EditorAudioRequest &,std::stop_token);
signals:
    void changed();
    void started(qulonglong id);
    void finished(qulonglong id);
    void failed(qulonglong id,const QString &message);
    void cancelled(qulonglong id);
    void rejected(const QString &message);
private:
    struct Job {qulonglong id;EditorAudioRequest request;};
    void launchPending();
    void complete();
    void outputEvent(qulonglong,EditorAudioOutput::State,std::string);
    void failCurrent(qulonglong,const QString &);
    void clearOutput();
    PrepareOperation m_prepare;
    std::unique_ptr<EditorAudioOutput> m_output;
    QFutureWatcher<std::shared_ptr<const EditorAudioBuffer>> m_completion;
    QTimer m_startDeadline;
    std::optional<Job> m_pending;
    std::stop_source m_cancel;
    qulonglong m_id=0,m_nextId=0,m_runningId=0;
    bool m_workerActive=false,m_preparing=false,m_playing=false;
    QString m_error,m_rejection;
};
