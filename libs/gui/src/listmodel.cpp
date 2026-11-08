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

ListModel::ListModel(Object* parent) : AbstractDataList(parent) {}

void ListModel::append(Row row) {
    for (const auto& [role, value] : row)
        m_roleDefaults.emplace(role, defaultValueFor(value)); // the first row wins
    const int index = count();
    beginInsertItems(index, index);
    endInsertItems();
    itemAt(index) = std::move(row);
    notifyPropertyChanged("count"); // Firml bindings (e.g. contentHeight)
}

void ListModel::clear() {
    if (count() == 0)
        return;
    beginRemoveItems(0, count() - 1);
    endRemoveItems();
    m_roleDefaults.clear();
    notifyPropertyChanged("count");
}

std::any ListModel::data(int index, const std::string& role) const {
    if (index < 0 || index >= count())
        return {};
    const Row& row = itemAt(index);
    const auto it = row.find(role);
    if (it != row.end())
        return it->second;
    const auto fallback = m_roleDefaults.find(role);
    return fallback == m_roleDefaults.end() ? std::any{} : fallback->second;
}

} // namespace Gui
} // namespace Firmir
