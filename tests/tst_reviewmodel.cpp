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
    void focusOnlyNotifiesTheTwoAffectedRows() {
        GenerationReviewModel model;
        QVariantList entries; for (int i = 0; i < 1000; ++i) entries.append(entry(i));
        model.setEntries(entries); model.setSort("seed"); model.selectAt(500);
        QAbstractItemModelTester checker(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy projection(&model, &GenerationReviewModel::projectionChanged);
        QSignalSpy state(&model, &GenerationReviewModel::stateChanged);
        const auto selected = model.selectedEntries();
        model.focusAt(501);
        QCOMPARE(changed.size(), 2);
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(changed[i][0].value<QModelIndex>().row(), 500 + i);
            QCOMPARE(changed[i][1].value<QModelIndex>().row(), 500 + i);
            QCOMPARE(changed[i][2].value<QList<int>>(), QList<int>{GenerationReviewModel::FocusedRole});
        }
        QCOMPARE(reset.size(), 0); QCOMPARE(projection.size(), 0); QCOMPARE(state.size(), 1);
        QCOMPARE(model.selectedEntries(), selected); QCOMPARE(model.focusedIndex(), 501);
        model.focusAt(501); QCOMPARE(changed.size(), 2); QCOMPARE(state.size(), 1);
        model.setFilter("rejected"); changed.clear();
        model.focusSource(entry(502).value("documentSource").toString());
        QCOMPARE(changed.size(), 0); QCOMPARE(model.focusedEntry(), entry(502));
        model.setFilter("all");
        QVERIFY(model.data(model.index(502), GenerationReviewModel::FocusedRole).toBool());
        changed.clear(); model.focusAt(503); QCOMPARE(changed.size(), 2);
    }
    void selectionAndReviewOnlyNotifyRolesThatChanged() {
        GenerationReviewModel model; model.setEntries({entry(0), entry(1), entry(2), entry(3)});
        model.selectAt(0);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.selectAt(2, Qt::ControlModifier);
        for (const auto &signal : changed) {
            const auto roles = signal[2].value<QList<int>>();
            QVERIFY(!roles.isEmpty());
            QVERIFY(!roles.contains(GenerationReviewModel::EntryRole));
            QVERIFY(!roles.contains(GenerationReviewModel::ReviewStateRole));
            QVERIFY(signal[0].value<QModelIndex>().row() != 1);
            QVERIFY(signal[1].value<QModelIndex>().row() != 3);
        }
        QCOMPARE(model.selectedCount(), 2); QCOMPARE(model.shortlistedCount(), 0);
        changed.clear(); model.setReview("shortlisted");
        QCOMPARE(model.shortlistedCount(), 2); QCOMPARE(model.rejectedCount(), 0);
        for (const auto &signal : changed)
            QCOMPARE(signal[2].value<QList<int>>(), QList<int>{GenerationReviewModel::ReviewStateRole});
        model.setFilter("shortlisted"); model.setEntryReview(model.keyAt(0), "rejected");
        QCOMPARE(model.rowCount(), 1); QCOMPARE(model.rejectedCount(), 1); QCOMPARE(model.hiddenSelectedCount(), 1);
        model.setEntries({entry(2)});
        QCOMPARE(model.selectedCount(), 1); QCOMPARE(model.shortlistedCount(), 1); QCOMPARE(model.rejectedCount(), 0);
    }
    void focusAndCounterReadMeasurement() {
        GenerationReviewModel model; QVariantList entries;
        for (int i = 0; i < 1000; ++i) entries.append(entry(i));
        model.setEntries(entries); model.setSort("seed"); model.selectAt(500); model.setReview("shortlisted");
        qint64 notifiedRows = 0;
        connect(&model, &QAbstractItemModel::dataChanged, &model, [&](const auto &first, const auto &last) {
            notifiedRows += last.row() - first.row() + 1;
        });
        QElapsedTimer timer; timer.start(); int checksum = 0;
        for (int i = 0; i < 1000; ++i) {
            model.focusAt(i);
            checksum += model.selectedCount() + model.shortlistedCount() + model.rejectedCount() + model.hiddenSelectedCount();
        }
        QCOMPARE(checksum, 2000);
        qInfo() << "1000 results / 1000 focus changes ns=" << timer.nsecsElapsed() << "notified rows=" << notifiedRows;
    }
    void arrivalsAndHiddenRemovalOnlyInvalidateChangedMetadata() {
        GenerationReviewModel model; model.setEntries({entry(0), entry(1), entry(2)});
        QAbstractItemModelTester checker(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.setEntries({entry(0), entry(1), entry(2), entry(3)});
        QCOMPARE(changed.size(), 0);
        auto updated = entry(2); updated["thumbnailSource"] = QUrl("file:///new-preview.png");
        model.setEntries({entry(0), entry(1), updated, entry(3)});
        QCOMPARE(changed.size(), 1); QCOMPARE(changed[0][0].value<QModelIndex>().row(), 2);
        QVERIFY(changed[0][2].value<QList<int>>().isEmpty());
        QCOMPARE(model.data(model.index(2), GenerationReviewModel::ThumbnailRole).toUrl(), QUrl("file:///new-preview.png"));
        model.setEntryReview(model.keyAt(1), "shortlisted"); model.setFilter("shortlisted");
        changed.clear(); model.setEntries({entry(1), updated, entry(3)});
        QCOMPARE(model.rowCount(), 1); QCOMPARE(changed.size(), 1);
        QCOMPARE(model.data(model.index(0), GenerationReviewModel::OrdinalRole).toInt(), 1);
        QCOMPARE(model.data(model.index(0), GenerationReviewModel::TitleRole).toString(), QString::fromUtf8("Image · 01"));
    }
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
    void nativeKeyboardAndBatchCommandsPreserveTargets() {
        GenerationReviewModel model;model.setEntries({entry(0),entry(1),entry(2),entry(3)});
        auto command=model.keyCommand(Qt::Key_Down,0,2,false,false,false);
        QVERIFY(command.value("accepted").toBool());QCOMPARE(model.focusedIndex(),2);
        command=model.keyCommand(Qt::Key_Left,Qt::ShiftModifier,2,false,false,false);QCOMPARE(model.selectedCount(),2);
        QVERIFY(model.keyCommand(Qt::Key_F,Qt::ControlModifier,2,false,false,false).isEmpty());QCOMPARE(model.shortlistedCount(),0);
        model.keyCommand(Qt::Key_F,0,2,false,false,false);QCOMPARE(model.shortlistedCount(),2);
        const auto targets=model.contextEntries();model.setFilter("rejected");model.markEntries(targets,"rejected");QCOMPARE(model.rejectedCount(),2);QCOMPARE(model.selectedCount(),2);
        QCOMPARE(model.keyCommand(Qt::Key_Escape,0,2,true,true,false).value("action").toString(),QString("back"));QCOMPARE(model.selectedCount(),2);
        model.keyCommand(Qt::Key_Escape,0,2,false,false,false);QCOMPARE(model.selectedCount(),0);
        QVERIFY(model.validImages({entry(0),entry(1)},2,4));auto video=entry(2);video["mediaType"]="Video";QVERIFY(!model.validImages({entry(0),video},2,4));
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
