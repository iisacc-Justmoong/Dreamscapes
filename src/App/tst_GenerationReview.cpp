#include "StorageViewTypes.h"
#include "Generation/GenerationController.h"
#include "Generation/AdvancedImageParameters.h"
#include "Views/Result/GenerationReviewModel.h"
#include "Views/Editor/EditorWorkspaceModel.h"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QWheelEvent>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJSValue>
#include <QScopeGuard>
#include <SocietyDrive.h>
#include <QtTest>

namespace {
GenerationRuntime runtime;
QString presetFile;
class ReviewAdvancedParameters : public AdvancedImageParameters {
public:
    explicit ReviewAdvancedParameters(QObject* parent=nullptr):AdvancedImageParameters(presetFile,parent){}
};
class ReviewGenerationController : public GenerationController {
public:
    explicit ReviewGenerationController(QObject* parent=nullptr):GenerationController(runtime,parent){}
};
QUrl source(const QString& file) { return QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+'/'+file); }
QQuickItem* visual(QQuickItem* root,const QString& name) {
    if(root->objectName()==name)return root;
    for(auto* child:root->childItems())if(auto* result=visual(child,name))return result;
    return nullptr;
}
QVariantList values(QObject* root,const char* name) {
    const auto value=root->property(name);
    return value.metaType()==QMetaType::fromType<QJSValue>()?value.value<QJSValue>().toVariant().toList():value.toList();
}
void click(QQuickWindow& window,QQuickItem* button) {
    if(button)QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
}
QVariantList fixtures(const QString& directory,int count) {
    QVariantList entries;
    for(int i=0;i<count;++i) {
        const auto path=directory+QString("/result-%1.png").arg(i%8);
        if(!QFile::exists(path)) {
            QImage image(768,512,QImage::Format_RGB32);
            QPainter painter(&image);QLinearGradient gradient(0,0,768,512);
            gradient.setColorAt(0,QColor::fromHsv(i*42%360,100,150));gradient.setColorAt(1,QColor::fromHsv((i*42+80)%360,140,70));
            painter.fillRect(image.rect(),gradient);painter.setBrush(QColor(240,220,180));painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPoint(380,190),100+i*4,100+i*4);painter.setBrush(QColor(30,40,60));
            painter.drawRect(80,330,600,182);painter.setPen(Qt::white);painter.setFont(QFont("Helvetica",38));
            painter.drawText(image.rect().adjusted(30,30,-30,-30),Qt::AlignBottom|Qt::AlignRight,QString("Review fixture %1").arg(i+1));painter.end();image.save(path);
        }
        entries.append(QVariantMap{{"id",QString("job-%1").arg(i/4)},{"imageSource",QUrl::fromLocalFile(path)},
            {"documentSource",QUrl::fromLocalFile(path)},{"mediaType","Image"},{"image",QString("output-%1").arg(i)},
            {"width",768},{"height",512},{"seed",1000+i},{"modelName","Fixture image model"},
            {"prompt",QString("Campaign candidate %1 — review fixture").arg(i+1)},{"steps",24},{"state","completed"}});
    }
    return entries;
}
}
class GenerationReviewTests : public QObject {
    Q_OBJECT
private slots:
    void longFailureKeepsDetailsAccessible() {
        const QString error = QStringLiteral("FIRST FAILURE LINE\n")
            + QStringLiteral("오류 evidence 123\n").repeated(100) + QStringLiteral("LAST FAILURE LINE");
        const QVariantList jobs{QVariantMap{{"id", "long-failure"}, {"state", "failed"},
            {"error", error}, {"mediaType", "Video"}, {"modelName", "Fixture video model"}}};
        QQmlEngine engine;
        QQmlComponent component(&engine, source("Views/Result/GenerationResult.qml"));
        QScopedPointer<QQuickItem> view(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
            {{"jobs", jobs}, {"errorText", error}})));
        QVERIFY2(view, qPrintable(component.errorString()));
        QQuickWindow window; window.resize(1440, 900);
        view->setSize(window.size()); view->setParentItem(window.contentItem()); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* status = visual(view.data(), "resultStatus"); QVERIFY(status);
        QTRY_VERIFY(status->height() < 60);
        QVERIFY(!status->property("text").toString().contains('\n'));
        auto* queue = visual(view.data(), "reviewQueueList"); QVERIFY(queue);
        QQuickItem* details = nullptr;
        QTRY_VERIFY((details = visual(view.data(), "reviewFailure_long-failure")) && details->isVisible());
        const auto queueTop = queue->mapToScene(QPointF(0, 0)).y();
        const auto detailsTop = details->mapToScene(QPointF(0, 0)).y();
        QVERIFY(detailsTop >= queueTop);
        QVERIFY(detailsTop + details->height() <= queueTop + queue->height());
        QSignalSpy failures(view.data(), SIGNAL(failureRequested(QString)));
        click(window, details);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(failures.first().first().toString(), QStringLiteral("long-failure"));
        QCOMPARE(view->property("errorText").toString(), error);
        QCOMPARE(values(view.data(), "jobs").first().toMap().value("error").toString(), error);
    }
    void workspace_data() {
        QTest::addColumn<QSize>("size");QTest::addColumn<int>("count");
        QTest::newRow("desktop")<<QSize(1440,900)<<24;
        QTest::newRow("narrow")<<QSize(390,844)<<24;
        QTest::newRow("virtualized-1000")<<QSize(1440,900)<<1000;
    }
    void workspace() {
        QTest::failOnWarning(QRegularExpression("Binding loop detected|ReferenceError|TypeError|Cannot assign"));
        QFETCH(QSize,size);QFETCH(int,count);
        QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-fixtures-XXXXXX");QVERIFY(files.isValid());
        const auto entries=fixtures(files.path(),count);
        QQmlEngine engine;QQmlComponent component(&engine,source("Views/Result/GenerationResult.qml"));
        QVariantList queue;
        for(int i=0;i<std::min(count/4,6);++i)queue.append(QVariantMap{{"id",QString("job-%1").arg(i)},{"state","completed"},{"mediaType","Image"},{"modelName","Fixture image model"},{"images",QVariantList{1,2,3,4}}});
        queue.append(QVariantMap{{"id","active"},{"state","running"},{"mediaType","Video"},{"modelName","Fixture video model"}});
        queue.append(QVariantMap{{"id","waiting"},{"state","queued"},{"mediaType","Image"},{"modelName","Fixture image model"}});
        queue.append(QVariantMap{{"id","failed"},{"state","failed"},{"error","Fixture loader failure"},{"mediaType","Video"},{"modelName","Fixture video model"}});
        QScopedPointer<QQuickItem> view(qobject_cast<QQuickItem*>(component.createWithInitialProperties({{"results",entries},{"jobs",queue},
            {"inferenceStatus",QVariantMap{{"step",10},{"total",20},{"state","sampling"}}}})));
        QVERIFY2(view,qPrintable(component.errorString()));
        auto* model=view->findChild<GenerationReviewModel*>("generationReviewModel");QVERIFY(model);QCOMPARE(model->totalCount(),count);
        QQuickWindow window;auto* theme=engine.singletonInstance<QObject*>("LVRS","Theme");QVERIFY(theme);
        window.setColor(theme->property("window").value<QColor>());
        window.resize(size);view->setParentItem(window.contentItem());view->setSize(size);window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* gallery=visual(view.data(),"resultGallery");QVERIFY(gallery);QTRY_VERIFY(gallery->width()>200&&gallery->height()>100);
        QTRY_VERIFY(gallery->property("currentItem").value<QQuickItem*>());
        QTRY_COMPARE(gallery->property("currentItem").value<QQuickItem*>()->property("imageStatus").toInt(),1); // Image.Ready.
        auto* content=gallery->property("contentItem").value<QQuickItem*>();QVERIFY(content);
        int delegates=0;for(auto* child:content->childItems())if(child->objectName().startsWith("resultImageTile"))++delegates;
        qInfo()<<"Mounted review delegates"<<delegates<<"of"<<count;
        QVERIFY2(delegates>0&&delegates<100,qPrintable(QString("Mounted delegates: %1 / %2").arg(delegates).arg(count)));
        auto* center=visual(view.data(),"reviewResultsArea");QVERIFY(center);
        QCOMPARE(gallery->width(),center->width());
        auto* selection=visual(view.data(),"resultImageSelection");QVERIFY(selection);
        click(window,visual(selection,"resultImageSelectionToggle"));QVERIFY(selection->property("toggleOnClick").toBool());
        gallery->forceActiveFocus();QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(!model->selectionMode());QVERIFY(!selection->property("toggleOnClick").toBool());
        if(size.width()>=1180) {
            QVERIFY(visual(view.data(),"reviewQueuePanel")->isVisible());QVERIFY(visual(view.data(),"reviewInspectorPanel")->isVisible());
            QCOMPARE(visual(view.data(),"reviewQueuePanel")->width(),260.);QCOMPARE(visual(view.data(),"reviewInspectorPanel")->width(),320.);
        } else {
            QVERIFY(!visual(view.data(),"reviewQueuePanel")->isVisible());QVERIFY(!visual(view.data(),"reviewInspectorPanel")->isVisible());
            click(window,visual(view.data(),"reviewQueueButton"));QVERIFY(view->property("queueOpen").toBool());
            QVERIFY(QMetaObject::invokeMethod(view.data(),"goBack"));
        }
        gallery->forceActiveFocus();QTest::keyClick(&window,Qt::Key_Right);QCOMPARE(model->focusedIndex(),1);
        QTest::keyClick(&window,Qt::Key_F);QCOMPARE(model->shortlistedCount(),1);
        model->selectAt(1);model->selectAt(3,Qt::ControlModifier);const auto selected=model->selectedEntries();const auto focused=model->focusedKey();
        model->setFilter("shortlisted");QCOMPARE(model->selectedCount(),2);QCOMPARE(model->hiddenSelectedCount(),1);
        QVERIFY(view->property("canSaveImage").toBool()); // File commands do not depend on a visible thumbnail.
        gallery->forceActiveFocus();QTest::keyClick(&window,Qt::Key_Space);QVERIFY(view->property("detailVisible").toBool());QCOMPARE(model->focusedKey(),focused);
        QTest::keyClick(&window,Qt::Key_Space);QVERIFY(!view->property("detailVisible").toBool());QTest::qWait(1);
        model->setFilter("all");QTest::qWait(1);gallery->setProperty("restoreSavedPosition",false);
        gallery->setProperty("contentY",std::max(0.,std::min(gallery->property("cellHeight").toReal()*2,
            gallery->property("contentHeight").toReal()-gallery->height())));
        const auto scroll=gallery->property("contentY").toReal();
        QVERIFY(gallery->property("hasPresented").toBool());
        model->setFilter("shortlisted");model->setFilter("all");QTest::qWait(1);
        QTRY_VERIFY(qAbs(gallery->property("contentY").toReal()-scroll)<1);
        auto next=entries;auto extra=entries.first().toMap();extra["id"]="arrived-later";extra["image"]="extra";next.append(extra);
        QVERIFY(view->setProperty("results",next));QTRY_COMPARE(model->totalCount(),count+1);
        QTRY_VERIFY2(qAbs(gallery->property("contentY").toReal()-scroll)<1,qPrintable(QString("Scroll %1 → %2, content %3, viewport %4")
            .arg(scroll).arg(gallery->property("contentY").toReal()).arg(gallery->property("contentHeight").toReal()).arg(gallery->height())));
        QCOMPARE(model->selectedEntries(),selected);QCOMPARE(model->focusedKey(),focused);
        gallery->forceActiveFocus();QTest::keyClick(&window,Qt::Key_C);QVERIFY(view->property("compareVisible").toBool());
        auto* compare=visual(view.data(),"reviewCompare");QVERIFY(compare&&compare->isVisible());
        click(window,visual(compare,"reviewCompareActual"));QCOMPARE(compare->property("zoom").toReal(),1.);
        auto* first=visual(compare,"reviewCompareMedia0");auto* second=visual(compare,"reviewCompareMedia1");QVERIFY(first&&second);
        QCOMPARE(first->property("zoom").toReal(),1.);QCOMPARE(second->property("zoom").toReal(),1.);
        QVERIFY(QMetaObject::invokeMethod(first,"panRequested",Q_ARG(qreal,.4),Q_ARG(qreal,-.2)));
        QCOMPARE(second->property("panX").toReal(),.4);QCOMPARE(second->property("panY").toReal(),-.2);
        click(window,visual(compare,"reviewCompareSync"));QVERIFY(!compare->property("syncView").toBool());
        QVERIFY(QMetaObject::invokeMethod(first,"zoomRequested",Q_ARG(qreal,2.)));
        QCOMPARE(first->property("zoom").toReal(),2.);QCOMPARE(second->property("zoom").toReal(),1.);
        if(size.width()<700) {
            auto* tools=visual(compare,"reviewCompareToolsViewport");QVERIFY(tools);
            tools->setProperty("contentX",tools->property("contentWidth").toReal()-tools->width());
        }
        click(window,visual(compare,"reviewCompareClose"));QVERIFY(!view->property("compareVisible").toBool());
        QSignalSpy opened(view.data(),SIGNAL(imagesRequested(QVariant)));
        gallery->forceActiveFocus();QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(opened.count(),1);
        const auto payload=opened.first().first();
        QCOMPARE(payload.metaType()==QMetaType::fromType<QJSValue>()?payload.value<QJSValue>().toVariant().toList():payload.toList(),selected);
        // Inspectable text owns key input; F/X must not change review state.
        if(size.width()<1180)click(window,visual(view.data(),"reviewInspectorButton"));
        auto* prompt=visual(view.data(),"reviewInspectorPrompt");QVERIFY(prompt);
        prompt->forceActiveFocus();const auto statuses=model->shortlistedCount();QTest::keyClick(&window,Qt::Key_F);QCOMPARE(model->shortlistedCount(),statuses);
        if(size.width()<1180)QVERIFY(QMetaObject::invokeMethod(view.data(),"goBack"));
        model->clearSelection();for(int i=0;i<4;++i)model->toggle(entries[i].toMap());model->setReview("shortlisted");
        if(size.width()>=1180) {
            QSignalSpy canceled(view.data(),SIGNAL(cancelJobRequested(QString)));
            click(window,visual(view.data(),"reviewCancelJob_active"));QCOMPARE(canceled.count(),1);QCOMPARE(canceled.first().first().toString(),QString("active"));
            QSignalSpy failures(view.data(),SIGNAL(failureRequested(QString)));
            auto* list=visual(view.data(),"reviewQueueList");QVERIFY(list);QVERIFY(QMetaObject::invokeMethod(list,"positionViewAtEnd"));QTest::qWait(1);
            click(window,visual(view.data(),"reviewFailure_failed"));QCOMPARE(failures.count(),1);QCOMPARE(failures.first().first().toString(),QString("failed"));
            click(window,visual(view.data(),"reviewAllJobsButton"));QCOMPARE(model->selectedCount(),4);
            QVERIFY(QMetaObject::invokeMethod(list,"positionViewAtBeginning"));
        }
        const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
        if(!capture.isEmpty()) {QVERIFY(QDir().mkpath(capture));gallery->setProperty("contentY",0);QTest::qWait(150);QVERIFY(window.grabWindow().save(capture+"/review-"+QTest::currentDataTag()+".png"));}
        gallery->forceActiveFocus();QTest::keyClick(&window,Qt::Key_C);
        QVERIFY(view->property("compareVisible").toBool());QCOMPARE(values(compare,"entries").size(),4);
        auto* fourth=visual(compare,"reviewCompareMedia3");QVERIFY(fourth);
        auto* media=visual(compare,"reviewCompareMedia0");QVERIFY(media);QTRY_COMPARE(media->property("imageStatus").toInt(),1);
        const auto point=media->mapToScene(media->boundingRect().center());
        QWheelEvent wheel(point,window.mapToGlobal(point.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(&window,&wheel);QTRY_VERIFY(media->property("zoom").toReal()>0);
        // Use a sufficiently enlarged preview to exercise real pointer panning.
        QVERIFY(QMetaObject::invokeMethod(media,"zoomRequested",Q_ARG(qreal,2.)));
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,point.toPoint());
        QTest::mouseMove(&window,(point+QPointF(20,10)).toPoint(),30);
        QTest::mouseMove(&window,(point+QPointF(35,20)).toPoint(),30);
        QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,(point+QPointF(35,20)).toPoint());
        QTRY_VERIFY(media->property("panX").toReal()!=0||media->property("panY").toReal()!=0);
        QVERIFY(QMetaObject::invokeMethod(compare,"resetView",Q_ARG(QVariant,0)));
        if(!capture.isEmpty() && count==24 && size.width()>=1180) {
            QTest::qWait(100);QVERIFY(window.grabWindow().save(capture+"/review-compare-desktop.png"));
        }
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(!view->property("compareVisible").toBool());QCOMPARE(model->selectedCount(),4);
    }
    void acceptedSubmissionsRoute_data() {
        QTest::addColumn<int>("kind");QTest::newRow("home-image")<<0;QTest::newRow("home-video")<<1;
        QTest::newRow("advanced-image")<<2;QTest::newRow("advanced-video")<<3;
    }
    void videoPlaybackErrorReportsTheActualFailure() {
        QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-video-error-XXXXXX");QVERIFY(files.isValid());
        const auto poster=fixtures(files.path(),1).first().toMap().value("imageSource");
        QFile invalid(files.filePath("invalid.mp4"));QVERIFY(invalid.open(QIODevice::WriteOnly));invalid.write("invalid fixture video");invalid.close();
        QQmlEngine engine;QQmlComponent component(&engine,source("Views/Result/GenerationResult.qml"));
        QScopedPointer<QQuickItem> view(qobject_cast<QQuickItem*>(component.create()));QVERIFY2(view,qPrintable(component.errorString()));
        QSignalSpy failures(view.data(),SIGNAL(operationFailed(QString,QString,QVariant)));
        QQuickWindow window;window.resize(1440,900);view->setSize(window.size());view->setParentItem(window.contentItem());window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto video=QVariantMap{{"id","invalid-video"},{"mediaType","Video"},{"imageSource",poster},
            {"mediaSource",QUrl::fromLocalFile(invalid.fileName())},{"width",768},{"height",512}};
        QVERIFY(view->setProperty("results",QVariantList{video}));
        QTRY_VERIFY_WITH_TIMEOUT(failures.count()>0,10000);
        QCOMPARE(failures.first()[0].toString(),QString("Preview video"));QVERIFY(!failures.first()[1].toString().isEmpty());
        QCOMPARE(view->property("saveFeedback").toString(),failures.last()[1].toString());
        QVERIFY(invalid.open(QIODevice::ReadOnly));QCOMPARE(invalid.readAll(),QByteArray("invalid fixture video"));
    }
    void largePreviewKeepsItsDecodeBudgetAndOriginal() {
        QTemporaryDir folder(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-preview-XXXXXX");QVERIFY(folder.isValid());
        const auto path=folder.filePath("original.png");QImage original(3000,1500,QImage::Format_RGB32);original.fill(Qt::darkCyan);QVERIFY(original.save(path));
        QQmlEngine engine;QQmlComponent component(&engine,source("Views/Result/ReviewMediaView.qml"));
        QScopedPointer<QQuickItem> media(qobject_cast<QQuickItem*>(component.createWithInitialProperties({{"source",QUrl::fromLocalFile(path)},
            {"interactive",true},{"pixelWidth",3000},{"pixelHeight",1500},{"zoom",1}})));QVERIFY2(media,qPrintable(component.errorString()));
        QQuickWindow window;window.resize(700,400);media->setSize(window.size());media->setParentItem(window.contentItem());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(media->property("imageStatus").toInt(),1);
        QCOMPARE(media->property("decodedSize").toSize(),QSize(2048,1024));QCOMPARE(QImage(path).size(),QSize(3000,1500));
    }
    void generatedOriginalOpensInTheExistingEditor() {
        QTemporaryDir drive(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-editor-XXXXXX.societycontainer");QVERIFY(iiSocietyContainer::SocietyDrive::create(drive.path()));
        QFile weights(drive.filePath("Models/image.safetensors"));QVERIFY(weights.open(QIODevice::WriteOnly));weights.write("fixture");weights.close();
        runtime=GenerationRuntime{QString(DREAMSCAPES_FAKE_GENERATOR),"cpu",1,64,{}};runtime.nativeInference=true;
        runtime.modelPreferencesFile=drive.filePath("settings/models.conf");
        runtime.nativeGenerate=[](const auto& request,const auto&,const auto&,const auto&,const auto&){
            iiLocalDiffusion::NativeGenerationResult result;result.width=request.width;result.height=request.height;
            result.rgb.resize(request.width*request.height*3,100);return result;
        };
        QQmlApplicationEngine engine;engine.setInitialProperties({{"initialContainerPath",drive.path()}});engine.load(source("Main.qml"));
        QCOMPARE(engine.rootObjects().size(),1);auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        window->resize(1440,900);QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* backend=window->findChild<GenerationController*>("generationController");QVERIFY(backend);backend->setForeground(true);
        auto* quick=window->findChild<QObject*>("quickGenerate");QVERIFY(quick);quick->setProperty("prompt","Unsubmitted review draft");
        QVERIFY(!backend->enqueue("Review editor fixture","1:1",1).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(backend->completedResults().size(),1,30000);
        auto* review=window->findChild<GenerationReviewModel*>("generationReviewModel");QVERIFY(review);QTRY_COMPARE(review->totalCount(),1);
        const auto original=review->focusedEntry();const auto key=review->focusedKey();review->selectAt(0);
        auto* result=window->findChild<QQuickItem*>("generationResult");QVERIFY(result);
        auto* gallery=visual(result,"resultGallery");QVERIFY(gallery);gallery->forceActiveFocus();QTest::keyClick(window,Qt::Key_Return);
        QTRY_VERIFY2_WITH_TIMEOUT(window->property("editorVisible").toBool(),qPrintable(window->property("generationRequestError").toString()),30000);
        const auto prepared=values(window,"canvasGenerationResults");QCOMPARE(prepared.size(),1);
        for(const auto* field:{"id","prompt","seed","documentSource","modelName"})QCOMPARE(prepared.first().toMap().value(field),original.value(field));
        auto* editor=window->findChild<QQuickItem*>("canvasEditor");QVERIFY(editor);
        auto *nativeEditor=editor->findChild<EditorWorkspaceModel*>("editorNativeWorkspace");QVERIFY(nativeEditor);
        QVERIFY(nativeEditor->submitAdvanced({{"prompt",QString("Editor-owned fixture")},{"model",backend->selectedModel()},{"width",64},{"height",64},{"outputCount",1}}));
        const auto owned=nativeEditor->editorJobIds();QCOMPARE(owned.size(),1);
        QVERIFY(window->property("editorVisible").toBool());QVERIFY(editor->isVisible());QVERIFY(!result->isVisible());
        backend->cancel(owned.front());
        QVERIFY(QMetaObject::invokeMethod(window,"closeCanvas"));QTRY_VERIFY(result->isVisible());
        QCOMPARE(review->selectedCount(),1);QCOMPARE(review->focusedKey(),key);
        QCOMPARE(quick->property("prompt").toString(),QString("Unsubmitted review draft"));
    }
    void acceptedSubmissionsRoute() {
        QFETCH(int,kind);
        QTemporaryDir drive(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-route-XXXXXX.societycontainer");QVERIFY(iiSocietyContainer::SocietyDrive::create(drive.path()));
        QFile model(drive.filePath("Models/image.safetensors"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("fixture");model.close();
        QVERIFY(QDir().mkpath(drive.filePath("Models/Checkpoint/ltx")));
        QFile manifest(drive.filePath("Models/Checkpoint/ltx/model_index.json"));QVERIFY(manifest.open(QIODevice::WriteOnly));manifest.write(R"({"_class_name":"LTXPipeline"})");manifest.close();
        runtime=GenerationRuntime{QString(DREAMSCAPES_FAKE_GENERATOR),"cpu",1,64,{}};runtime.nativeInference=true;
        runtime.modelPreferencesFile=drive.filePath("settings/models.conf");
        QQmlApplicationEngine engine;engine.setInitialProperties({{"initialContainerPath",drive.path()}});engine.load(source("Main.qml"));
        QCOMPARE(engine.rootObjects().size(),1);auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        window->resize(1440,900);QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* backend=window->findChild<GenerationController*>("generationController");QVERIFY(backend);backend->setForeground(false);
        QSignalSpy accepted(backend,&GenerationController::submissionQueued);
        if(kind<2) {
            auto* quick=window->findChild<QObject*>("quickGenerate");QVERIFY(quick);quick->setProperty("mediaType",kind?"Video":"Image");quick->setProperty("prompt","Review route fixture");
            QVERIFY(QMetaObject::invokeMethod(quick,"submit"));
        } else {
            auto* home=window->findChild<QObject*>("desktopHome");QVERIFY(home);
            QVERIFY(QMetaObject::invokeMethod(home,"activate",Q_ARG(QVariant,kind==3?"video":"image")));
            auto* workspace=window->findChild<QQuickItem*>(kind==3?"videoGenerationWorkspace":"imageGenerationWorkspace");QVERIFY(workspace);
            auto* panel=workspace->property("parameterPanel").value<QQuickItem*>();QVERIFY(panel);
            QVERIFY(QMetaObject::invokeMethod(panel,"edit",Q_ARG(QVariant,"prompt"),Q_ARG(QVariant,"Review route fixture")));
            QVERIFY(QMetaObject::invokeMethod(workspace,"submit"));
            QVERIFY(!values(workspace,"jobIds").isEmpty());
        }
        QCOMPARE(accepted.count(),1);QVERIFY(window->property("resultVisible").toBool());
        const auto jobs=backend->jobs();QVERIFY(!jobs.isEmpty());
        for(const auto& value:jobs)backend->cancel(value.toMap().value("id").toString());
        auto* result=window->findChild<QQuickItem*>("generationResult");QVERIFY(result&&result->isVisible());
        auto* gallery=visual(result,"resultGallery");QVERIFY(gallery);QTRY_VERIFY(gallery->hasActiveFocus()||result->hasActiveFocus());
        QTRY_COMPARE(values(result,"jobs").size(),jobs.size());
        const auto before=backend->jobs();QSignalSpy changed(backend,&GenerationController::jobsChanged);
        QVERIFY(QMetaObject::invokeMethod(window,"dismissResult"));QVERIFY(QMetaObject::invokeMethod(window,"openGenerationReview"));
        QCOMPARE(backend->jobs(),before);QCOMPARE(changed.count(),0);QVERIFY(result->isVisible());
    }
};
int main(int argc,char** argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QQuickStyle::setStyle("Basic");
    const auto runtimeDirectory = qEnvironmentVariable("DREAMSCAPES_TEST_RUNTIME_DIRECTORY", DREAMSCAPES_TEST_DIRECTORY);
    QTemporaryDir settings(runtimeDirectory+"/review-settings-XXXXXX");if(!settings.isValid())return 2;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH",settings.filePath("storage.json").toUtf8());qputenv("DREAMSCAPES_TEMP_DIRECTORY",runtimeDirectory.toUtf8());
    qputenv("SOCIETY_DISABLE_SESSION_RESTORE","1");qputenv("IILOCALLLM_DISABLE_APP_MCP","1");qunsetenv("SOCIETY_CONTAINER_PATH");
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);registerStorageViewTypes();
    presetFile=settings.filePath("presets.json");
    qmlRegisterType<ReviewAdvancedParameters>("Dreamscapes.Storage",1,0,"AdvancedImageParameters");
    qmlRegisterType<ReviewGenerationController>("Dreamscapes.Storage",1,0,"GenerationController");
    GenerationReviewTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "tst_GenerationReview.moc"
