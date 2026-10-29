#pragma once
#include "ViewCatalog.h"

namespace dreamscapes::vm {
struct StorageSnapshot {
    Value aliases;
    std::string section = "files", directory, query, sort = "newest", selectedKey;
};
struct StoragePresentation {
    Value files, entries, selected;
};
class StorageProjector final {
  public:
    StorageProjector()
        : queries_([](const StorageSnapshot &snapshot) {
              StoragePresentation result;
              result.files = sectionFiles(snapshot.aliases, snapshot.section);
              result.entries =
                  storageListing(result.files, snapshot.section, snapshot.directory, snapshot.query, snapshot.sort);
              result.selected = Value(Object{});
              for (const auto &entry : array(result.entries))
                  if (text(get(entry, "key")) == snapshot.selectedKey) {
                      result.selected = entry;
                      break;
                  }
              return result;
          }) {}
    const ii::runtime::QueryPort<StorageSnapshot, StoragePresentation> &queries() const {
        return queries_;
    }

  private:
    ii::runtime::QueryPort<StorageSnapshot, StoragePresentation> queries_;
};
ii::runtime::Runtime &viewRuntime();
} // namespace dreamscapes::vm
