#pragma once
#include <iiColorManager/ColorManager.h>
#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QVariantMap>

// GUI boundary only. All color calculations and picker geometry live in iiColorManager.
class ColorManagerModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE beginColor NOTIFY stateChanged)
    Q_PROPERTY(QColor previousColor READ previousColor NOTIFY stateChanged)
    Q_PROPERTY(QString hex READ hex NOTIFY stateChanged)
    Q_PROPERTY(int revision READ revision NOTIFY stateChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString modeName READ modeName NOTIFY modeChanged)
    Q_PROPERTY(QStringList modeNames READ modeNames CONSTANT)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY modeChanged)
    Q_PROPERTY(bool webSafe READ webSafe NOTIFY stateChanged)
    Q_PROPERTY(bool outOfGamut READ outOfGamut NOTIFY stateChanged)
public:
    explicit ColorManagerModel(QObject* parent=nullptr):QObject(parent) {}
    QColor color() const;
    QColor previousColor() const;
    QString hex() const;
    int revision() const { return revision_; }
    int mode() const { return int(picker_.mode()); }
    QString modeName() const;
    QStringList modeNames() const;
    QVariantList channels() const;
    bool webSafe() const { return iiColorManager::isWebSafe(picker_.color()); }
    bool outOfGamut() const { return picker_.outOfGamut(); }
    void setMode(int);
    Q_INVOKABLE void beginColor(const QColor&);
    Q_INVOKABLE bool editColor(const QColor&);
    Q_INVOKABLE bool editHex(const QString&);
    Q_INVOKABLE bool editChannel(int,double);
    Q_INVOKABLE bool editChannelText(int,const QString &);
    Q_INVOKABLE double channelValue(int) const;
    Q_INVOKABLE QVariantMap channel(int) const;
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool snapWebSafe();
    bool editSurface(int,double,double,int);
    const iiColorManager::Picker& picker() const { return picker_; }
signals:
    void stateChanged();
    void modeChanged();
    void colorEdited(const QColor& color);
private:
    iiColorManager::Picker picker_;
    int revision_=0;
    void changed(bool userEdit);
};

class ColorManagerSurface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(ColorManagerModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int field READ field WRITE setField NOTIFY fieldChanged)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(bool interactive READ interactive WRITE setInteractive NOTIFY interactiveChanged)
    Q_PROPERTY(bool showMarker READ showMarker WRITE setShowMarker NOTIFY showMarkerChanged)
    Q_PROPERTY(bool checkerboard READ checkerboard WRITE setCheckerboard NOTIFY checkerboardChanged)
public:
    enum Field { HueCube, BrightnessCube, ColorWheel, HueRail, BrightnessRail, Spectrum, ChannelRamp };
    Q_ENUM(Field)
    explicit ColorManagerSurface(QQuickItem* parent=nullptr);
    ColorManagerModel* model() const { return model_; }
    int field() const { return field_; }
    int channel() const { return channel_; }
    bool interactive() const { return interactive_; }
    bool showMarker() const { return showMarker_; }
    bool checkerboard() const { return checkerboard_; }
    void setModel(ColorManagerModel*);
    void setField(int);
    void setChannel(int);
    void setInteractive(bool);
    void setShowMarker(bool);
    void setCheckerboard(bool);
    void paint(QPainter*) override;
signals:
    void modelChanged();
    void fieldChanged();
    void channelChanged();
    void interactiveChanged();
    void showMarkerChanged();
    void checkerboardChanged();
protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QPointer<ColorManagerModel> model_;
    int field_=HueCube,channel_=int(iiColorManager::Channel::Red);
    bool interactive_=true,showMarker_=true,checkerboard_=true,pressed_=false;
    void select(const QPointF&);
};
void registerColorManagerTypes();
