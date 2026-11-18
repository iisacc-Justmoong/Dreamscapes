#pragma once
#include "App/Native/NativeCanvasFileModel.h"
#include "App/Native/NativeDocumentSession.h"
#include <iiSDKCore>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes::editor {
struct FilePanelRow final {
    std::string id,key,label;
    bool section=false;
    ii::core::Observable<std::string> value;
};
class FilePanelModel final : public std::enable_shared_from_this<FilePanelModel> {
  public:
    static std::shared_ptr<FilePanelModel> create(std::shared_ptr<native::DocumentSession>,std::string_view schema);
    [[nodiscard]] ii::core::Result<void> select(std::string_view);
    void setVisible(bool);
    void setFileModel(std::shared_ptr<native::CanvasFileModel>);
    void sizeChanged(double,double,double inset);
    ii::core::Observable<std::vector<std::shared_ptr<FilePanelRow>>> rows;
    ii::core::Observable<bool> informationMode{true},contentsMode{false},storageMode{false};
    ii::core::Observable<std::string> viewTitle{"Information"};
    ii::core::Observable<double> width{1},height{1},contentWidth{1},valueWidth{1};
  private:
    struct Field {std::string key,label;};
    struct Section {std::string name;std::vector<Field> fields;};
    struct View {std::string id,name;std::vector<Section> sections;};
    FilePanelModel(std::shared_ptr<native::DocumentSession>,std::string_view);
    void start();
    void refresh();
    void refreshFrame();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    std::shared_ptr<native::CanvasFileModel> file_;
    std::vector<View> schema_;
    std::string active_{"information"};
    bool visible_=false,refreshing_=false,pending_=false;
    ii::core::ObservableSubscription revisionObserver_,generationObserver_,frameObserver_,dirtyObserver_,pathObserver_,fileErrorObserver_;
};
}
