#include "App/Views/Result/GenerationReviewModel.h"
#include <QAbstractItemModelTester>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QVariantMap entry(int index) {
    return {{"id",QString::number(index)},{"imageSource",QStringLiteral("file:///review/%1.png").arg(index)},
        {"documentSource",QStringLiteral("file:///review/%1.iisc").arg(index)},
        {"mediaType","Image"},{"state","completed"},{"seed",index},{"prompt",QStringLiteral("Prompt %1").arg(index)}};
}
QVariantMap row(const QVariantList& rows, const QString& label) {
    for (const auto& value : rows) if (value.toMap().value("label")==label) return value.toMap();
    return {};
}
}
class ReviewModelTests : public QObject {
    Q_OBJECT
private slots:
    void filtersAndArrivalsPreserveIdentityAndOriginalMetadata() {
        GenerationReviewModel model;
        QAbstractItemModelTester checker(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QVariantList initial{entry(0),entry(1),entry(2),entry(3)};
        model.setEntries(initial); model.selectAt(1); model.selectAt(3,Qt::ControlModifier);
        const auto selected=model.selectedEntries();
        const auto focused=model.focusedKey();
        model.toggleReview("shortlisted");
        QCOMPARE(model.shortlistedCount(),2);
        model.setFilter("rejected"); QCOMPARE(model.rowCount(),0);
        QCOMPARE(model.selectedEntries(),selected); QCOMPARE(model.focusedKey(),focused);
        QCOMPARE(model.hiddenSelectedCount(),2);
        model.setFilter("all");
        QSignalSpy reset(&model,&QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model,&QAbstractItemModel::rowsInserted);
        QVariantList appended{entry(9)}; appended.append(initial); appended.append(entry(10));
        model.setEntries(appended);
        QCOMPARE(reset.count(),0); QCOMPARE(inserted.count(),1);
        QCOMPARE(model.entryAt(0),initial[0].toMap());
        QCOMPARE(model.entryAt(4),entry(9)); QCOMPARE(model.entryAt(5),entry(10));
        QCOMPARE(model.selectedEntries(),selected); QCOMPARE(model.focusedKey(),focused);
        QCOMPARE(model.reviewState(focused),QString("shortlisted"));
        for(const auto& value:model.entries()) QVERIFY(!value.toMap().contains("reviewState"));
        model.setEntryReview(focused,"rejected");
        QCOMPARE(model.rejectedCount(),1); QCOMPARE(model.shortlistedCount(),1);
        model.clearSelection(); QCOMPARE(model.selectedCount(),0); QCOMPARE(model.focusedKey(),focused);
    }
    void filteredRangeAndSortRemainPredictable() {
        GenerationReviewModel model; model.setEntries({entry(30),entry(10),entry(20),entry(40)});
        model.setSort("seed"); QCOMPARE(model.entryAt(0).value("seed").toInt(),10);
        model.selectAt(0); model.selectAt(2,Qt::ShiftModifier);
        QCOMPARE(model.selectedCount(),3); model.setReview("shortlisted");
        model.setFilter("shortlisted"); QCOMPARE(model.rowCount(),3);
        model.setSort("newest"); QCOMPARE(model.selectedCount(),3);
        QCOMPARE(model.entryAt(0).value("id"),QVariant("20"));
        model.setFilter("rejected"); model.selectAll(); QCOMPARE(model.selectedCount(),3);
        model.setFilter("all"); QCOMPARE(model.rowCount(),4);
    }
    void metadataUsesUnavailableAndMixedWithoutDefaultValues() {
        GenerationReviewModel model;
        auto a=entry(0), b=entry(1); a["width"]=1024;a["height"]=768;
        a["startedAt"]="2026-10-24T00:00:00.000Z";a["finishedAt"]="2026-10-24T00:00:01.250Z";
        a["advancedParameters"]=QVariantMap{{"cfgScale",4.5},{"sampler","euler"}};
        b["mediaType"]="Video";b["mediaSource"]="file:///review/1.mp4";
        model.setEntries({a,b}); model.selectAt(0);
        QCOMPARE(row(model.inspectorRows(),"Model").value("value"),QVariant("Unavailable"));
        QCOMPARE(row(model.inspectorRows(),"Seed").value("value"),QVariant("0"));
        QCOMPARE(row(model.inspectorRows(),"Resolution").value("value"),QVariant(QString::fromUtf8("1024 × 768")));
        QCOMPARE(row(model.inspectorRows(),"Generation time").value("value"),QVariant("1.25 s"));
        QVERIFY(row(model.inspectorRows(),"Generation settings").value("value").toString().contains("euler"));
        model.selectAt(1,Qt::MetaModifier);
        QVERIFY(row(model.inspectorRows(),"Resolution").value("mixed").toBool());
        QCOMPARE(row(model.inspectorRows(),"Model").value("mixed"),QVariant(false));
        QCOMPARE(model.selectedImages(),QVariantList{a});
        model.setFilter("videos"); QCOMPARE(model.selectedCount(),2); QCOMPARE(model.hiddenSelectedCount(),1);
    }
    void previewRevisionDoesNotChangeIdentity() {
        GenerationReviewModel model; auto a=entry(1);
        a["imageSource"]="image://canvas-preview/1?v=first";model.setEntries({a});model.selectAt(0);model.setReview("shortlisted");
        const auto key=model.focusedKey();a["imageSource"]="image://canvas-preview/1?v=second";model.setEntries({a});
        QCOMPARE(model.focusedKey(),key);QCOMPARE(model.selectedCount(),1);QCOMPARE(model.shortlistedCount(),1);
        QCOMPARE(model.selectedEntries(),QVariantList{a});
    }
    void originalVideoReferenceFocusesTheMatchingResult() {
        GenerationReviewModel model;auto a=entry(0),b=entry(1);
        a["mediaType"]="Video";a["mediaSource"]="file:///review/0.mp4";
        b["mediaType"]="Video";b["mediaSource"]=QUrl("file:///review/Video outputs/1.mp4");
        model.setEntries({a,b});model.selectAt(0);model.setFilter("images");
        model.focusSource(b.value("mediaSource").toUrl().toString(QUrl::FullyEncoded));
        QCOMPARE(model.focusedEntry(),b);QCOMPARE(model.selectedEntries(),QVariantList{a});
        model.focusSource({});QCOMPARE(model.focusedEntry(),b);
    }
    void thousandResultsDoNotResetExistingRowsOrTouchSources() {
        QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/review-model-XXXXXX"); QFile source(files.filePath("source.iisc"));QVERIFY(source.open(QIODevice::WriteOnly));source.write("original canvas bytes");source.close();
        QVariantList entries;for(int i=0;i<1000;++i) {
            auto value=entry(i);if(i==500)value["documentSource"]=QUrl::fromLocalFile(source.fileName());entries.append(value);
        }
        GenerationReviewModel model;model.setEntries(entries);model.selectAt(500);model.setReview("shortlisted");
        model.setFilter("shortlisted");model.setFilter("all");
        QSignalSpy reset(&model,&QAbstractItemModel::modelReset); entries.append(entry(1000));model.setEntries(entries);
        QCOMPARE(model.rowCount(),1001);QCOMPARE(reset.count(),0);QCOMPARE(model.focusedEntry().value("id"),QVariant("500"));
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),QByteArray("original canvas bytes"));
        model.setEntries({});QCOMPARE(model.selectedCount(),0);QCOMPARE(model.shortlistedCount(),0);QVERIFY(model.focusedKey().isEmpty());QVERIFY(!model.selectionMode());
    }
};
QTEST_GUILESS_MAIN(ReviewModelTests)
#include "tst_reviewmodel.moc"
