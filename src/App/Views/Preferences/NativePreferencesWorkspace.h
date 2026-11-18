#pragma once
#include <iiSDKCore>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes::preferences {
struct CategorySpec {std::string id,label;};
struct FieldSpec {std::string id,label,value; bool writable=false,choice=false;};
struct Choice {std::string key,label; bool operator==(const Choice &) const = default;};
class WorkspaceModel;
struct Category final {
    std::string id,label;
    ii::core::Observable<bool> selected{false};
    std::weak_ptr<WorkspaceModel> owner;
    void choose();
};
struct Field final {
    std::string id,label,category;
    bool isChoice=false;
    std::vector<std::string> options,choiceKeys;
    ii::core::Observable<std::string> value;
    ii::core::Observable<bool> editable{false};
    ii::core::Observable<int> index{0};
    std::weak_ptr<WorkspaceModel> owner;
    [[nodiscard]] ii::core::Result<void> edit(std::string);
    [[nodiscard]] ii::core::Result<void> select(int);
    void edited(std::string input) {(void)edit(std::move(input));}
    void selected(int input) {(void)select(input);}
  private:
    friend class WorkspaceModel;
    std::uint64_t projectionVersion_=0;
};
[[nodiscard]] std::vector<CategorySpec> parseCatalog(std::string_view source);
class WorkspaceModel final : public std::enable_shared_from_this<WorkspaceModel> {
  public:
    using WriteHandler=std::function<ii::core::Result<void>(std::string,std::string,std::string)>;
    static std::shared_ptr<WorkspaceModel> create(std::vector<CategorySpec>);
    [[nodiscard]] ii::core::Result<void> select(std::string_view);
    [[nodiscard]] ii::core::Result<void> setSnapshot(std::string,std::vector<FieldSpec>);
    [[nodiscard]] ii::core::Result<void> clearSnapshot(std::string_view);
    [[nodiscard]] ii::core::Result<void> setChoices(std::string,std::string,std::vector<Choice>,bool available);
    [[nodiscard]] ii::core::Result<void> edit(std::string_view category,std::string_view key,std::string value);
    void setWriteHandler(WriteHandler);
    [[nodiscard]] ii::core::Result<void> setCategoryWriteHandler(std::string,WriteHandler);
    [[nodiscard]] ii::core::Result<void> setCategoryStatus(std::string,std::string);
    void sizeChanged(double,double,double);
    ii::core::Observable<std::vector<std::shared_ptr<Category>>> categories;
    ii::core::Observable<std::vector<std::shared_ptr<Field>>> fields;
    ii::core::Observable<std::string> category,status{"Settings unavailable"};
    ii::core::Observable<bool> available{false};
    ii::core::Observable<double> width{1},height{1},sidebarWidth{1},detailWidth{1};
  private:
    friend struct Field;
    explicit WorkspaceModel(std::vector<CategorySpec>);
    void start();
    void project();
    [[nodiscard]] std::shared_ptr<Field> makeField(const std::string &,const FieldSpec &);
    [[nodiscard]] bool fieldEditable(const std::string &,const FieldSpec &) const;
    [[nodiscard]] WriteHandler writeHandler(std::string_view) const;
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] ii::core::Result<void> requireCurrentField(const Field *) const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::vector<CategorySpec> catalog_;
    std::map<std::string,std::vector<FieldSpec>,std::less<>> snapshots_;
    struct Choices {std::vector<Choice> entries; bool available=false;};
    std::map<std::pair<std::string,std::string>,Choices> choices_;
    std::map<std::string,WriteHandler,std::less<>> categoryWrites_;
    std::map<std::string,std::string,std::less<>> categoryStatus_;
    WriteHandler write_;
    std::uint64_t snapshotRevision_=0,projectionVersion_=0;
    bool projecting_=false,projectionPending_=false,selecting_=false,selectionPending_=false;
};
}
