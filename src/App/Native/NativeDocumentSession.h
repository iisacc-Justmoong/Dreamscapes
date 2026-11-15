#pragma once

#include <Document/Document.h>
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes::native {
class DocumentSession;
enum class LayerKeyKind {source,motion};
struct MaskPatch final {
    std::optional<bool> enabled,inverted;
    std::optional<double> density,featherRadius;
    std::optional<iiSharedCanvas::MaskCombineMode> combine;
};
struct DocumentLayerRow final {
    std::string id;
    ii::core::Observable<std::string> name;
    ii::core::Observable<bool> visible{true},selected{false};
    std::weak_ptr<DocumentSession> owner;
    void select();
    void toggleVisible(bool);
};

// Owns a detached in-memory IISC draft. View navigation never persists edits.
class DocumentSession final : public std::enable_shared_from_this<DocumentSession> {
  public:
    static std::shared_ptr<DocumentSession> create();
    [[nodiscard]] ii::core::Result<void> attach(iiSharedCanvas::Document,std::string name,bool isNew=false);
    [[nodiscard]] ii::core::Result<void> selectLayer(std::string_view);
    [[nodiscard]] ii::core::Result<void> seek(std::int64_t);
    [[nodiscard]] ii::core::Result<void> setLayerVisible(std::string_view,bool);
    [[nodiscard]] ii::core::Result<void> setLayerOpacity(std::string_view,double);
    [[nodiscard]] ii::core::Result<void> setLayerBlend(std::string_view,RasterBlendMode);
    [[nodiscard]] ii::core::Result<void> setLayerTransform(std::string_view,const AffineTransform &);
    // RGB is 0xRRGGBB; an absent alpha retains each path's existing alpha.
    [[nodiscard]] ii::core::Result<void> setVectorSolidFill(std::string_view,std::uint32_t rgb,std::optional<std::uint8_t> alpha={});
    [[nodiscard]] ii::core::Result<std::string> createLayerMask(std::string_view);
    [[nodiscard]] ii::core::Result<std::string> duplicateLayerMask(std::string_view,std::string_view);
    [[nodiscard]] ii::core::Result<void> removeLayerMask(std::string_view,std::string_view);
    [[nodiscard]] ii::core::Result<void> updateLayerMask(std::string_view,std::string_view,const MaskPatch &);
    [[nodiscard]] ii::core::Result<void> setLayerMaskStackEnabled(std::string_view,bool);
    [[nodiscard]] ii::core::Result<void> setLayerRange(std::string_view,std::int64_t first,std::int64_t end);
    [[nodiscard]] ii::core::Result<void> moveLayerRange(std::string_view,std::int64_t first,std::int64_t end);
    [[nodiscard]] ii::core::Result<void> moveLayerKey(std::string_view,LayerKeyKind,std::int64_t from,std::int64_t to);
    [[nodiscard]] std::string canvasTransformStatus() const;
    [[nodiscard]] ii::core::Result<void> resizeCanvasCentered(std::int32_t width,std::int32_t height);
    [[nodiscard]] ii::core::Result<void> mirrorCanvas(bool horizontal);
    [[nodiscard]] ii::core::Result<void> rotateCanvas(double degrees);
    [[nodiscard]] ii::core::Result<std::string> createLayer(iiSharedCanvas::LayerKind);
    [[nodiscard]] ii::core::Result<std::string> createAudioTrack();
    [[nodiscard]] ii::core::Result<std::string> duplicateLayer(std::string_view);
    [[nodiscard]] ii::core::Result<void> removeLayer(std::string_view);
    [[nodiscard]] ii::core::Result<void> editPublication(std::string_view,std::string);
    [[nodiscard]] ii::core::Result<void> editPublicationSetting(std::string,iiSharedCanvas::PublicationSetting);
    // Reconciles an already committed file with the same document generation.
    // Returns false when later edits remain unsaved.
    [[nodiscard]] ii::core::Result<bool> acknowledgeSave(std::uint64_t generation,std::uint64_t revision,
        const iiSharedCanvas::Document &captured,iiSharedCanvas::Document confirmed);
    [[nodiscard]] const iiSharedCanvas::Document *document() const noexcept;
    [[nodiscard]] bool hasCurrentLayerSelection() const;
    // Immutable snapshots keep the exact prior document alive on workers.
    // Only the owner thread may acquire a new snapshot.
    [[nodiscard]] std::shared_ptr<const iiSharedCanvas::Document> snapshot() const;
    ii::core::Observable<bool> hasDocument{false},dirty{false};
    ii::core::Observable<std::string> title,dimensions,selectedLayer,publicationTitle,publicationDescription;
    ii::core::Observable<std::int64_t> frame{0},frameCount{0};
    ii::core::Observable<std::uint64_t> revision{0},visualRevision{0},documentGeneration{0};
    ii::core::Observable<std::vector<std::shared_ptr<DocumentLayerRow>>> layers;
  private:
    DocumentSession()=default;
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] ii::core::Result<void> detachForEdit(bool visual=true);
    [[nodiscard]] ii::core::Result<void> changed(std::uint64_t generation,bool visual=true);
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<iiSharedCanvas::Document> document_;
    std::uint64_t selectedLayerGeneration_=0;
    bool reconcilingSelection_=false,selectionPending_=false,editingStructure_=false;
};
}
