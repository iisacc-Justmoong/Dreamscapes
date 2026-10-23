#include "App/Views/Editor/EditorImageImport.h"
#include <iiSharedCanvas/Bitmap/BitmapCodec.h>
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <iiSharedCanvas/Validation/Validation.h>
#include <SocietyDrive.h>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace iiSharedCanvas;
namespace {
Document maskedDocument() {
    Document d;d.extent={8,8};d.timeline.frameCount=2;
    d.assets.emplace_back(RasterAsset{"color",makeRasterLayer(8,8,0xffff5030U)});
    auto mask=makeRasterLayer(8,8,0xffffffffU);
    for(int y=0;y<8;++y)for(int x=4;x<8;++x)mask.pixels[y*8+x]=0xff000000U;
    d.assets.emplace_back(RasterAsset{"mask",std::move(mask)});
    VectorPath shape;shape.fill=SolidPaint{0xffffffffU};
    shape.commands={MoveTo{{0,0}},LineTo{{8,0}},LineTo{{8,8}},LineTo{{0,8}},ClosePath{}};
    d.assets.emplace_back(VectorAsset{"vector-mask",{8,8},{shape}});
    LayerProperties p;p.id="layer";p.name="Masked artwork";p.frameRange=LayerFrameRange{0,1};
    p.appearance.rasterMask=LayerMask{"mask"};
    p.appearance.rasterMask->density=.8;p.appearance.rasterMask->linkedToLayer=false;
    p.appearance.vectorMask=LayerMask{"vector-mask"};
    p.appearance.filterMask=LayerMask{"mask"};
    p.appearance.maskStack.push_back({"operand","Coverage",LayerMask{"mask"},MaskCombineMode::Intersect});
    MotionKeyframe a,b;a.frame=0;a.value.opacity=.4;b.frame=1;b.value.opacity=.9;p.motion={a,b};
    d.layers.emplace_back(StaticBitmapLayer{p,StaticSource{"color"}});
    d.audioAssets.push_back({"sound",48000,1,std::vector<std::int16_t>(4000,4096)});
    d.audioTracks.push_back({"track","Audio",true,-3,{{"clip","Native audio","sound",0,2,0,-6,false}}});
    return d;
}
std::filesystem::path path(const QString &value){return std::filesystem::path(value.toStdU16String());}
}
class ImageImportTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/image-import-store-XXXXXX.societycontainer"};
    QString saveImage(QTemporaryDir &directory,const QString &name,QColor color=Qt::red) {
        const auto file=directory.filePath(name);QImage image(16,12,QImage::Format_ARGB32);image.fill(color);
        return image.save(file)?file:QString{};
    }
    QString saveNative(QTemporaryDir &directory,const QString &name,const Document &document) {
        const auto file=directory.filePath(name);DocumentFile output;
        if(!output.create(file.toStdString(),document).ok())return {};
        output.close();return file;
    }
    dreamscapes::ImageImportRequest request(std::initializer_list<QString> sources) {
        dreamscapes::ImageImportRequest r;r.storageRoot=path(m_storage.path());
        for(const auto &source:sources)r.sources.push_back(path(source));return r;
    }
