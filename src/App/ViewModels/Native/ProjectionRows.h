#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>
#include <set>

namespace dreamscapes::vm {
enum class RowChangeKind { insert, move, update, remove };
struct RowChange {
    RowChangeKind kind;
    std::size_t from = 0, to = 0, count = 1;
    Value row;
};
struct RowProjection {
    Value previous, next;
    std::string key;
};
struct RowPlan {
    bool valid = true;
    std::vector<RowChange> changes;
};
class RowProjector final {
  public:
    RowProjector()
        : queries_([](const RowProjection &request) {
              RowPlan plan;
              auto rows = array(request.previous);
              const auto &entries = array(request.next);
              std::set<std::string> ids;
              for (const auto &row : entries) {
                  const auto id = text(get(row, request.key));
                  if (id.empty() || !ids.insert(id).second)
                      return RowPlan{false, {}};
              }
              for (std::size_t index = 0; index < entries.size(); ++index) {
                  const auto &entry = entries[index];
                  const auto id = text(get(entry, request.key));
                  std::size_t found = index;
                  while (found < rows.size() && text(get(rows[found], request.key)) != id)
                      ++found;
                  if (found == rows.size()) {
                      rows.insert(rows.begin() + index, entry);
                      plan.changes.push_back({RowChangeKind::insert, index, index, 1, entry});
                  } else {
                      if (found != index) {
                          auto old = rows[found];
                          rows.erase(rows.begin() + found);
                          rows.insert(rows.begin() + index, old);
                          plan.changes.push_back({RowChangeKind::move, found, index});
                      }
                      if (rows[index] != entry) {
                          rows[index] = entry;
                          plan.changes.push_back({RowChangeKind::update, index, index, 1, entry});
                      }
                  }
              }
              if (rows.size() > entries.size())
                  plan.changes.push_back(
                      {RowChangeKind::remove, entries.size(), entries.size(), rows.size() - entries.size()});
              return plan;
          }) {}
    const ii::runtime::QueryPort<RowProjection, RowPlan> &queries() const {
        return queries_;
    }

  private:
    ii::runtime::QueryPort<RowProjection, RowPlan> queries_;
};
} // namespace dreamscapes::vm
