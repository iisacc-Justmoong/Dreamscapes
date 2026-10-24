#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dreamscapes::review {
enum class State { Unreviewed, Shortlisted, Rejected };
enum class SelectionMode { Replace, Toggle, Range, AddRange };

// Session-local presentation policy. No document, filesystem or inference APIs.
class Selection {
public:
    void synchronize(const std::vector<std::string>& incoming) {
        std::unordered_set<std::string> available;
        for (const auto& id : incoming) if (!id.empty()) available.insert(id);
        std::erase_if(m_order, [&](const auto& id) { return !available.contains(id); });
        std::erase_if(m_items, [&](const auto& item) { return !available.contains(item.first); });
        for (const auto& id : incoming) if (!id.empty() && !m_items.contains(id)) {
            m_items.emplace(id, Item{}); m_order.push_back(id);
        }
        if (!contains(m_focus)) m_focus = m_order.empty() ? std::string{} : m_order.front();
        if (!contains(m_anchor)) m_anchor = m_focus;
    }
    const std::vector<std::string>& order() const { return m_order; }
    const std::string& focusedKey() const { return m_focus; }
    bool contains(const std::string& id) const { return m_items.contains(id); }
    bool focus(const std::string& id) {
        if (!contains(id)) return false;
        m_anchor = id;
        if (m_focus == id) return false;
        m_focus = id; return true;
    }
    State state(const std::string& id) const {
        const auto it = m_items.find(id); return it == m_items.end() ? State::Unreviewed : it->second.state;
    }
    bool selected(const std::string& id) const {
        const auto it = m_items.find(id); return it != m_items.end() && it->second.selected;
    }
    std::vector<std::string> selectedKeys() const {
        std::vector<std::string> ids;
        for (const auto& id : m_order) if (selected(id)) ids.push_back(id);
        return ids;
    }
    void clearSelection() { for (auto& [id, item] : m_items) item.selected = false; }
    void selectAll(const std::vector<std::string>& visible) {
        // Select-visible adds to an existing hidden selection.
        for (const auto& id : visible) if (contains(id)) m_items.at(id).selected = true;
    }
    void select(const std::string& id, const std::vector<std::string>& visible, SelectionMode mode) {
        if (!contains(id)) return;
        const auto target = std::find(visible.begin(), visible.end(), id);
        if (target == visible.end()) return;
        m_focus = id;
        if (mode == SelectionMode::Range || mode == SelectionMode::AddRange) {
            auto anchor = std::find(visible.begin(), visible.end(), m_anchor);
            if (anchor == visible.end()) { anchor = target; m_anchor = id; }
            if (mode == SelectionMode::Range) clearSelection();
            const auto first = std::min(anchor, target), last = std::max(anchor, target);
            for (auto it = first; it != last + 1; ++it) if (contains(*it)) m_items.at(*it).selected = true;
        } else {
            const bool wasSelected = selected(id);
            if (mode == SelectionMode::Replace) clearSelection();
            m_items.at(id).selected = mode == SelectionMode::Replace || !wasSelected;
            m_anchor = id;
        }
    }
    void setState(const std::vector<std::string>& ids, State value) {
        for (const auto& id : ids) if (contains(id)) m_items.at(id).state = value;
    }
    void toggleState(const std::vector<std::string>& ids, State value) {
        if (ids.empty()) return;
        const bool allSet = std::all_of(ids.begin(), ids.end(), [&](const auto& id) { return state(id) == value; });
        setState(ids, allSet ? State::Unreviewed : value);
    }
private:
    struct Item { State state = State::Unreviewed; bool selected = false; };
    std::vector<std::string> m_order;
    std::unordered_map<std::string, Item> m_items;
    std::string m_focus, m_anchor;
};
}
