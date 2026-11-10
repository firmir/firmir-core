#pragma once

#include <firmir/gui/abstractdatalist.hpp>

#include <any>
#include <map>
#include <memory>
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
    ~ListModel() override;

    /// Appends one row of role values (emits itemsAboutToBeInserted/itemsInserted;
    /// the row data is in place before itemsInserted fires).
    void append(Row row);
    /// Inserts one row at @p index (same signals as append()).
    void insertRow(int index, Row row);
    /// Removes the row at @p index (emits itemsAboutToBeRemoved/itemsRemoved).
    void removeAt(int index);
    /// Replaces the value of @p role in row @p index and emits
    /// itemsDataChanged(index, index) so views update the single delegate.
    void setData(int index, const std::string& role, std::any value);
    /// Removes all rows (emits itemsAboutToBeRemoved/itemsRemoved when
    /// non-empty). The learned role defaults survive: they describe the
    /// role schema, so sparse-role lookups keep returning typed defaults
    /// after a clear()+append() cycle.
    void clear();
    /// The value of @p role in row @p index. When the row has no such role
    /// but another row declared it, returns a default-constructed value of the
    /// role's type (0.0, empty string, false, default Color); an empty
    /// std::any when the role is entirely unknown.
    std::any data(int index, const std::string& role) const;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
