#pragma once

#include <firmir/object.hpp>

#include <algorithm>
#include <memory>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Firmir {
namespace Gui {

/**
 * @brief Abstract base for an owning list of data items with structural
 * change notifications.
 *
 * The model stores `count` items of type ItemT and exposes them through
 * itemAt(index). Structural changes are bracketed by begin/end calls from the
 * subclass; the base class performs the storage mutation in the end call and
 * emits the matching signal pair, so observers always see a consistent state:
 *
 *   class ScoresModel : public AbstractDataList<Score> {
 *   public:
 *       void addScore(const Score& score) {
 *           const int index = count();
 *           beginInsertItems(index, index);
 *           endInsertItems();               // the item appears, itemsInserted fires
 *           itemAt(index) = score;
 *       }
 *   };
 *
 * count is a registered read-only property (propertyChanged fires on change).
 *
 * Move destination semantics: the index the first moved item occupies after
 * the move (in post-removal indexing), so moving items [0, 0] to 1 in
 * [A B C] yields [B A C].
 */
template <typename ItemT>
class AbstractDataList : public Object {
public:
    static constexpr const char* signalItemsAboutToBeInserted =
        "itemsAboutToBeInserted";
    static constexpr const char* signalItemsInserted = "itemsInserted";
    static constexpr const char* signalItemsAboutToBeMoved =
        "itemsAboutToBeMoved";
    static constexpr const char* signalItemsMoved = "itemsMoved";
    static constexpr const char* signalItemsAboutToBeRemoved =
        "itemsAboutToBeRemoved";
    static constexpr const char* signalItemsRemoved = "itemsRemoved";
    static constexpr const char* signalModelAboutToBeReset = "modelAboutToBeReset";
    static constexpr const char* signalModelReset = "modelReset";

    /// Number of items.
    int count() const { return static_cast<int>(d->items.size()); }

    /// The item at @p index; bounds-checked.
    ItemT& itemAt(int index) { return d->items.at(checkedIndex(index)); }
    const ItemT& itemAt(int index) const {
        return d->items.at(checkedIndex(index));
    }

protected:
    /// The base class is abstract in spirit: only subclasses may be created.
    explicit AbstractDataList(Object* parent = nullptr)
        : Object(parent), d(std::make_unique<Impl>()) {
        registerPropertyImpl(
            "count", typeid(int),
            [](const Object* object) -> std::any {
                return static_cast<const AbstractDataList*>(object)->count();
            },
            [](Object*, const std::any&) {
                throw std::invalid_argument("count is read-only");
            });
    }

    /// Inserts default-constructed items [first, last] (the indices the new
    /// items will occupy). Valid range: 0 <= first <= count, first <= last.
    void beginInsertItems(int first, int last) {
        if (first < 0 || first > count() || last < first)
            throw std::out_of_range(
                "Firmir::AbstractDataList: invalid range for insert items");
        begin(StructureChange::Insert, first, last, -1);
        emitSignal(signalItemsAboutToBeInserted, first, last);
    }
    void endInsertItems() {
        const Pending pending = end(StructureChange::Insert);
        const int count = pending.last - pending.first + 1;
        d->items.insert(d->items.begin() + pending.first, count, ItemT{});
        notifyPropertyChanged("count");
        emitSignal(signalItemsInserted, pending.first, pending.last);
    }

    /// Moves items [sourceFirst, sourceLast] so that they start at
    /// @p destinationIndex (post-removal indexing,
    /// 0 <= destinationIndex <= count - (sourceLast - sourceFirst + 1)).
    void beginMoveItems(int sourceFirst, int sourceLast, int destinationIndex) {
        checkRange(sourceFirst, sourceLast, count(), "move items");
        const int blockSize = sourceLast - sourceFirst + 1;
        if (destinationIndex < 0 || destinationIndex > count() - blockSize)
            throw std::out_of_range(
                "Firmir::AbstractDataList: invalid destination for move items");
        begin(StructureChange::Move, sourceFirst, sourceLast, destinationIndex);
        emitSignal(signalItemsAboutToBeMoved, sourceFirst,
                                  sourceLast, destinationIndex);
    }
    void endMoveItems() {
        const Pending pending = end(StructureChange::Move);
        const int count = pending.last - pending.first + 1;
        const auto first = d->items.begin() + pending.first;
        const auto last = first + count;
        std::vector<ItemT> block;
        block.reserve(count);
        std::move(first, last, std::back_inserter(block));
        d->items.erase(first, last);
        d->items.insert(d->items.begin() + pending.destination,
                       std::make_move_iterator(block.begin()),
                       std::make_move_iterator(block.end()));
        emitSignal(signalItemsMoved, pending.first, pending.last,
                                  pending.destination);
    }

    /// Removes items [first, last] (0 <= first <= last < count).
    void beginRemoveItems(int first, int last) {
        checkRange(first, last, count(), "remove items");
        begin(StructureChange::Remove, first, last, -1);
        emitSignal(signalItemsAboutToBeRemoved, first, last);
    }
    void endRemoveItems() {
        const Pending pending = end(StructureChange::Remove);
        const auto first = d->items.begin() + pending.first;
        d->items.erase(first, first + (pending.last - pending.first + 1));
        notifyPropertyChanged("count");
        emitSignal(signalItemsRemoved, pending.first, pending.last);
    }

    /// Brackets a full content replacement: call clearItems() (and any own
    /// state reset) between the two calls, then repopulate through the normal
    /// insert calls after endResetModel().
    void beginResetModel() {
        begin(StructureChange::Reset, -1, -1, -1);
        emitSignal<>(signalModelAboutToBeReset);
    }
    void endResetModel() {
        end(StructureChange::Reset);
        notifyPropertyChanged("count");
        emitSignal<>(signalModelReset);
    }

    /// Drops all items; intended to be called between beginResetModel() and
    /// endResetModel().
    void clearItems() { d->items.clear(); }

private:
    enum class StructureChange { None, Insert, Move, Remove, Reset };
    struct Pending {
        StructureChange change = StructureChange::None;
        int first = -1;
        int last = -1;
        int destination = -1;
    };

    size_t checkedIndex(int index) const {
        if (index < 0 || index >= count())
            throw std::out_of_range(
                "Firmir::AbstractDataList: itemAt() index out of range");
        return static_cast<size_t>(index);
    }

    static void checkRange(int first, int last, int extent, const char* what) {
        if (first < 0 || first >= extent || last < first || last >= extent)
            throw std::out_of_range(std::string("Firmir::AbstractDataList: invalid "
                                                "range for ") +
                                    what);
    }

    void begin(StructureChange change, int first, int last, int destination) {
        if (d->pending.change != StructureChange::None)
            throw std::logic_error(
                "Firmir::AbstractDataList: nested begin* calls are not allowed");
        d->pending = Pending{change, first, last, destination};
    }

    Pending end(StructureChange change) {
        if (d->pending.change != change)
            throw std::logic_error(
                "Firmir::AbstractDataList: end* without the matching begin*");
        const Pending pending = d->pending;
        d->pending = Pending{};
        return pending;
    }

    // Template classes cannot hide Impl in a .cpp; the pointer indirection
    // is kept anyway for uniformity with the rest of the framework.
    struct Impl {
        std::vector<ItemT> items;
        Pending pending;
    };
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir

