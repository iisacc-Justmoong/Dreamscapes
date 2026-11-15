#pragma once

#include <QObject>
#include <QPointer>
#include <vector>

class GenerationController;
namespace dreamscapes {
class GenerationProjection;
class WorkspaceSession;

// Temporary backend transport for a native host. The source session selects
// the controller; no QML bindings or ownership transfer are required.
class GenerationProjectionBinding final : public QObject {
  public:
    GenerationProjectionBinding(WorkspaceSession *, GenerationProjection *, QObject *parent = nullptr);
    ~GenerationProjectionBinding() override;
    void detach();

  private:
    enum Section : unsigned { Jobs = 1, Results = 2, Storage = 4, Inference = 8, Preview = 16,
                              CurrentResult = 32, RequestIds = 64, All = 127 };
    void rebindController(bool synchronize);
    void clearController();
    void synchronize(unsigned sections);

    QPointer<WorkspaceSession> session_;
    QPointer<GenerationProjection> projection_;
    QPointer<GenerationController> controller_;
    std::vector<QMetaObject::Connection> controllerConnections_;
    bool attached_ = false;
};
} // namespace dreamscapes
