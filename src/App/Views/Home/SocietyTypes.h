#pragma once
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiSocietyContainer/SocietyApplication.h>
#include <Container.h>
#include <QtQml/qqmlregistration.h>
#include "../../ViewModels/ViewData.h"
#include "../../ViewModels/WorkspaceSession.h"
#include "../Editor/EditorToolStateModel.h"
#include "../Editor/EditorGraphicDraftModel.h"
#include "../../ViewModels/VideoTimelineModel.h"
#include "../../ViewModels/GenerationProjection.h"
#include "../../Generation/VideoDraftModel.h"
#include "../../Generation/GenerationComposerModel.h"

struct NativeViewDataRegistration {
    Q_GADGET
    QML_FOREIGN(dreamscapes::ViewData)
    QML_NAMED_ELEMENT(NativeData)
    QML_SINGLETON
};
struct NativeWorkspaceSessionRegistration {
    Q_GADGET
    QML_FOREIGN(dreamscapes::WorkspaceSession)
    QML_NAMED_ELEMENT(NativeWorkspaceSession)
};
struct NativeVideoDraftRegistration {
    Q_GADGET
    QML_FOREIGN(VideoDraftModel)
    QML_NAMED_ELEMENT(NativeVideoDraft)
};
struct NativeVideoTimelineRegistration {
    Q_GADGET
    QML_FOREIGN(dreamscapes::VideoTimelineModel)
    QML_NAMED_ELEMENT(NativeVideoTimeline)
};
struct NativeViewDataDomainRegistration {
    Q_GADGET
    QML_FOREIGN(dreamscapes::ViewDataDomain)
    QML_ANONYMOUS
};

// Expose the SDK types to QML tooling without duplicating their implementations.
struct SocietyContainerRegistration {
    Q_GADGET
    QML_FOREIGN(iiSocietyContainer::Container)
    QML_NAMED_ELEMENT(SocietyContainer)
    QML_UNCREATABLE("Provided by iiSocietyHelper")
};
struct SocietyDashboardFilesRegistration {
    Q_GADGET
    QML_FOREIGN(iiSocietyContainer::DashboardFiles)
    QML_NAMED_ELEMENT(DashboardFiles)
};
struct SocietyApplicationRegistration {
    Q_GADGET
    QML_FOREIGN(iiSocietyContainer::SocietyApplication)
    QML_NAMED_ELEMENT(SocietyApplication)
};

struct NativeEditorToolStateRegistration {
    Q_GADGET
    QML_FOREIGN(EditorToolStateModel)
    QML_NAMED_ELEMENT(NativeEditorToolState)
};
struct NativeGraphicDraftRegistration {
    Q_GADGET
    QML_FOREIGN(EditorGraphicDraftModel)
    QML_NAMED_ELEMENT(NativeGraphicDraft)
};

struct NativeGenerationProjectionRegistration {
    Q_GADGET
    QML_FOREIGN(dreamscapes::GenerationProjection)
    QML_NAMED_ELEMENT(NativeGenerationProjection)
};

struct NativeGenerationComposerRegistration {
    Q_GADGET
    QML_FOREIGN(GenerationComposerModel)
    QML_NAMED_ELEMENT(NativeGenerationComposer)
};
