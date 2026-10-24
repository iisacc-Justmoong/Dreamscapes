#include "StorageViewTypes.h"
#include "Views/Home/HomeCanvas.h"
#include "Views/Home/CanvasPresets.h"
#include "Views/Home/CanvasPreview.h"
#include "Views/Home/ImageOpenRequest.h"
#include "Views/Editor/EditorCanvas.h"
#include "Views/Editor/EditorCanvasFeedback.h"
#include "Views/Editor/EditorAlignmentOverlay.h"
#include "Views/Editor/EditorClippingOverlay.h"
#include "Views/Editor/EditorTimelineModel.h"
#include "Views/Editor/EditorTimelineViewport.h"
#include "Views/Editor/EditorProject.h"
#include "Views/Editor/EditorImageImportRequest.h"
#include "Views/Editor/EditorLayerHistogram.h"
#include "Views/Editor/EditorGraphicHelper.h"
#include "Views/Editor/EditorGraphicCommand.h"
#include "Views/ColorManagerModel.h"
#include "Views/Result/ImageFileExporter.h"
#include "Views/Result/GenerationReviewModel.h"
#include "Views/Result/GenerationReviewExport.h"
#include "Views/Result/PhotoLibraryExporter.h"
#include "Publishing/SocialPublishingController.h"
#include "Publishing/PublishDraft.h"
#include "Publishing/PublishReview.h"
#include <iiSocietyHelper/FileDownload.h>
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiSocietyContainer/SocietyApplication.h>
#include <Container.h>
#include <QtQml/qqml.h>

void registerStorageViewTypes()
{
    static const bool registered = [] {
        constexpr auto uri = "Dreamscapes.Storage";
        registerColorManagerTypes();
        qmlRegisterUncreatableType<iiSocietyContainer::Container>(uri, 1, 0,
            "SocietyContainer", "Provided by iiSocietyHelper");
        qmlRegisterUncreatableType<iiSocietyHelper::FileDownload>(uri, 1, 0,
            "SocietyFileDownload", "Provided by iiSocietyHelper");
        qmlRegisterType<SocialPublishingController>(uri, 1, 0, "SocialPublishingController");
        qmlRegisterAnonymousType<PublishCanvasModel>(uri, 1);
        qmlRegisterType<PublishDraft>(uri, 1, 0, "PublishDraft");
        qmlRegisterType<PublishReview>(uri, 1, 0, "PublishReview");
        qmlRegisterType<iiSocietyContainer::DashboardFiles>(uri, 1, 0, "DashboardFiles");
        qmlRegisterType<iiSocietyContainer::SocietyApplication>(uri, 1, 0, "SocietyApplication");
        qmlRegisterType<HomeCanvas>(uri, 1, 0, "HomeCanvas");
        qmlRegisterType<CanvasPresets>(uri, 1, 0, "CanvasPresets");
        qmlRegisterType<CanvasPreview>(uri, 1, 0, "CanvasPreview");
        qmlRegisterType<ImageOpenRequest>(uri, 1, 0, "ImageOpenRequest");
        qmlRegisterType<EditorCanvas>(uri, 1, 0, "EditorCanvas");
        qmlRegisterType<EditorCanvasFeedback>(uri, 1, 0, "EditorCanvasFeedback");
        qmlRegisterType<EditorAlignmentOverlay>(uri, 1, 0, "EditorAlignmentOverlay");
        qmlRegisterType<EditorClippingOverlay>(uri, 1, 0, "EditorClippingOverlay");
        qmlRegisterAnonymousType<EditorViewOptions>(uri, 1);
        qmlRegisterType<EditorTimelineModel>(uri, 1, 0, "EditorTimelineModel");
        qmlRegisterType<EditorTimelineViewport>(uri, 1, 0, "EditorTimelineViewport");
        qmlRegisterType<EditorProject>(uri, 1, 0, "EditorProject");
        qmlRegisterType<EditorImageImportRequest>(uri, 1, 0, "EditorImageImportRequest");
        qmlRegisterType<EditorLayerHistogram>(uri, 1, 0, "EditorLayerHistogram");
        qmlRegisterType<EditorGraphicHelper>(uri, 1, 0, "EditorGraphicHelper");
        qmlRegisterType<EditorGraphicCommand>(uri, 1, 0, "EditorGraphicCommand");
        qmlRegisterType<ImageFileExporter>(uri, 1, 0, "ImageFileExporter");
        qmlRegisterType<GenerationReviewModel>(uri, 1, 0, "GenerationReviewModel");
        qmlRegisterType<GenerationReviewExport>(uri, 1, 0, "GenerationReviewExport");
        qmlRegisterType<PhotoLibraryExporter>(uri, 1, 0, "PhotoLibraryExporter");
        return true;
    }();
    (void)registered;
}