private slots:
    void initTestCase() {
        QVERIFY(m_storage.isValid());QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH",m_storage.path().toUtf8());
    }
    void rejectedBatchDoesNotCreateManagedFiles() {
        QTemporaryDir originals(DREAMSCAPES_TEST_DIRECTORY "/image-import-reject-XXXXXX");QVERIFY(originals.isValid());
        const auto png=saveImage(originals,"not-accepted.png");QVERIFY(!png.isEmpty());
        const QDir files(m_storage.filePath("Files"));const auto before=files.entryList(QDir::Files,QDir::Name);
        const auto result=dreamscapes::prepareImageImport(request({png,originals.filePath("missing.png")}));
        QVERIFY(!result.ok());QVERIFY(!result.error.empty());QCOMPARE(files.entryList(QDir::Files,QDir::Name),before);
    }
    void incompatibleAndOversizedBatchesDoNotCreateManagedFiles() {
        QTemporaryDir originals(DREAMSCAPES_TEST_DIRECTORY "/image-import-limits-XXXXXX");QVERIFY(originals.isValid());
        const auto png=saveImage(originals,"not-accepted-either.png");QVERIFY(!png.isEmpty());
        auto other=maskedDocument();other.timeline.frameRate={30,1};
        const auto native=saveNative(originals,"rate.iisc",other);QVERIFY(!native.isEmpty());
        const QDir files(m_storage.filePath("Files"));const auto before=files.entryList(QDir::Files,QDir::Name);
        auto result=dreamscapes::prepareImageImport(request({png,native}));
        QVERIFY(!result.ok());QCOMPARE(files.entryList(QDir::Files,QDir::Name),before);
        Document large;large.extent={8192,8192};
        const auto limit=saveNative(originals,"limit.iisc",large);QVERIFY(!limit.isEmpty());
        result=dreamscapes::prepareImageImport(request({limit,png}));
        QVERIFY(!result.ok());QCOMPARE(files.entryList(QDir::Files,QDir::Name),before);
    }
    void multipleArtboardsAndDeduplicationKeepTheirSourceIndices() {
        QTemporaryDir originals(DREAMSCAPES_TEST_DIRECTORY "/image-import-map-XXXXXX");QVERIFY(originals.isValid());
        auto native=maskedDocument();native.artboards={{"a","A",{{-12,4},{8,8}},0},{"b","B",{{20,-8},{8,8}},0}};
        layerProperties(native.layers.front()).artboardId="a";
        auto second=native.layers.front();layerProperties(second).id="second";layerProperties(second).artboardId="b";native.layers.push_back(second);
        const auto file=saveNative(originals,"two-pages.iisc",native),png=saveImage(originals,"single.png",Qt::blue);
        QVERIFY(!file.isEmpty()&&!png.isEmpty());
        const auto result=dreamscapes::prepareImageImport(request({file,file,png}));QVERIFY2(result.ok(),result.error.c_str());
        QCOMPARE(result.document.artboards.size(),std::size_t(3));QCOMPARE(result.pageSources,(std::vector<std::size_t>{0,0,2}));
        QCOMPARE(result.firstNew,0);QVERIFY(validate(result.document).ok());
        QCOMPARE(result.document.layers.size(),std::size_t(3));
    }
    void masksMotionAndAudioSurviveNativeImport() {
        QTemporaryDir originals(DREAMSCAPES_TEST_DIRECTORY "/image-import-masks-XXXXXX");QVERIFY(originals.isValid());
        const auto source=maskedDocument();const auto file=saveNative(originals,"masked.iisc",source);QVERIFY(!file.isEmpty());
        const auto result=dreamscapes::prepareImageImport(request({file}));QVERIFY2(result.ok(),result.error.c_str());
        const auto &d=result.document;QVERIFY(validate(d).ok());const auto &p=layerProperties(d.layers.front());
        QVERIFY(p.appearance.rasterMask->assetId.starts_with("attachment.0:"));
        QVERIFY(p.appearance.vectorMask->assetId.starts_with("attachment.0:"));
        QVERIFY(p.appearance.filterMask->assetId.starts_with("attachment.0:"));
        QVERIFY(p.appearance.maskStack.front().mask.assetId.starts_with("attachment.0:"));
        QCOMPARE(p.appearance.rasterMask->density,.8);QCOMPARE(p.motion.size(),std::size_t(2));
        QCOMPARE(d.audioAssets.front().samples,source.audioAssets.front().samples);
        QCOMPARE(d.audioTracks.front().muted,true);QCOMPARE(d.audioTracks.front().gainDb,-3.0);
        QCOMPARE(d.audioTracks.front().clips.front().gainDb,-6.0);QVERIFY(!d.audioTracks.front().clips.front().enabled);
        for(FrameIndex frame=0;frame<2;++frame) {
            const auto a=renderFrame(source,frame),b=renderArtboard(d,frame,d.artboards.front().id);
            QVERIFY(a.ok()&&b.ok());QCOMPARE(a.pixels.pixels,b.pixels.pixels);
        }
    }
    void sourcePayloadIsMovedIntoTheAggregate() {
        auto source=maskedDocument();const auto *pixels=std::get<RasterAsset>(source.assets.front()).pixels.pixels.data();
        const auto *audio=source.audioAssets.front().samples.data();Document target;std::int64_t x=0;
        dreamscapes::appendImportedArtboards(target,std::move(source),"Moved",0,x);
        QCOMPARE(std::get<RasterAsset>(target.assets.front()).pixels.pixels.data(),pixels);
        QCOMPARE(target.audioAssets.front().samples.data(),audio);QVERIFY(validate(target).ok());
    }
    void focusedBaseRestoresAuthorVisibilityAndReservesExistingIds() {
        QTemporaryDir originals(DREAMSCAPES_TEST_DIRECTORY "/image-import-base-XXXXXX");QVERIFY(originals.isValid());
        const auto file=saveNative(originals,"addition.iisc",maskedDocument());QVERIFY(!file.isEmpty());
        auto base=std::make_shared<Document>(maskedDocument());base->artboards={{"attachment.1:canvas","Existing",{{0,0},{8,8}},0,false}};
        layerProperties(base->layers.front()).artboardId=base->artboards.front().id;layerProperties(base->layers.front()).visible=false;
        auto r=request({file});r.base=base;r.baseName="Base";r.artboardVisibility={true};r.layerVisibility={true};
        const auto result=dreamscapes::prepareImageImport(r);QVERIFY2(result.ok(),result.error.c_str());
        QCOMPARE(result.firstNew,1);QCOMPARE(result.document.artboards.size(),std::size_t(2));
        QVERIFY(result.document.artboards.front().visible);QVERIFY(layerProperties(result.document.layers.front()).visible);
        QCOMPARE(result.document.artboards.back().id,std::string("attachment.2:canvas"));
        QVERIFY(!base->artboards.front().visible);QVERIFY(!layerProperties(base->layers.front()).visible);
    }
    void cancellationBeforeIntakeDoesNotReadOrWriteSources() {
        auto r=request({"/this-source-does-not-exist.png"});r.cancelled=std::make_shared<std::atomic_bool>(true);
        const QDir files(m_storage.filePath("Files"));const auto before=files.entryList(QDir::Files,QDir::Name);
        const auto result=dreamscapes::prepareImageImport(r);QVERIFY(result.cancelled);QVERIFY(result.error.empty());
        QCOMPARE(files.entryList(QDir::Files,QDir::Name),before);
    }
};
QTEST_GUILESS_MAIN(ImageImportTests)
#include "tst_imageimport.moc"
