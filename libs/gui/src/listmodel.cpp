#include <firmir/gui/listmodel.hpp>

#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

namespace {

/// A default-constructed std::any of the same category as @p value (for the
/// roles a sparse row does not carry).
std::any defaultValueFor(const std::any& value) {
    if (value.type() == typeid(bool))
        return std::any(false);
    if (value.type() == typeid(double) || value.type() == typeid(float) ||
        value.type() == typeid(int) || value.type() == typeid(long long))
        return std::any(0.0);
    if (value.type() == typeid(std::string))
        return std::any(std::string{});
    if (value.type() == typeid(String))
        return std::any(String{});
    if (value.type() == typeid(Color))
        return std::any(Color{});
    return {};
}

} // namespace

class ListModel::Impl {
public:
    /// Default value of a role (its type learned from the first row carrying
    /// it), for sparse rows.
    std::map<std::string, std::any> roleDefaults;
};

ListModel::ListModel(Object* parent)
    : AbstractDataList(parent), d(std::make_unique<Impl>()) {}

ListModel::~ListModel() = default;

void ListModel::append(Row row) {
    for (const auto& [role, value] : row)
        d->roleDefaults.emplace(role, defaultValueFor(value)); // the first row wins
    const int index = count();
    beginInsertItems(index, index);
    endInsertItems(); // notifies "count" itself
    itemAt(index) = std::move(row);
}

void ListModel::clear() {
    if (count() == 0)
        return;
    beginRemoveItems(0, count() - 1);
    endRemoveItems(); // notifies "count" itself
    // d->roleDefaults stays: it describes the role schema, not the rows.
    // Sparse-role lookups (data() of a row missing a role) must keep
    // returning the default after a clear()+append() cycle.
}

std::any ListModel::data(int index, const std::string& role) const {
    if (index < 0 || index >= count())
        return {};
    const Row& row = itemAt(index);
    const auto it = row.find(role);
    if (it != row.end())
        return it->second;
    const auto fallback = d->roleDefaults.find(role);
    return fallback == d->roleDefaults.end() ? std::any{} : fallback->second;
}

} // namespace Gui
} // namespace Firmir
