#include <firmir/gui/positioners.hpp>

#include <firmir/gui/repeater.hpp>

#include <algorithm>
#include <cmath>

namespace Firmir {
namespace Gui {

class Positioner::Impl {
public:
    std::vector<Connection> childConnections;
    float spacing = 0;
    bool layingOut = false;
};

Positioner::Positioner(Object* parent)
    : Item(parent), d(std::make_unique<Impl>()) {
    registerProperty<float>("spacing", &Positioner::spacing,
                            &Positioner::setSpacing);
    connect(this, "childrenChanged",
            [this] {
                rewatchChildren();
                requestLayout();
            },
            ConnectionMode::Direct);
}

Positioner::~Positioner() = default;

float Positioner::spacing() const { return d->spacing; }

void Positioner::setSpacing(float spacing) {
    if (spacing == d->spacing)
        return;
    d->spacing = spacing;
    notifyPropertyChanged("spacing");
    requestLayout();
}

std::vector<Item*> Positioner::effectiveChildren() const {
    std::vector<Item*> out;
    for (Item* child : childItems()) {
        if (auto* repeater = dynamic_cast<Repeater*>(child)) {
            for (Item* delegate : repeater->childItems())
                out.push_back(delegate);
        } else {
            out.push_back(child);
        }
    }
    return out;
}

void Positioner::rewatchChildren() {
    for (Connection& connection : d->childConnections)
        connection.disconnect();
    d->childConnections.clear();
    const auto watchItem = [this](Item* item) {
        d->childConnections.push_back(item->connect<std::string, std::any>(
            this, signalPropertyChanged,
            [this](const std::string& name, const std::any&) {
                if (name == "width" || name == "height" || name == "visible")
                    requestLayout();
            },
            ConnectionMode::Direct));
    };
    for (Item* child : childItems()) {
        auto* repeater = dynamic_cast<Repeater*>(child);
        if (!repeater) {
            watchItem(child);
            continue;
        }
        for (Item* delegate : repeater->childItems())
            watchItem(delegate);
        d->childConnections.push_back(child->connect(this, "childrenChanged",
                                                     [this] {
                                                         rewatchChildren();
                                                         requestLayout();
                                                     },
                                                     ConnectionMode::Direct));
    }
}

void Positioner::requestLayout() {
    if (d->layingOut)
        return;
    d->layingOut = true;
    std::vector<Item*> visible;
    for (Item* child : effectiveChildren())
        if (child->isVisible())
            visible.push_back(child);
    layoutChildren(visible);
    d->layingOut = false;
}

// --------------------------------- Column ---------------------------------

Column::Column(Object* parent) : Positioner(parent) {}

void Column::layoutChildren(const std::vector<Item*>& children) {
    const float gap = spacing();
    float y = 0;
    float maxWidth = 0;
    for (Item* child : children) {
        child->setY(y);
        y += child->height() + gap;
        maxWidth = std::max(maxWidth, child->width());
    }
    if (!children.empty())
        y -= gap;
    setImplicitWidth(maxWidth);
    setImplicitHeight(y);
}

// ---------------------------------- Row -----------------------------------

Row::Row(Object* parent) : Positioner(parent) {}

void Row::layoutChildren(const std::vector<Item*>& children) {
    const float gap = spacing();
    float x = 0;
    float maxHeight = 0;
    for (Item* child : children) {
        child->setX(x);
        x += child->width() + gap;
        maxHeight = std::max(maxHeight, child->height());
    }
    if (!children.empty())
        x -= gap;
    setImplicitWidth(x);
    setImplicitHeight(maxHeight);
}

// ---------------------------------- Grid ----------------------------------

class Grid::Impl {
public:
    int columns = 4;
    int rows = 0;
};

Grid::Grid(Object* parent) : Positioner(parent), d(std::make_unique<Impl>()) {
    registerProperty<int>("columns", &Grid::columns, &Grid::setColumns);
    registerProperty<int>("rows", &Grid::rows, &Grid::setRows);
}

Grid::~Grid() = default;

int Grid::columns() const { return d->columns; }
int Grid::rows() const { return d->rows; }

void Grid::setColumns(int columns) {
    if (columns < 0)
        throw std::invalid_argument("Firmir::Gui::Grid: columns must be >= 0");
    if (columns == d->columns)
        return;
    d->columns = columns;
    notifyPropertyChanged("columns");
    requestLayout();
}

void Grid::setRows(int rows) {
    if (rows < 0)
        throw std::invalid_argument("Firmir::Gui::Grid: rows must be >= 0");
    if (rows == d->rows)
        return;
    d->rows = rows;
    notifyPropertyChanged("rows");
    requestLayout();
}

void Grid::layoutChildren(const std::vector<Item*>& children) {
    if (children.empty()) {
        setImplicitWidth(0);
        setImplicitHeight(0);
        return;
    }
    const int n = static_cast<int>(children.size());
    int cols = d->columns;
    if (cols <= 0)
        cols = d->rows > 0 ? (n + d->rows - 1) / d->rows : 4;
    cols = std::max(1, cols);
    const int rows = (n + cols - 1) / cols;
    const float gap = spacing();

    float cellWidth = 0;
    float cellHeight = 0;
    for (const Item* child : children) {
        cellWidth = std::max(cellWidth, child->width());
        cellHeight = std::max(cellHeight, child->height());
    }
    for (int i = 0; i < n; ++i) {
        Item* child = children[static_cast<size_t>(i)];
        const int row = i / cols;
        const int column = i % cols;
        child->setX(column * (cellWidth + gap) + (cellWidth - child->width()) / 2);
        child->setY(row * (cellHeight + gap) + (cellHeight - child->height()) / 2);
    }
    setImplicitWidth(cols * cellWidth + (cols - 1) * gap);
    setImplicitHeight(rows * cellHeight + (rows - 1) * gap);
}

} // namespace Gui
} // namespace Firmir
