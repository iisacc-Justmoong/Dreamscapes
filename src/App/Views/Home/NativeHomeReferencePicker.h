#pragma once

#include <iiGUI>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>
#include <string>

namespace dreamscapes {
class NativeHomeReferenceInputs;
namespace home {
[[nodiscard]] ii::gui::FileDialogOptions homeReferenceDialogOptions(std::string title, std::string filter);

// A host supplies the operating-system dialog; this controller never owns a view.
class NativeHomeReferencePicker final : public QObject {
  public:
    using Chooser = std::function<ii::core::Result<void>(ii::gui::FileDialogOptions, ii::gui::FileDialogCompletion)>;
    using Failure = std::function<void(const ii::core::Error &)>;
    NativeHomeReferencePicker(NativeHomeReferenceInputs *, Chooser, ii::gui::FileDialogOptions, Failure, QObject *parent = nullptr);
    ~NativeHomeReferencePicker() override;
    [[nodiscard]] ii::core::Result<void> choose();
    [[nodiscard]] bool pending() const;
  private:
    struct State { bool pending = false, processing = false; std::uint64_t ticket = 0; };
    void complete(std::uint64_t, ii::core::Result<ii::gui::FileDialogSelection>);
    QPointer<NativeHomeReferenceInputs> inputs_;
    Chooser chooser_;
    ii::gui::FileDialogOptions options_;
    Failure failure_;
    std::shared_ptr<State> state_ = std::make_shared<State>();
};
} // namespace home
} // namespace dreamscapes
