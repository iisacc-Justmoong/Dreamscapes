#include "App/Views/Result/GenerationReviewModel.h"
#include "App/Views/Result/NativeReviewSession.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QUrl>
#include <QVariant>
#include <iostream>
#include <random>
#include <stdexcept>

using dreamscapes::review::Session;
using dreamscapes::review::State;
using dreamscapes::vm::Array;
using dreamscapes::vm::Object;
using dreamscapes::vm::Value;
namespace {
void require(bool result, const std::string &message) {
    if (!result)
        throw std::runtime_error(message);
}
Value value(const QVariant &v) {
    if (!v.isValid() || v.isNull())
        return {};
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        return Value(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
        return Value(v.toLongLong());
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return Value(v.toULongLong());
    case QMetaType::Double:
        return Value(v.toDouble());
    case QMetaType::QVariantMap: {
        Object result;
        const auto m = v.toMap();
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            result.emplace(it.key().toStdString(), value(it.value()));
        return Value(std::move(result));
    }
    case QMetaType::QVariantList: {
        Array result;
        for (const auto &x : v.toList())
            result.push_back(value(x));
        return Value(std::move(result));
    }
    default:
        return Value(v.toString().toStdString());
    }
}
std::vector<Value> values(const QVariantList &items) {
    std::vector<Value> result;
    for (const auto &x : items)
        result.push_back(value(x));
    return result;
}
QVariantMap entry(int i) {
    return {{"id", QString::number(i)},
            {"documentSource", QStringLiteral("file:///review/%1.iisc").arg(i)},
            {"mediaType", "Image"},
            {"seed", i},
            {"prompt", QStringLiteral("Prompt %1").arg(i)}};
}
void compare(Session &native, GenerationReviewModel &legacy, const std::string &stage) {
    auto check = [&](bool ok, const std::string &what) { require(ok, stage + ": " + what); };
    check(native.totalCount() == std::size_t(legacy.totalCount()), "total count");
    check(native.visibleCount() == std::size_t(legacy.rowCount()), "visible count");
    check(native.selectedCount() == std::size_t(legacy.selectedCount()), "selection count");
    check(native.shortlistedCount() == std::size_t(legacy.shortlistedCount()), "shortlist count");
    check(native.rejectedCount() == std::size_t(legacy.rejectedCount()), "rejection count");
    check(native.hiddenSelectedCount() == std::size_t(legacy.hiddenSelectedCount()), "hidden selection count");
    check(native.focusedKey() == legacy.focusedKey().toStdString(),
          "focused key: native=" + native.focusedKey() + " legacy=" + legacy.focusedKey().toStdString());
    check(native.focusedIndex() == legacy.focusedIndex(),
          "focused index: native=" + std::to_string(native.focusedIndex()) +
              " legacy=" + std::to_string(legacy.focusedIndex()));
    check(native.selectionMode() == legacy.selectionMode(), "selection mode");
    check(native.entries() == values(legacy.entries()), "original entry sequence");
    check(native.selectedEntries() == values(legacy.selectedEntries()), "selected entries");
    check(native.selectedImages() == values(legacy.selectedImages()), "selected images");
    check(native.actionEntries() == values(legacy.actionEntries()), "action entries");
    for (int i = 0; i < legacy.rowCount(); ++i) {
        check(native.keyAt(i) == legacy.keyAt(i).toStdString(), "visible key at " + std::to_string(i));
        check(native.entryAt(i) == value(legacy.entryAt(i)), "visible entry at " + std::to_string(i));
        const auto state = native.reviewState(native.keyAt(i));
        const auto expected = state == State::Shortlisted ? "shortlisted"
                              : state == State::Rejected  ? "rejected"
                                                          : "unreviewed";
        check(legacy.reviewState(legacy.keyAt(i)) == expected, "review state at " + std::to_string(i));
        const auto row = native.rowAt(i);
        check(bool(row), "row projection missing");
        check(row->ordinal == legacy.data(legacy.index(i), GenerationReviewModel::OrdinalRole).toInt(), "row ordinal");
        check(row->title == legacy.data(legacy.index(i), GenerationReviewModel::TitleRole).toString().toStdString(),
              "row title");
        check(row->thumbnail == value(legacy.data(legacy.index(i), GenerationReviewModel::ThumbnailRole)),
              "row thumbnail");
        check(row->selected == legacy.data(legacy.index(i), GenerationReviewModel::SelectedRole).toBool(),
              "row selection");
        check(row->focused == legacy.data(legacy.index(i), GenerationReviewModel::FocusedRole).toBool(), "row focus");
    }
    const auto a = native.inspectorRows();
    const auto b = legacy.inspectorRows();
    check(a.size() == std::size_t(b.size()), "inspector row count");
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto expected = b[qsizetype(i)].toMap();
        check(a[i].label == expected.value("label").toString().toStdString(), "inspector label");
        check(a[i].value == expected.value("value").toString().toStdString(),
              "inspector value " + a[i].label + ": native=" + a[i].value +
                  " legacy=" + expected.value("value").toString().toStdString());
        check(a[i].mixed == expected.value("mixed").toBool(), "inspector mixed " + a[i].label);
    }
}
void parity() {
    Session n;
    GenerationReviewModel q;
    QVariantMap a = entry(0), b = entry(1), c = entry(2), d = entry(3);
    a["width"] = 1024;
    a["height"] = 768;
    a["startedAt"] = "2026-10-24T00:00:00.000Z";
    a["finishedAt"] = "2026-10-24T00:00:01.250Z";
    a["advancedParameters"] = QVariantMap{{"cfgScale", 4.5}, {"sampler", "euler"}};
    c["mediaType"] = "Video";
    c["mediaSource"] = QUrl("file:///review/Video%20outputs/2.mp4");
    QVariantList items{a, b, c, d};
    n.replaceEntries(values(items));
    q.setEntries(items);
    compare(n, q, "initial");
    n.selectAt(0);
    q.selectAt(0);
    compare(n, q, "single selection and metadata");
    n.selectAt(2, false, true);
    q.selectAt(2, Qt::ControlModifier);
    compare(n, q, "multiple selection");
    n.setReview(State::Shortlisted);
    q.setReview("shortlisted");
    compare(n, q, "shortlist");
    n.setFilter("rejected");
    q.setFilter("rejected");
    compare(n, q, "hidden selection");
    n.setFilter("all");
    q.setFilter("all");
    items.insert(0, entry(9));
    items.append(entry(10));
    n.replaceEntries(values(items));
    q.setEntries(items);
    compare(n, q, "arrivals");
    n.setSort("newest");
    q.setSort("newest");
    compare(n, q, "newest");
    n.setSort("seed");
    q.setSort("seed");
    compare(n, q, "seed sort");
    n.selectAt(0);
    q.selectAt(0);
    n.selectAt(2, true, false);
    q.selectAt(2, Qt::ShiftModifier);
    compare(n, q, "range");
    n.setReview(State::Rejected);
    q.setReview("rejected");
    n.setFilter("rejected");
    q.setFilter("rejected");
    compare(n, q, "filtered range");
    n.focusSource("file:///review/Video%20outputs/2.mp4?preview=4");
    q.focusSource("file:///review/Video%20outputs/2.mp4?preview=4");
    compare(n, q, "source focus");
    n.clearSelection();
    q.clearSelection();
    compare(n, q, "clear");
    n.replaceEntries({});
    q.setEntries({});
    compare(n, q, "empty");
    QVariantMap uri{{"id", "url"}, {"imageSource", "file:///review/a%20b.png?v=1"}};
    require(Session::keyForEntry(value(uri)) == q.keyForEntry(uri).toStdString(), "encoded source identity parity");
    uri["imageSource"] = "file:///review/a%2Fb.png?v=1";
    require(Session::keyForEntry(value(uri)) == q.keyForEntry(uri).toStdString(), "reserved source identity parity");
}
void randomizedParity() {
    Session n;
    GenerationReviewModel q;
    QVariantList items;
    for (int i = 0; i < 24; ++i) {
        auto e = entry(i);
        if (i % 4 == 0)
            e["mediaType"] = "Video";
        items.push_back(e);
    }
    n.replaceEntries(values(items));
    q.setEntries(items);
    std::mt19937 random(67123);
    const std::vector<QString> filters{"all", "unreviewed", "shortlisted", "rejected", "images", "videos"};
    const std::vector<QString> sorts{"arrival", "newest", "seed"};
    for (int step = 0; step < 200; ++step) {
        const int op = int(random() % 10), count = q.rowCount();
        const int index = count ? int(random() % unsigned(count)) : 0;
        if (op == 0 && count) {
            n.selectAt(index);
            q.selectAt(index);
        } else if (op == 1 && count) {
            n.selectAt(index, false, true);
            q.selectAt(index, Qt::ControlModifier);
        } else if (op == 2 && count) {
            n.selectAt(index, true, false);
            q.selectAt(index, Qt::ShiftModifier);
        } else if (op == 3 && count) {
            n.focusAt(index);
            q.focusAt(index);
        } else if (op == 4) {
            const auto &f = filters[random() % filters.size()];
            n.setFilter(f.toStdString());
            q.setFilter(f);
        } else if (op == 5) {
            const auto &s = sorts[random() % sorts.size()];
            n.setSort(s.toStdString());
            q.setSort(s);
        } else if (op == 6) {
            n.toggleReview(State::Shortlisted);
            q.toggleReview("shortlisted");
        } else if (op == 7) {
            n.setReview(State::Rejected);
            q.setReview("rejected");
        } else if (op == 8) {
            n.selectAll();
            q.selectAll();
        } else if (op == 9) {
            n.clearSelection();
            q.clearSelection();
        }
        compare(n, q, "random sequence " + std::to_string(step));
    }
}
void inputParity() {
    GenerationReviewModel legacy;
    bool sourceIdentitiesMatch = true;
    for (const auto &source :
         {"FILE:///review/a%20b.png?preview=1#part", "https://EXAMPLE.COM/Path%2FImage.png?token=x",
          "file:///review/%ED%95%9C%EA%B8%80.png", "file:///review/a%3Fb.png", "file:///review/a%25b.png",
          "/review/simple name.png", "file:///review/percent%.png", "file:///review/partial%a.png",
          "file:///review/space%20percent%.png", "file:///review/a%20b.png?version=%",
          "file:///review/a%20b.png#partial%"}) {
        QVariantMap item{{"id", "case"}, {"imageSource", source}};
        const auto native = Session::keyForEntry(value(item));
        const auto qt = legacy.keyForEntry(item).toStdString();
        if (native != qt) {
            std::cerr << "source identity: " << source << " native=" << native << " legacy=" << qt << '\n';
            sourceIdentitiesMatch = false;
        }
    }
    require(sourceIdentitiesMatch, "source identity comparison failed");
    for (const QVariant &width : {QVariant(" 1024 "), QVariant(1024.5), QVariant(1024), QVariant(-1)}) {
        auto item = entry(0);
        item["width"] = width;
        item["height"] = 768;
        Session n;
        GenerationReviewModel q;
        QVariantList items{item};
        n.replaceEntries(values(items));
        q.setEntries(items);
        n.selectAt(0);
        q.selectAt(0);
        compare(n, q, "resolution input " + width.toString().toStdString());
    }
    {
        auto item = entry(0);
        item["advancedParameters"] =
            QVariantMap{{"nested", QVariantMap{{"empty", QVariantList{}},
                                               {"emptyObject", QVariantMap{}},
                                               {"prompt", QString::fromUtf8("한글 \\\"title\\\"")}}},
                        {"values", QVariantList{1, 2.5, true, QStringLiteral("line\\nbreak")}}};
        Session n;
        GenerationReviewModel q;
        QVariantList items{item};
        n.replaceEntries(values(items));
        q.setEntries(items);
        n.selectAt(0);
        q.selectAt(0);
        compare(n, q, "nested settings");
    }
    QVariantList items;
    for (const QVariant &number : {QVariant(" +10 "), QVariant("-8"), QVariant(3), QVariant("invalid"), QVariant(12.5),
                                   QVariant("+-1"), QVariant::fromValue(qulonglong(9223372036854775808ULL))}) {
        auto item = entry(int(items.size()));
        item["seed"] = number;
        items.push_back(item);
    }
    Session n;
    GenerationReviewModel q;
    n.replaceEntries(values(items));
    q.setEntries(items);
    n.setSort("seed");
    q.setSort("seed");
    compare(n, q, "seed inputs");
    for (const auto &type : {"Image", "Video", "Canvas", ""}) {
        for (const QVariant &image : {QVariant(false), QVariant(true), QVariant(0), QVariant(1), QVariant("false"),
                                      QVariant("true"), QVariant("0"), QVariant("1"), QVariant("other")}) {
            for (const bool hasDocument : {false, true}) {
                auto item = entry(0);
                if (!hasDocument)
                    item.remove("documentSource");
                item["mediaType"] = type;
                item["imageSource"] = "file:///review/image.png";
                item["isImage"] = image;
                const bool expected =
                    item.value("mediaType").toString() != "Video" && item.value("mediaType").toString() != "Canvas" &&
                    (item.value("isImage", true).toBool() || !item.value("documentSource").toString().isEmpty());
                const QVariantList single{item};
                require(Session::validImages(values(single)) == expected && legacy.validImages(single) == expected,
                        std::string("image eligibility conversion: ") + type + " " + image.toString().toStdString());
            }
        }
    }
    QVariantMap numericId{{"id", 12}, {"imageSource", "file:///review/image.png"}};
    require(Session::keyForEntry(value(numericId)) == legacy.keyForEntry(numericId).toStdString(),
            "numeric id conversion");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        parity();
        randomizedParity();
        inputParity();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Native review and existing Qt model agree on review workflows\n";
}
