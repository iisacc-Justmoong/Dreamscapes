#pragma once

#include <iiSharedCanvas/Document/Document.h>
#include <memory>
#include <string>

struct EditorFrameSnapshot {
    std::shared_ptr<const iiSharedCanvas::Document> document;
    iiSharedCanvas::FrameIndex frame = 0;
    std::string artboardId;
};
