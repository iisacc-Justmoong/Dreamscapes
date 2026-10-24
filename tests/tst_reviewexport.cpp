#include "App/Views/Result/GenerationReviewExport.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ReviewExportTests:public QObject {
    Q_OBJECT
private slots:
    void partialResultsAreReportedAndInputsStayImmutable() {
        QTemporaryDir folder(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-export-XXXXXX");QVERIFY(folder.isValid());
        int calls=0;
        GenerationReviewExport exporter([&](const QUrl& source,const QUrl& target,bool video){
            ++calls;if(source.fileName()=="bad.png")return QString("fixture failure");
            QFile file(target.toLocalFile());if(!file.open(QIODevice::WriteOnly))return file.errorString();
            file.write(video?"video":"image");return QString{};
        },nullptr);
        QVariantList inputs{QVariantMap{{"imageSource","file:///a.png"},{"state","completed"}},
            QVariantMap{{"imageSource","file:///bad.png"}},QVariantMap{{"mediaType","Video"},{"mediaSource","file:///b.mp4"}}};
        const auto snapshot=inputs;QSignalSpy done(&exporter,&GenerationReviewExport::completed);
        QVERIFY(exporter.start(inputs,QUrl::fromLocalFile(folder.path())));
        inputs.clear();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);
        QCOMPARE(calls,3);QCOMPARE(exporter.succeeded(),2);QCOMPARE(exporter.failed(),1);QCOMPARE(exporter.processed(),3);
        const auto output=exporter.result().value("directory").toUrl().toLocalFile();
        QVERIFY(QFile::exists(output+"/0001.png"));QVERIFY(!QFile::exists(output+"/0002.png"));QVERIFY(QFile::exists(output+"/0003.mp4"));
        QVERIFY(!snapshot[0].toMap().contains("reviewState"));QCOMPARE(exporter.result().value("errors").toStringList().size(),1);
    }
    void invalidFolderFailsEveryItemWithoutClaimingSuccess() {
        QTemporaryDir folder(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-export-XXXXXX");GenerationReviewExport exporter([](const auto&,const auto&,bool){return QString{};},nullptr);
        QSignalSpy done(&exporter,&GenerationReviewExport::completed);
        QVERIFY(exporter.start({QVariantMap{{"imageSource","file:///a.png"}}},QUrl::fromLocalFile(folder.filePath("missing"))));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QCOMPARE(exporter.succeeded(),0);QCOMPARE(exporter.failed(),1);
    }
    void emptySelectionAndRemoteFolderAreRejected() {
        GenerationReviewExport exporter([](const auto&,const auto&,bool){return QString{};},nullptr);
        QSignalSpy rejected(&exporter,&GenerationReviewExport::rejected);
        QVERIFY(!exporter.start({},QUrl::fromLocalFile("/tmp")));
        QVERIFY(!exporter.start({QVariantMap{{"imageSource","file:///a.png"}}},QUrl("https://example.invalid/export")));
        QCOMPARE(rejected.count(),2);QVERIFY(!exporter.busy());
    }
};
QTEST_GUILESS_MAIN(ReviewExportTests)
#include "tst_reviewexport.moc"
