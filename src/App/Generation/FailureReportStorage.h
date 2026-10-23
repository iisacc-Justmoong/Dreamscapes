#pragma once
#include <iiFileProviderCoreIO.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace dreamscapes {
// Worker-only persistence using the SDK's atomic publication contract.
inline QJsonObject persistFailureReport(QJsonObject report) {
    const auto destination = report["reportPath"].toString();
    if (destination.isEmpty()) return report;
    try {
        const std::filesystem::path path(destination.toStdU16String());
        std::filesystem::create_directories(path.parent_path());
        auto staged = iiFileProvider::core::AtomicFile::create(path);
        if (!staged) throw std::runtime_error("Cannot create an atomic failure report: " + staged.error().native.message());
        const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
        auto remaining = std::as_bytes(std::span(bytes.data(), std::size_t(bytes.size())));
        while (!remaining.empty()) {
            const auto written = staged->write(remaining);
            if (!written || *written == 0) throw std::runtime_error("Cannot write the atomic failure report.");
            remaining = remaining.subspan(*written);
        }
        const auto result = staged->commit();
        if (!result) throw std::runtime_error("Cannot commit the atomic failure report: " + result.error().native.message());
    } catch (const std::exception& error) { report["persistenceError"] = QString::fromUtf8(error.what()); }
    return report;
}
}
