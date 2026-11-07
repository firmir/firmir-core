#pragma once

#include <firmir/gui/item.hpp>

#include <vector>

namespace Firmir {
namespace Gui {

/**
 * @brief Base class for the positioner items (Column, Row, Grid).
 *
 * A positioner arranges its visible child items according to its layout rule
 * and keeps the arrangement up to date: it repositions the children whenever
 * the child set changes (added/removed items) and whenever a child's width,
 * height or visibility changes. Children created by a Repeater are treated as
 * if they were direct children of the positioner.
 *
 * The positioner's own implicit size is recomputed to fit the layout, so an
 * unset width/height takes the content size.
 */
class Positioner : public Item {
public:
    /// Gap between neighboring children in pixels (default 0).
    float spacing() const;
    void setSpacing(float spacing);

protected:
    explicit Positioner(Object* parent = nullptr);

    /// Repositions the children (in effective-children order) and updates the
    /// implicit size. Called with the scene consistent; must not add/remove
    /// children.
    virtual void layoutChildren(const std::vector<Item*>& children) = 0;
    /// Runs the layout unless already inside one.
    void requestLayout();
    /// The children taking part in the layout: Repeater children are
    /// flattened to their delegates.
    std::vector<Item*> effectiveChildren() const;

private:
    void rewatchChildren();

    std::vector<Connection> m_childConnections;
    float m_spacing = 0;
    bool m_layingOut = false;
};

/// Positions its children in a vertical column (y stacking; x untouched).
class Column : public Positioner {
public:
    explicit Column(Object* parent = nullptr);

protected:
    void layoutChildren(const std::vector<Item*>& children) override;
};

/// Positions its children in a horizontal row (x stacking; y untouched).
class Row : public Positioner {
public:
    explicit Row(Object* parent = nullptr);

protected:
    void layoutChildren(const std::vector<Item*>& children) override;
};

/**
 * @brief Positions its children in a grid of equal cells.
 *
 * Cells are filled row by row, left to right. The cell size fits the largest
 * child; each child is centered in its cell. `columns` defaults to 4; when
 * `rows` is set (and columns is 0), the column count is derived from the row
 * count and the number of children.
 */
class Grid : public Positioner {
public:
    explicit Grid(Object* parent = nullptr);

    /// Number of columns; 0 (with rows set) derives it from the row count.
    int columns() const;
    void setColumns(int columns);
    /// Number of rows; 0 (default) derives it from the column count.
    int rows() const;
    void setRows(int rows);

protected:
    void layoutChildren(const std::vector<Item*>& children) override;

private:
    int m_columns = 4;
    int m_rows = 0;
};

} // namespace Gui
} // namespace Firmir
