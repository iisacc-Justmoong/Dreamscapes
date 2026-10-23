#include "EditorAudioPlayback.h"
#include "TimelineAudioMix.h"
#include <QAudioDevice>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>
#include <QPointer>
#include <QtConcurrentRun>
#include <bit>
#include <exception>
#include <limits>

namespace {
class QtAudioOutput final : public QObject,public EditorAudioOutput {
public:
    ~QtAudioOutput() override {setCallback({});stop();}
    std::string start(qulonglong id,std::shared_ptr<const EditorAudioBuffer> buffer) override {
        stop();QAudioFormat format;format.setSampleRate(int(buffer->audio->sampleRate));
        format.setChannelCount(buffer->audio->channelCount);format.setSampleFormat(QAudioFormat::Int16);
        const auto device=QMediaDevices::defaultAudioOutput();
        if(device.isNull()||!device.isFormatSupported(format))
            return "The current audio output does not support this track's sample rate and channels.";
        m_audio=std::move(buffer);
        m_pcm.setData(QByteArray::fromRawData(m_audio->data(),qsizetype(m_audio->byteSize())));
        if(!m_pcm.open(QIODevice::ReadOnly)){m_audio.reset();return "Could not open the prepared audio buffer.";}
        m_sink=std::make_unique<QAudioSink>(device,format);const auto generation=m_generation;
        connect(m_sink.get(),&QAudioSink::stateChanged,this,[this,id,generation](QAudio::State state) {
            // Never destroy a sink or its buffer inside its own start/state stack.
            QMetaObject::invokeMethod(this,[this,id,generation,state] {
                if(generation!=m_generation||!m_sink)return;
                if(state==QAudio::ActiveState) {
                    if(!m_started){m_started=true;notify(id,State::Started);}
                } else if(state==QAudio::IdleState&&m_pcm.atEnd())notify(id,State::Finished);
                else if(state==QAudio::StoppedState&&m_sink->error()!=QAudio::NoError)
                    notify(id,State::Failed,"The audio output stopped before completing playback.");
            },Qt::QueuedConnection);
        });
        m_sink->start(&m_pcm);return {};
    }
    void stop() override {
        ++m_generation;m_started=false;
        if(m_sink){disconnect(m_sink.get(),nullptr,this,nullptr);m_sink->stop();m_sink.reset();}
        m_pcm.close();m_pcm.setData({});m_audio.reset();
    }
    qint64 processedUsecs() const override {return m_sink?m_sink->processedUSecs():0;}
private:
    std::unique_ptr<QAudioSink> m_sink;
    QBuffer m_pcm;
    std::shared_ptr<const EditorAudioBuffer> m_audio;
    qulonglong m_generation=0;
    bool m_started=false;
};
}
const char *EditorAudioBuffer::data() const {
    return !portableBytes.empty()?portableBytes.data():audio?reinterpret_cast<const char *>(audio->samples.data()):nullptr;
}
std::size_t EditorAudioBuffer::byteSize() const {
    return !portableBytes.empty()?portableBytes.size():audio?audio->samples.size()*sizeof(std::int16_t):0;
}
EditorAudioPlayback::EditorAudioPlayback(QObject *parent)
    :EditorAudioPlayback(prepareAudio,std::make_unique<QtAudioOutput>(),parent) {}
