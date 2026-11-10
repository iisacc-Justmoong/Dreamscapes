#include "App/ViewModels/LvuiStyleTokens.h"

#include <LVUI/StyleTokens.h>
#include <QColor>
#include <QDirIterator>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QtTest>
#include <QtMath>
#include <memory>

class LvuiStyleTests : public QObject {
    Q_OBJECT
private slots:
    void catalogValuesReachQml() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import LVRS 1.0 as LV
            import Dreamscapes.Style 1.0 as Style
            QtObject {
                property var legacyTheme: LV.Theme
                property color canvas: Style.Tokens.color("Theme Colors/Panel/canvas")
                property color legacyCanvas: LV.Theme.canvas
                property color body: Style.Tokens.color("Theme Colors/Text/bodyColor")
                property color legacyBody: LV.Theme.bodyColor
                property color blue: Style.Tokens.color("Theme Colors/Accent/Blue/Default")
                property color legacyBlue: LV.Theme.accentBlue
                property color blueOverlay: LV.Theme.accentRoleColor("Blue", "Overlay")
                property color greenOverlay: LV.Theme.accentRoleColor("Green", "Overlay")
                property color blueText: LV.Theme.accentRoleColor("Blue", "Text")
                property real gap: Style.Tokens.metric("Theme Metrics/Space/8")
                property real legacyGap: LV.Theme.gap8
            }
        )QML", QUrl());
        std::unique_ptr<QObject> value(component.create());
        QVERIFY2(value, qPrintable(component.errorString()));
        QCOMPARE(value->property("canvas"), value->property("legacyCanvas"));
        QCOMPARE(value->property("body"), value->property("legacyBody"));
        QCOMPARE(value->property("blue"), value->property("legacyBlue"));
        QCOMPARE(value->property("gap"), value->property("legacyGap"));

        const auto color = lvui::default_style_tokens().color("Theme Colors/Panel/canvas");
        QVERIFY(color.has_value());
        QCOMPARE(value->property("canvas").value<QColor>(),
                 QColor::fromRgbF(color->red, color->green, color->blue, color->alpha));

        const auto *theme = value->property("legacyTheme").value<QObject *>();
        QVERIFY(theme);
        dreamscapes::LvuiStyleTokens tokens;
        const std::pair<const char *, const char *> colors[] = {
            {"canvas", "Panel/canvas"}, {"surfaceGhost", "Panel/surfaceGhost"},
            {"windowAlt", "Panel/windowAlt"}, {"subSurface", "Panel/subSurface"},
            {"surfaceSolid", "Panel/surfaceSolid"}, {"surfaceAlt", "Panel/surfaceAlt"},
            {"surfaceInteractive", "Panel/surfaceInteractive"}, {"surfacePanel", "Panel/surfacePanel"},
            {"surfaceControl", "Panel/surfaceControl"}, {"surfaceInput", "Panel/surfaceInput"},
            {"surfaceInputHover", "Panel/surfaceInputHover"}, {"surfaceRaised", "Panel/surfaceRaised"},
            {"window", "Panel/window"}, {"textTokenBase", "Text/textTokenBase"},
            {"titleHeaderColor", "Text/titleHeaderColor"}, {"bodyColor", "Text/bodyColor"},
            {"descriptionColor", "Text/descriptionColor"}, {"captionColor", "Text/captionColor"},
            {"disabledColor", "Text/disabledColor"}, {"accentBlue", "Accent/Blue/Default"},
            {"accentRed", "Accent/Red/Default"}, {"accentGreen", "Accent/Green/Default"},
            {"accentYellow", "Accent/Yellow/Default"}, {"accentPurple", "Accent/Purple/Default"},
            {"accentOrange", "Accent/Orange/Default"}, {"accentBlueMuted", "Accent/Blue/Selected"},
            {"accentWhite", "Text/textTokenBase"},
            {"defaultPrimary", "Accent/Blue/Default"}, {"success", "Accent/Green/Default"},
            {"danger", "Accent/Red/Default"},
        };
        for (const auto &[role, token] : colors) {
            const auto name = QStringLiteral("Theme Colors/") + QLatin1StringView(token);
            const auto actual = tokens.color(name);
            QVERIFY2(actual.isValid(), qPrintable(name));
            QCOMPARE(actual, theme->property(role).value<QColor>());
        }
        for (const auto &[property, token] : {
                 std::pair{"blueOverlay", "Theme Colors/Accent/Blue/Overlay"},
                 std::pair{"greenOverlay", "Theme Colors/Accent/Green/Overlay"},
                 std::pair{"blueText", "Theme Colors/Accent/Blue/Text"}})
            QCOMPARE(tokens.color(QString::fromLatin1(token)), value->property(property).value<QColor>());
        for (const int gap : {2, 4, 6, 8, 10, 12, 16, 20, 24}) {
            const auto role = QByteArray("gap") + QByteArray::number(gap);
            QCOMPARE(tokens.metric(QStringLiteral("Theme Metrics/Space/") + QString::number(gap)),
                     theme->property(role.constData()).toReal());
        }
        const std::pair<const char *, const char *> metrics[] = {
            {"textSubheader", "Theme Typography/Subheader/textSubheader"},
            {"textHeader", "Theme Typography/Header/textHeader"},
            {"textCaption", "Theme Typography/Caption/textCaption"},
            {"textBody", "Theme Typography/Body/textBody"},
            {"textCaptionLineHeight", "Theme Typography/Caption/textCaptionLineHeight"},
            {"textBodyLineHeight", "Theme Typography/Body/textBodyLineHeight"},
            {"textHeader2LineHeight", "Theme Typography/Subheader/textSubheaderLineHeight"},
            {"textCaptionWeight", "Theme Typography/Weight/Regular"},
            {"textBodyWeight", "Theme Typography/Weight/Medium"},
            {"textHeaderWeight", "Theme Typography/Weight/SemiBold"},
            {"textBodyLetterSpacing", "Theme Typography/Tracking/Normal"},
            {"radiusMd", "Theme Metrics/Radius/8"},
            {"radiusLg", "Theme Metrics/Radius/12"},
            {"controlHeightSm", "Theme Metrics/Size/22"},
        };
        for (const auto &[role, name] : metrics)
            QCOMPARE(tokens.metric(QString::fromLatin1(name)), theme->property(role).toReal());
    }

    void invalidOrMismatchedNamesAreNotReplacedByProductDefaults() {
        dreamscapes::LvuiStyleTokens tokens;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*LVUI.*not-a-token.*"));
        QVERIFY(!tokens.color(QStringLiteral("not-a-token")).isValid());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*LVUI.*Space/8.*"));
        QVERIFY(!tokens.color(QStringLiteral("Theme Metrics/Space/8")).isValid());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*LVUI.*Panel/canvas.*"));
        QVERIFY(qIsNaN(tokens.metric(QStringLiteral("Theme Colors/Panel/canvas"))));
    }

    void everyViewReferenceResolvesAgainstTheCatalog() {
        const QRegularExpression reference(
            QStringLiteral("Style\\.Tokens\\.(color|metric)\\(\"([^\"]+)\"\\)"));
        QDirIterator views(QStringLiteral(DREAMSCAPES_QML_SOURCE_DIR), {QStringLiteral("*.qml")},
                           QDir::Files, QDirIterator::Subdirectories);
        int checked = 0;
        while (views.hasNext()) {
            QFile file(views.next());
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
            const auto text = QString::fromUtf8(file.readAll());
            if (text.contains(QStringLiteral("Style.Tokens.")))
                QVERIFY2(text.contains(QStringLiteral("import Dreamscapes.Style 1.0 as Style")),
                         qPrintable(file.fileName()));
            auto matches = reference.globalMatch(text);
            while (matches.hasNext()) {
                const auto match = matches.next();
                const auto token = match.captured(2).toStdString();
                const auto &catalog = lvui::default_style_tokens();
                const bool valid = match.captured(1) == QStringLiteral("color")
                    ? catalog.color(token).has_value() : catalog.number(token).has_value();
                QVERIFY2(valid, qPrintable(file.fileName() + QStringLiteral(": ") + match.captured(2)));
                ++checked;
            }
        }
        QVERIFY(checked > 0);
    }

    void sharedPanelAndPlayheadUseCatalogValues() {
        QQmlEngine engine;
        dreamscapes::LvuiStyleTokens tokens;
        const auto source = QStringLiteral(DREAMSCAPES_QML_SOURCE_DIR) + QStringLiteral("/Views/");
        QQmlComponent panel(&engine, QUrl::fromLocalFile(source + QStringLiteral("PanelFrame.qml")));
        std::unique_ptr<QObject> frame(panel.create());
        QVERIFY2(frame, qPrintable(panel.errorString()));
        QCOMPARE(frame->property("panelBorderColor").value<QColor>(),
                 tokens.color(QStringLiteral("Theme Colors/Panel/canvas")));
        QCOMPARE(frame->property("strokeWidth").toInt(), 1);
        QCOMPARE(frame->property("radius").toReal(), 0.0);

        QQmlComponent marker(&engine, QUrl::fromLocalFile(source + QStringLiteral("TimelinePlayhead.qml")));
        std::unique_ptr<QObject> playhead(marker.create());
        QVERIFY2(playhead, qPrintable(marker.errorString()));
        QCOMPARE(playhead->property("fillColor").value<QColor>(),
                 tokens.color(QStringLiteral("Theme Colors/Accent/Blue/Default")));
    }
};

int main(int argc, char **argv) {
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    dreamscapes::registerLvuiStyleTypes();
    LvuiStyleTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_lvuistyle.moc"
