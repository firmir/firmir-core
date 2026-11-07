#pragma once

#include <firmir/gui/abstractdatalist.hpp>

#include <any>
#include <map>
#include <string>

namespace Firmir {
namespace Gui {

/**
 * @brief Static list of named role values (the Firml ListModel backing type).
 *
 * Each row is a role name -> value map; rows are appended at build time
 * (firmlc compiles ListModel/ListElement declarations into append() calls)
 * and read by Repeater delegates through data(). Structural changes reuse the
 * AbstractDataList begin/end API, so views can subscribe to itemsInserted and
 * friends.
 */
class ListModel : public AbstractDataList<std::map<std::string, std::any>> {
public:
    using Row = std::map<std::string, std::any>;

    explicit ListModel(Object* parent = nullptr);

    /// Appends one row of role values (emits itemsAboutToBeInserted/itemsInserted).
    void append(Row row);
    /// The value of @p role in row @p index. When the row has no such role
    /// but another row declared it, returns a default-constructed value of the
    /// role's type (0.0, empty string, false, default Color); an empty
    /// std::any when the role is entirely unknown.
    std::any data(int index, const std::string& role) const;

private:
    /// Default value of a role (its type learned from the first row carrying
    /// it), for sparse rows.
    std::map<std::string, std::any> m_roleDefaults;
};

} // namespace Gui
} // namespace Firmir