EditorAudioPlayback::EditorAudioPlayback(PrepareOperation prepare,std::unique_ptr<EditorAudioOutput> output,QObject *parent)
    :QObject(parent),m_prepare(std::move(prepare)),m_output(std::move(output)) {
    connect(&m_completion,&QFutureWatcher<std::shared_ptr<const EditorAudioBuffer>>::finished,this,&EditorAudioPlayback::complete);
    m_startDeadline.setSingleShot(true);m_startDeadline.setInterval(5000);
    connect(&m_startDeadline,&QTimer::timeout,this,[this]{if(m_preparing)failCurrent(m_id,tr("The audio output did not start in time."));});
    if(m_output)m_output->setCallback([this](qulonglong id,EditorAudioOutput::State state,std::string error){outputEvent(id,state,std::move(error));});
}
EditorAudioPlayback::~EditorAudioPlayback() {
    disconnect(&m_completion,nullptr,this,nullptr);disconnect(&m_startDeadline,nullptr,this,nullptr);
    m_cancel.request_stop();m_startDeadline.stop();
    if(m_output){m_output->setCallback({});m_output->stop();}
}
qint64 EditorAudioPlayback::processedUsecs() const {return m_playing&&m_output?std::max<qint64>(0,m_output->processedUsecs()):0;}
void EditorAudioPlayback::clearOutput() {m_startDeadline.stop();if(m_output)m_output->stop();}
qulonglong EditorAudioPlayback::play(EditorAudioRequest request) {
    if(!request.document||!m_prepare||!m_output) {
        const auto message=tr("Audio playback is unavailable for this request.");
        m_rejection=message;emit rejected(message);return 0;
    }
    const auto previous=m_id;
    if(++m_nextId==0)++m_nextId;
    const auto id=m_nextId;
    m_id=id;m_preparing=true;m_playing=false;m_error.clear();m_rejection.clear();
    m_cancel.request_stop();clearOutput();m_pending=Job{id,std::move(request)};
    launchPending();const auto owner=QPointer<EditorAudioPlayback>(this);
    if(previous)emit cancelled(previous);
    if(owner&&m_id==id)emit changed();
    return id;
}
void EditorAudioPlayback::stop() {
    const auto previous=m_id;m_id=0;m_preparing=false;m_playing=false;m_pending.reset();m_cancel.request_stop();clearOutput();
    if(!previous)return;
    const auto owner=QPointer<EditorAudioPlayback>(this);emit cancelled(previous);
    if(owner&&m_id==0)emit changed();
}
bool EditorAudioPlayback::stopRequest(qulonglong id) {if(!id||m_id!=id)return false;stop();return true;}
void EditorAudioPlayback::launchPending() {
    if(m_workerActive||!m_pending)return;
    auto job=std::move(*m_pending);m_pending.reset();m_runningId=job.id;m_workerActive=true;
    m_cancel=std::stop_source{};const auto token=m_cancel.get_token();
    m_completion.setFuture(QtConcurrent::run([prepare=m_prepare,request=std::move(job.request),token] {
        try{return std::make_shared<const EditorAudioBuffer>(prepare(request,token));}
        catch(const std::exception &error){return std::make_shared<const EditorAudioBuffer>(EditorAudioBuffer{{},{},error.what()});}
        catch(...){return std::make_shared<const EditorAudioBuffer>(EditorAudioBuffer{{},{},"Could not prepare audio playback."});}
    }));
}
void EditorAudioPlayback::complete() {
    const auto result=m_completion.result();const auto id=m_runningId;m_workerActive=false;
    const auto owner=QPointer<EditorAudioPlayback>(this);
    if(id==m_id&&m_preparing) {
        if(!result||!result->error.empty()||!result->audio||result->audio->samples.empty()
            ||!result->audio->channelCount||!result->audio->sampleRate||result->audio->sampleRate>std::uint32_t(std::numeric_limits<int>::max())
            ||result->audio->samples.size()%result->audio->channelCount||result->byteSize()>std::size_t(std::numeric_limits<qsizetype>::max()))
            failCurrent(id,result&&!result->error.empty()?QString::fromStdString(result->error):tr("The audio request returned no playable PCM data."));
        else {
            m_startDeadline.start();std::string error;
            try{error=m_output->start(id,result);}catch(const std::exception &e){error=e.what();}catch(...){error="Could not start the audio output.";}
            if(!owner)return;
            if(m_id==id&&!error.empty())failCurrent(id,QString::fromStdString(error));
        }
    }
    if(owner)launchPending();
}
void EditorAudioPlayback::failCurrent(qulonglong id,const QString &error) {
    if(!id||m_id!=id)return;
    const auto message=error.isEmpty()?tr("Could not play the requested audio."):error;
    m_id=0;m_preparing=false;m_playing=false;m_error=message;clearOutput();
    const auto owner=QPointer<EditorAudioPlayback>(this);emit changed();if(owner)emit failed(id,message);
}
void EditorAudioPlayback::outputEvent(qulonglong id,EditorAudioOutput::State state,std::string error) {
    if(!id||m_id!=id)return;
    if(state==EditorAudioOutput::State::Started) {
        if(!m_preparing||m_playing)return;
        m_preparing=false;m_playing=true;m_startDeadline.stop();
        const auto owner=QPointer<EditorAudioPlayback>(this);emit changed();if(owner&&m_id==id&&m_playing)emit started(id);
    } else if(state==EditorAudioOutput::State::Failed)failCurrent(id,QString::fromStdString(error));
    else if(!m_playing)failCurrent(id,tr("The audio output ended before playback could start."));
    else {
        m_id=0;m_preparing=false;m_playing=false;clearOutput();
        const auto owner=QPointer<EditorAudioPlayback>(this);emit changed();if(owner)emit finished(id);
    }
}
EditorAudioBuffer EditorAudioPlayback::prepareAudio(const EditorAudioRequest &request,std::stop_token cancelled) {
    using namespace iiSharedCanvas;
    if(!request.document)return {{},{},"Open a canvas before preparing audio."};
    if(cancelled.stop_requested())return {{},{},"Audio preparation cancelled."};
    EditorAudioBuffer result;
    if(request.assetId) {
        const auto *asset=findAudioAsset(*request.document,*request.assetId);
        if(!asset)return {{},{},"The selected audio asset no longer exists."};
        result.audio=std::shared_ptr<const AudioAsset>(request.document,asset);
    } else {
        auto mixed=dreamscapes::mixTimelineAudio(*request.document,request.firstFrame,cancelled);
        if(!mixed.ok())return {{},{},mixed.result.message.empty()?"Could not mix the audio timeline.":mixed.result.message};
        result.audio=std::make_shared<const AudioAsset>(std::move(mixed.asset));
    }
    if(result.audio->samples.empty()||!result.audio->channelCount||!result.audio->sampleRate
        ||result.audio->sampleRate>std::uint32_t(std::numeric_limits<int>::max())
        ||result.audio->samples.size()%result.audio->channelCount)
        return {{},{},"The audio request returned no playable PCM data."};
    if constexpr(std::endian::native!=std::endian::little) {
        result.portableBytes.resize(result.audio->samples.size()*2);
        for(std::size_t i=0;i<result.audio->samples.size();++i) {
            if(i%8192==0&&cancelled.stop_requested())return {{},{},"Audio preparation cancelled."};
            const auto value=std::uint16_t(result.audio->samples[i]);result.portableBytes[i*2]=char(value&255);result.portableBytes[i*2+1]=char(value>>8);
        }
    }
    return result;
}
