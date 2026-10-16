#pragma once
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiSocietyContainer/SocietyApplication.h>
#include <Container.h>
#include <iiSocietyHelper/FileDownload.h>
#include <QtQml/qqmlregistration.h>

// Expose the SDK types to QML tooling without duplicating their implementations.
struct SocietyFileDownloadRegistration {
    Q_GADGET
    QML_FOREIGN(iiSocietyHelper::FileDownload)
    QML_NAMED_ELEMENT(SocietyFileDownload)
    QML_UNCREATABLE("Provided by iiSocietyHelper")
};
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
