#pragma once
#include "Native/ViewCatalog.h"
#include "ViewProjectionModel.h"
#include <QObject>
#include <QVariant>
#include <memory>

// The only Qt-specific layer here is value conversion and QML registration.
// All catalog queries and validation execute in the native iiSDK receiver.
namespace dreamscapes {
vm::Value nativeValue(const QVariant &);
QVariant viewValue(const vm::Value &);
class ViewDataDomain final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList views READ viewsData CONSTANT)
    Q_PROPERTY(QVariantList shapes READ shapes CONSTANT)
    Q_PROPERTY(QVariantList roles READ roles CONSTANT)
    Q_PROPERTY(QVariantList aliases READ aliases CONSTANT)
    Q_PROPERTY(QVariantList usages READ usages CONSTANT)
    Q_PROPERTY(QVariantList typography READ typography CONSTANT)
    Q_PROPERTY(QVariantList layout READ layout CONSTANT)
    Q_PROPERTY(QVariantList appearance READ appearance CONSTANT)
  public:
    ViewDataDomain(vm::ViewCatalog &, vm::Domain, QObject *parent);
    QVariantList viewsData() const;
    QVariantList shapes() const;
    QVariantList roles() const;
    QVariantList aliases() const;
    QVariantList usages() const;
    QVariantList typography() const;
    QVariantList layout() const;
    QVariantList appearance() const;
    Q_INVOKABLE QVariant tool(const QString &) const;
    Q_INVOKABLE QVariant definition(const QString & = {}) const;
    Q_INVOKABLE QVariant viewsFor(const QString &) const;
    Q_INVOKABLE QVariant view(const QString &, const QString & = {}) const;
    Q_INVOKABLE QVariant field(const QString &) const;
    Q_INVOKABLE QVariant defaults(const QVariant & = {}) const;
    Q_INVOKABLE QVariant runtimeDefinition(const QVariant &) const;
    Q_INVOKABLE QVariant applicableFields(const QVariant &, const QVariant &) const;
    Q_INVOKABLE QVariant rows(const QVariant &, bool) const;
    Q_INVOKABLE QVariant catalog() const;
    Q_INVOKABLE QVariant normalizedTool(const QString &) const;
    Q_INVOKABLE QVariant panelsFor(const QString &) const;
    Q_INVOKABLE QVariant byId(const QString &) const;
    Q_INVOKABLE QVariant overviewFor(const QString &) const;
    Q_INVOKABLE QVariant defaultsFor(const QVariant &) const;
    Q_INVOKABLE QVariant operationDefaults(const QString &) const;
    Q_INVOKABLE QVariant controls(const QString &, const QString &) const;
    Q_INVOKABLE QVariant shapeView(const QString &) const;
    Q_INVOKABLE QVariant roleValues(const QVariant &) const;
    Q_INVOKABLE QVariant readout(const QVariant &) const;
    Q_INVOKABLE QVariant fillColor(const QString &, const QString &) const;
    Q_INVOKABLE QVariant sections(const QString &, const QVariant &) const;
    Q_INVOKABLE QVariant source(const QVariant &) const;
    Q_INVOKABLE QVariant key(const QVariant &) const;
    Q_INVOKABLE bool isImage(const QVariant &) const;
    Q_INVOKABLE QVariant historyImages(const QVariant &) const;
    Q_INVOKABLE QVariant kind(const QVariant &) const;
    Q_INVOKABLE QVariant dimensions(const QVariant &) const;
    Q_INVOKABLE QVariant structure(const QVariant &) const;
    Q_INVOKABLE QVariant date(const QVariant &) const;
    Q_INVOKABLE QVariant state(const QVariant &) const;
    Q_INVOKABLE QVariant model(const QVariant &) const;
    Q_INVOKABLE QVariant resultSummary(const QVariant &) const;
    Q_INVOKABLE QVariant value(const QVariant &, const QString &) const;
    Q_INVOKABLE QVariant sectionFiles(const QVariant &, const QString &) const;
    Q_INVOKABLE QVariant listing(const QVariant &, const QString &, const QString &, const QString &,
                                 const QString &) const;
    Q_INVOKABLE QVariant clone(const QVariant &) const;
    Q_INVOKABLE QStringList pathParts(const QString &) const;
    Q_INVOKABLE QVariant getPath(const QVariant &, const QString &) const;
    Q_INVOKABLE QVariant withPath(const QVariant &, const QString &, const QVariant &) const;
    Q_INVOKABLE QVariant nested(const QVariant &, const QVariant & = {}) const;
    Q_INVOKABLE QVariant flatten(const QVariant &, const QString & = {}) const;
    Q_INVOKABLE QString resolvedPath(const QString &, const QVariant &) const;
    Q_INVOKABLE QString numberText(const QVariant &, const QVariant &, bool = false) const;
    Q_INVOKABLE QVariant parseNumber(const QVariant &, const QString &) const;
    Q_INVOKABLE QVariant parsed(const QVariant &, const QString &) const;
    Q_INVOKABLE QString formatted(const QVariant &, const QVariant &, bool) const;
    Q_INVOKABLE QVariant numberInput(const QString &, double minimum, double maximum, const QString &unit = {}) const;
    Q_INVOKABLE QVariant dimensionInput(const QVariant &, int index, const QString &) const;
    Q_INVOKABLE QVariant hexInput(const QString &) const;
    Q_INVOKABLE QVariant buttonRows(double, const QVariant &, int, double, double, bool) const;
    Q_INVOKABLE QVariantList filesOfKind(const QVariantList &, const QString &) const;
    Q_INVOKABLE int fileIndex(const QVariantList &, const QString &) const;
    Q_INVOKABLE QVariantMap fileForKey(const QVariantList &, const QString &, const QString &field = {}) const;
    Q_INVOKABLE QString reconciledFileKey(const QVariantList &, const QString &) const;
    Q_INVOKABLE QVariantMap merged(const QVariantMap &, const QVariantMap &) const;
    Q_INVOKABLE int stateCount(const QVariantList &, const QStringList &) const;
    Q_INVOKABLE QVariantMap firstState(const QVariantList &, const QString &) const;
    Q_INVOKABLE int sectionRowCount(const QVariant &) const;
    Q_INVOKABLE bool containsView(const QString &) const;
    Q_INVOKABLE QVariantList fieldsByType(const QVariant &, const QString &) const;

  private:
    QVariant read(vm::Read, std::initializer_list<QVariant>) const;
    vm::ViewCatalog &catalog_;
    vm::Domain domain_;
};
class ViewData : public QObject {
    Q_OBJECT
    Q_PROPERTY(dreamscapes::ViewProjectionModel *presentation READ presentation CONSTANT)
#define VIEW_DATA_DOMAIN(name) Q_PROPERTY(dreamscapes::ViewDataDomain *name READ name CONSTANT)
    VIEW_DATA_DOMAIN(tools)
    VIEW_DATA_DOMAIN(graphics)
    VIEW_DATA_DOMAIN(graphicState)
    VIEW_DATA_DOMAIN(file)
    VIEW_DATA_DOMAIN(elements) VIEW_DATA_DOMAIN(text) VIEW_DATA_DOMAIN(photo) VIEW_DATA_DOMAIN(compact)
        VIEW_DATA_DOMAIN(viewOptions) VIEW_DATA_DOMAIN(selection) VIEW_DATA_DOMAIN(storage) VIEW_DATA_DOMAIN(facts)
#undef VIEW_DATA_DOMAIN
            public : explicit ViewData(QObject *parent = nullptr);
    ViewProjectionModel *presentation() const { return presentation_; }
#define VIEW_DATA_GETTER(name)                                                                                         \
    ViewDataDomain *name() const {                                                                                     \
        return name##_;                                                                                                \
    }
    VIEW_DATA_GETTER(tools)
    VIEW_DATA_GETTER(graphics)
    VIEW_DATA_GETTER(graphicState)
    VIEW_DATA_GETTER(file)
    VIEW_DATA_GETTER(elements) VIEW_DATA_GETTER(text) VIEW_DATA_GETTER(photo) VIEW_DATA_GETTER(compact)
        VIEW_DATA_GETTER(viewOptions) VIEW_DATA_GETTER(selection) VIEW_DATA_GETTER(storage) VIEW_DATA_GETTER(facts)
#undef VIEW_DATA_GETTER
            vm::ViewCatalog &nativeCatalog() {
        return *catalog_;
    }

  private:
    std::unique_ptr<vm::ViewCatalog> catalog_;
    ViewProjectionModel *presentation_ = nullptr;
    ViewDataDomain *tools_, *graphics_, *graphicState_, *file_, *elements_, *text_, *photo_, *compact_, *viewOptions_,
        *selection_, *storage_, *facts_;
};
void registerNativeViewModelTypes();
} // namespace dreamscapes
