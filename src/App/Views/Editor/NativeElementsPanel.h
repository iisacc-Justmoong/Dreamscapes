#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::editor {
// This inspector deliberately edits only static vector geometry with no
// motion. Unsupported content is shown as unavailable rather than guessed.
class ElementsPanelModel final : public std::enable_shared_from_this<ElementsPanelModel> {
  public:
    static std::shared_ptr<ElementsPanelModel> create(std::shared_ptr<native::DocumentSession>);
    [[nodiscard]] ii::core::Result<void> editWidth(std::string);
    [[nodiscard]] ii::core::Result<void> editHeight(std::string);
    [[nodiscard]] ii::core::Result<void> setConstrained(bool);
    [[nodiscard]] ii::core::Result<void> apply();
    [[nodiscard]] ii::core::Result<void> editFill(std::string);
    [[nodiscard]] ii::core::Result<void> applyFill();
    void setVisible(bool);
    void sizeChanged(double,double,double,double);
    ii::core::Observable<bool> editable{false},constrained{true},fillEditable{false};
    ii::core::Observable<double> elementWidth{0},elementHeight{0},width{1},height{1},contentWidth{1},fieldWidth{1};
    ii::core::Observable<std::string> widthText,heightText,fillText,selectedName,status;
  private:
    explicit ElementsPanelModel(std::shared_ptr<native::DocumentSession>);
    void start();
    void synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::core::ObservableSubscription selection_,revision_,generation_;
    std::string currentId_;
    std::uint64_t currentGeneration_=0;
    enum class Edited {none,width,height} edited_=Edited::none;
    bool visible_=false,synchronizing_=false,pending_=false,fillEdited_=false;
};
}
