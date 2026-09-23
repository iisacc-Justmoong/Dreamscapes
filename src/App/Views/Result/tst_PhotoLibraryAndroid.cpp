#include "PhotoLibraryExporter.h"
#include "PhotoLibraryNative.h"
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJniObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class PhotoLibraryAndroidTests : public QObject
{
    Q_OBJECT
private slots:
    void mediaStorePreservesOriginal()
    {
        QTemporaryDir directory;
        QImage image(512, 512, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        image.setText("seed", "123456789");
        const auto path = directory.filePath(QStringLiteral("Dreamscapes 검증 #1.png"));
        QVERIFY(image.save(path));
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        PhotoLibraryExporter exporter;
        QSignalSpy saved(&exporter, &PhotoLibraryExporter::saved);
        QSignalSpy failed(&exporter, &PhotoLibraryExporter::failed);
        QVERIFY(exporter.supported());
        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(saved.size() + failed.size() > 0, 15000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        const auto asset = saved.first().first().toString();
        QVERIFY(asset.startsWith("content://media/"));
        QFile imported(asset);
        const bool readable = imported.open(QIODevice::ReadOnly);
        const auto bytes = imported.readAll();
        imported.close();
        // Remove only this test-created asset, including when subsequent assertions fail.
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        const auto resolver = activity.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        const auto uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;", QJniObject::fromString(asset).object<jstring>());
        auto cursor = resolver.callObjectMethod("query",
            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
            uri.object(), nullptr, nullptr, nullptr, nullptr);
        bool published = false;
        if (cursor.isValid() && cursor.callMethod<jboolean>("moveToFirst")) {
            const auto column = cursor.callMethod<jint>("getColumnIndex", "(Ljava/lang/String;)I",
                QJniObject::fromString("is_pending").object<jstring>());
            published = column >= 0 && cursor.callMethod<jint>("getInt", "(I)I", column) == 0;
        }
        if (cursor.isValid()) cursor.callMethod<void>("close");
        const int removed = resolver.callMethod<jint>("delete",
            "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I", uri.object(), nullptr, nullptr);
        QVERIFY(readable);
        QCOMPARE(bytes, original.readAll());
        QVERIFY(published);
        QCOMPARE(removed, 1);
        QVERIFY(!exporter.busy());
    }

    void nativeFailureReportsError()
    {
        // Exercise the Java catch/rollback path, beyond the common C++ validation.
        bool finished = false;
        QString error;
        const auto name = "Dreamscapes-missing-" + QUuid::createUuid().toString(QUuid::Id128) + ".png";
        nativePhotoLibrarySaveOperation()("/missing/dreamscapes/" + name, [&](QString asset, QString message) {
            QVERIFY(asset.isEmpty());
            error = message;
            finished = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(finished, 15000);
        QVERIFY(!error.isEmpty());
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        const auto resolver = activity.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        const auto collection = QJniObject::getStaticObjectField("android/provider/MediaStore$Images$Media",
            "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
        const auto selection = QJniObject::fromString("_display_name = '" + name + "'");
        auto cursor = resolver.callObjectMethod("query",
            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
            collection.object(), nullptr, selection.object<jstring>(), nullptr, nullptr);
        QVERIFY(cursor.isValid());
        const auto count = cursor.callMethod<jint>("getCount");
        cursor.callMethod<void>("close");
        QCOMPARE(count, 0); // No pending row or broken image survives a failed write.
    }
};

QTEST_MAIN(PhotoLibraryAndroidTests)
#include "tst_PhotoLibraryAndroid.moc"
