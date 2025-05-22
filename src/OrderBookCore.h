// file: src/OrderBookCore.h
//================
// OrderBookCore.h
//================

#ifndef EXCHANGE_ORDERBOOKCORE_H
#define EXCHANGE_ORDERBOOKCORE_H

#include <iostream>
#include <list>
#include <unordered_map>
#include <memory> // For std::unique_ptr
#include <iterator> // For std::prev, std::next, std::reverse_iterator
#include <set>
#include <cassert>
#include <vector>
#include <optional>
#include <functional> // For std::greater_equal, std::less_equal
#include "Globals.h"

enum class DOUBLEOPTION { FRONT, BACK };
enum class TRIPLEOPTION { FRONT, BACK, INPLACE };

class LOBOrder {
public:
    LOBOrder(ID_TYPE uoid, SIZE_TYPE quantity) : uoid_(uoid), quantity_(quantity) {}

    LOBOrder(const LOBOrder&) = default;
    LOBOrder(LOBOrder&&) noexcept = default;
    LOBOrder& operator=(const LOBOrder&) = delete;
    LOBOrder& operator=(LOBOrder&&) noexcept = delete;

    SIZE_TYPE quantity_;
    const ID_TYPE uoid_;
};

class LOBFillResult {
public:
    LOBFillResult(ID_TYPE uoid_maker, SIZE_TYPE quantity, bool exhausted)
            : uoid_maker_(uoid_maker), quantity_(quantity), exhausted_(exhausted) {}

    LOBFillResult(const LOBFillResult&) = default;
    LOBFillResult(LOBFillResult&&) noexcept = default;
    LOBFillResult& operator=(const LOBFillResult&) = delete;
    LOBFillResult& operator=(LOBFillResult&&) noexcept = delete;

    const ID_TYPE uoid_maker_;
    const SIZE_TYPE quantity_;
    const bool exhausted_;
};

class LOBClearResult {
public:
    LOBClearResult(PRICE_TYPE price, std::vector<LOBFillResult> trades)
            : price_(price), trades_(std::move(trades)) {}

    LOBClearResult(const LOBClearResult&) = default;
    LOBClearResult(LOBClearResult&&) noexcept = default;
    LOBClearResult& operator=(const LOBClearResult&) = delete;
    LOBClearResult& operator=(LOBClearResult&&) noexcept = delete;

    const PRICE_TYPE price_;
    std::vector<LOBFillResult> trades_;
};

class ModifyVolResult {
public:
    PRICE_TYPE price;
    SIZE_TYPE old_volume;
    SIZE_TYPE new_volume;
    bool removed;
    std::optional<ID_TYPE> new_uoid;

    ModifyVolResult() = default;
    ModifyVolResult(PRICE_TYPE p, SIZE_TYPE ov, SIZE_TYPE nv, bool r, std::optional<ID_TYPE> nuoid = std::nullopt)
            : price(p), old_volume(ov), new_volume(nv), removed(r), new_uoid(nuoid) {}

    ModifyVolResult(const ModifyVolResult&) = default;
    ModifyVolResult(ModifyVolResult&&) noexcept = default;
    ModifyVolResult& operator=(const ModifyVolResult&) = default;
    ModifyVolResult& operator=(ModifyVolResult&&) noexcept = default;
};

class ModifyPriceResult {
public:
    PRICE_TYPE before_price;
    SIZE_TYPE volume_of_new_order;
    std::optional<ID_TYPE> new_uoid;

    ModifyPriceResult() = default;
    ModifyPriceResult(PRICE_TYPE bp, SIZE_TYPE v, std::optional<ID_TYPE> nuoid)
            : before_price(bp), volume_of_new_order(v), new_uoid(nuoid) {}

    ModifyPriceResult(const ModifyPriceResult&) = default;
    ModifyPriceResult(ModifyPriceResult&&) noexcept = default;
    ModifyPriceResult& operator=(const ModifyPriceResult&) = default;
    ModifyPriceResult& operator=(ModifyPriceResult&&) noexcept = default;
};

class ModifyPriceVolResult {
public:
    PRICE_TYPE before_price;
    SIZE_TYPE old_volume;
    SIZE_TYPE new_volume_at_new_price;
    bool old_price_level_removed;
    std::optional<ID_TYPE> new_uoid;

    ModifyPriceVolResult() = default;
    ModifyPriceVolResult(PRICE_TYPE bp, SIZE_TYPE ov, SIZE_TYPE nv, bool old_pl_rem, std::optional<ID_TYPE> nuoid)
            : before_price(bp), old_volume(ov), new_volume_at_new_price(nv),
              old_price_level_removed(old_pl_rem), new_uoid(nuoid) {}

    ModifyPriceVolResult(const ModifyPriceVolResult&) = default;
    ModifyPriceVolResult(ModifyPriceVolResult&&) noexcept = default;
    ModifyPriceVolResult& operator=(const ModifyPriceVolResult&) = default;
    ModifyPriceVolResult& operator=(ModifyPriceVolResult&&) noexcept = default;
};

class ReplaceOrderResult {
public:
    PRICE_TYPE price_of_old_order;
    SIZE_TYPE old_volume;
    bool old_order_effectively_removed;


    ReplaceOrderResult() = default;
    ReplaceOrderResult(PRICE_TYPE p, SIZE_TYPE ov, bool r)
            : price_of_old_order(p), old_volume(ov), old_order_effectively_removed(r) {}

    ReplaceOrderResult(const ReplaceOrderResult&) = default;
    ReplaceOrderResult(ReplaceOrderResult&&) noexcept = default;
    ReplaceOrderResult& operator=(const ReplaceOrderResult&) = default;
    ReplaceOrderResult& operator=(ReplaceOrderResult&&) noexcept = default;
};


class OrderContainer {
public:
    std::list<LOBOrder>::iterator addOrder(ID_TYPE uoid, SIZE_TYPE quantity) {
        orders_.emplace_back(uoid, quantity);
        auto list_it = std::prev(orders_.end());
        index_[uoid] = list_it;
        return list_it;
    }

    std::list<LOBOrder>::iterator addOrderToFront(ID_TYPE uoid, SIZE_TYPE quantity) {
        orders_.emplace_front(uoid, quantity);
        auto list_it = orders_.begin();
        index_[uoid] = list_it;
        return list_it;
    }

    std::list<LOBOrder>::iterator addOrderAtPosition(std::list<LOBOrder>::iterator pos, ID_TYPE uoid, SIZE_TYPE quantity) {
        auto list_it = orders_.emplace(pos, uoid, quantity);
        index_[uoid] = list_it;
        return list_it;
    }

    std::optional<LOBOrder> removeOrder(ID_TYPE uoid) {
        auto map_it = index_.find(uoid);
        if (map_it != index_.end()) {
            auto list_it = map_it->second;
            LOBOrder removedOrderData = *list_it;
            orders_.erase(list_it);
            index_.erase(map_it);
            return removedOrderData;
        }
        return std::nullopt;
    }

    std::list<LOBOrder>::iterator erase(std::list<LOBOrder>::iterator list_it) {
        if (list_it != orders_.end()) {
            index_.erase(list_it->uoid_);
            return orders_.erase(list_it);
        }
        return orders_.end();
    }

    LOBOrder* getOrder(ID_TYPE uoid) {
        auto it = index_.find(uoid);
        if (it != index_.end()) {
            return &(*(it->second));
        }
        return nullptr;
    }

    const LOBOrder* getOrder(ID_TYPE uoid) const {
        auto it = index_.find(uoid);
        if (it != index_.end()) {
            return &(*(it->second));
        }
        return nullptr;
    }

    std::optional<std::list<LOBOrder>::iterator> getOrderListIterator(ID_TYPE uoid) {
        auto map_it = index_.find(uoid);
        if (map_it != index_.end()) {
            return map_it->second;
        }
        return std::nullopt;
    }

    std::optional<std::list<LOBOrder>::const_iterator> getOrderListIterator(ID_TYPE uoid) const {
        auto map_it = index_.find(uoid);
        if (map_it != index_.end()) {
            return map_it->second;
        }
        return std::nullopt;
    }

    auto begin() { return orders_.begin(); }
    auto end() { return orders_.end(); }
    [[nodiscard]] auto begin() const { return orders_.cbegin(); }
    [[nodiscard]] auto end() const { return orders_.cend(); }
    auto rbegin() { return orders_.rbegin(); }
    auto rend() { return orders_.rend(); }
    [[nodiscard]] auto rbegin() const { return orders_.crbegin(); }
    [[nodiscard]] auto rend() const { return orders_.crend(); }


    void clear() {
        orders_.clear();
        index_.clear();
    }

    bool empty() const { return orders_.empty(); }
    size_t size() const { return orders_.size(); }

private:
    std::list<LOBOrder> orders_;
    std::unordered_map<ID_TYPE, std::list<LOBOrder>::iterator> index_;
};

class Price {
public:
    const PRICE_TYPE price_;
    SIZE_TYPE total_quantity_ = 0;
    OrderContainer container;

    explicit Price(PRICE_TYPE price) : price_(price) {}

    SIZE_TYPE get_total_quantity() const {
        return total_quantity_;
    }

    LOBOrder* get_order(ID_TYPE target_uoid) {
        return container.getOrder(target_uoid);
    }
    const LOBOrder* get_order(ID_TYPE target_uoid) const {
        return container.getOrder(target_uoid);
    }

    template <DOUBLEOPTION i>
    std::list<LOBOrder>::iterator insert_order(ID_TYPE uoid, SIZE_TYPE quantity) {
        std::list<LOBOrder>::iterator order_it;
        if constexpr (i == DOUBLEOPTION::BACK) {
            order_it = container.addOrder(uoid, quantity);
        } else if constexpr (i == DOUBLEOPTION::FRONT) {
            order_it = container.addOrderToFront(uoid, quantity);
        }
        total_quantity_ += quantity;
        return order_it;
    }

    std::list<LOBOrder>::iterator insert_order_at_position(std::list<LOBOrder>::iterator pos, ID_TYPE uoid, SIZE_TYPE quantity) {
        auto order_it = container.addOrderAtPosition(pos, uoid, quantity);
        total_quantity_ += quantity;
        return order_it;
    }

    std::optional<LOBOrder> remove_order_from_container(ID_TYPE uoid) {
        std::optional<LOBOrder> removed_order_data = container.removeOrder(uoid);
        if (removed_order_data) {
            total_quantity_ -= removed_order_data->quantity_;
        }
        return removed_order_data;
    }


    template <DOUBLEOPTION i>
    LOBClearResult clear_quantity(SIZE_TYPE& quantity_to_clear, std::vector<ID_TYPE>& exhausted_order_uoids_at_this_level) {
        std::vector<LOBFillResult> trades;
        exhausted_order_uoids_at_this_level.clear();

        if constexpr (i == DOUBLEOPTION::FRONT) {
            auto it = container.begin();
            while (it != container.end() && quantity_to_clear > 0) {
                LOBOrder& currentOrder = *it;
                SIZE_TYPE tradeQuantity = std::min(quantity_to_clear, currentOrder.quantity_);

                quantity_to_clear -= tradeQuantity;
                currentOrder.quantity_ -= tradeQuantity;
                total_quantity_ -= tradeQuantity;

                bool exhausted = (currentOrder.quantity_ == 0);
                trades.emplace_back(currentOrder.uoid_, tradeQuantity, exhausted);

                if (exhausted) {
                    exhausted_order_uoids_at_this_level.push_back(currentOrder.uoid_);
                    it = container.erase(it);
                } else {
                    ++it;
                }
            }
        } else if constexpr (i == DOUBLEOPTION::BACK) {
            auto it = container.rbegin();
            while (it != container.rend() && quantity_to_clear > 0) {
                LOBOrder& currentOrder = *it;
                SIZE_TYPE tradeQuantity = std::min(quantity_to_clear, currentOrder.quantity_);

                quantity_to_clear -= tradeQuantity;
                currentOrder.quantity_ -= tradeQuantity;
                total_quantity_ -= tradeQuantity;

                bool exhausted = (currentOrder.quantity_ == 0);
                trades.emplace_back(currentOrder.uoid_, tradeQuantity, exhausted);

                if (exhausted) {
                    exhausted_order_uoids_at_this_level.push_back(currentOrder.uoid_);
                    auto fwd_iter_to_erase = std::prev(it.base());
                    auto fwd_iter_after_erase = container.erase(fwd_iter_to_erase);
                    it = std::reverse_iterator(fwd_iter_after_erase);
                } else {
                    ++it;
                }
            }
        }
        return LOBClearResult(price_, std::move(trades));
    }

    bool operator<(const Price& other) const {
        return price_ < other.price_;
    }
};


struct PriceUniquePtrCompareAscending {
    using is_transparent = void;
    bool operator()(const std::unique_ptr<Price>& lhs, const std::unique_ptr<Price>& rhs) const {
        return lhs->price_ < rhs->price_;
    }
    bool operator()(const std::unique_ptr<Price>& lhs, PRICE_TYPE rhs_price) const {
        return lhs->price_ < rhs_price;
    }
    bool operator()(PRICE_TYPE lhs_price, const std::unique_ptr<Price>& rhs) const {
        return lhs_price < rhs->price_;
    }
};

struct PriceUniquePtrCompareDescending {
    using is_transparent = void;
    bool operator()(const std::unique_ptr<Price>& lhs, const std::unique_ptr<Price>& rhs) const {
        return lhs->price_ > rhs->price_;
    }
    bool operator()(const std::unique_ptr<Price>& lhs, PRICE_TYPE rhs_price) const {
        return lhs->price_ > rhs_price;
    }
    bool operator()(PRICE_TYPE lhs_price, const std::unique_ptr<Price>& rhs) const {
        return lhs_price > rhs->price_;
    }
};


class OrderBookCore {
private:
    static ID_TYPE next_uoid_;

    const PriceUniquePtrCompareAscending comp_asc_unique_ptr_{};
    const PriceUniquePtrCompareDescending comp_desc_unique_ptr_{};

    std::set<std::unique_ptr<Price>, PriceUniquePtrCompareDescending> buy_prices_;
    std::set<std::unique_ptr<Price>, PriceUniquePtrCompareAscending> sell_prices_;
    std::unordered_map<ID_TYPE, Price*> uoid_to_price_;



    template<typename BookType>
    void erase_price_level_if_empty(BookType& book, PRICE_TYPE price_key) {
        auto it_to_erase = book.find(price_key);
        if (it_to_erase != book.end() && (*it_to_erase)->get_total_quantity() == 0) {
            book.erase(it_to_erase);
        }
    }

public:
    OrderBookCore() = default;

    ID_TYPE generate_uoid() {
        return next_uoid_++;
    }

    size_t get_num_orders() const {
        return uoid_to_price_.size();
    }

    template <typename CompUniquePtr>
    auto& get_orderbook() {
        if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareAscending>) return sell_prices_;
        else if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareDescending>) return buy_prices_;
    }

    template <typename CompUniquePtr>
    const auto& get_orderbook_const() const {
        if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareAscending>) return sell_prices_;
        else if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareDescending>) return buy_prices_;
    }

    template <typename CompUniquePtr>
    auto& get_counter_orderbook() {
        if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareAscending>) return buy_prices_;
        else if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareDescending>) return sell_prices_;
    }

    template <typename CompUniquePtr>
    const auto& get_counter_orderbook_const() const {
        if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareAscending>) return buy_prices_;
        else if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareDescending>) return sell_prices_;
    }

    template <typename CompUniquePtr>
    [[nodiscard]] auto get_counter_price_val_comparator() const {
        if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareAscending>) return std::greater_equal<PRICE_TYPE>();
        else if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareDescending>) return std::less_equal<PRICE_TYPE>();
    }

    template <typename CompUniquePtr>
    [[nodiscard]] const auto& get_price_inst_comparator() const {
        if constexpr (std::is_same_v<CompUniquePtr, PriceUniquePtrCompareAscending>) return comp_asc_unique_ptr_;
        else  return comp_desc_unique_ptr_;
    }


    template <typename CompUniquePtr, DOUBLEOPTION FillOrderPriority>
    std::tuple<SIZE_TYPE, std::vector<LOBClearResult>> limit_match_price_quantity(PRICE_TYPE price, SIZE_TYPE quantity) {
        std::vector<LOBClearResult> clearings;
        auto price_val_comparator = get_counter_price_val_comparator<CompUniquePtr>();
        auto& counter_book = get_counter_orderbook<CompUniquePtr>();

        std::vector<ID_TYPE> exhausted_uoids_from_level;

        auto it = counter_book.begin();
        while (it != counter_book.end() && quantity > 0 && price_val_comparator((*it)->price_, price)) {
            Price* price_node = it->get();
            exhausted_uoids_from_level.clear();
            LOBClearResult result = price_node->template clear_quantity<FillOrderPriority>(quantity, exhausted_uoids_from_level);

            if(!result.trades_.empty()){
                clearings.push_back(std::move(result));
            }

            for (ID_TYPE uoid_to_remove : exhausted_uoids_from_level) {
                uoid_to_price_.erase(uoid_to_remove);
            }

            if (price_node->get_total_quantity() == 0) {
                it = counter_book.erase(it);
            } else {
                ++it;
            }
        }
        return {quantity, std::move(clearings)};
    }

    template <typename CompUniquePtr, DOUBLEOPTION BookOrderPriority>
    std::optional<std::tuple<ID_TYPE, Price*>> book_price_quantity(PRICE_TYPE price, SIZE_TYPE quantity) {
        if (quantity <= 0) {
            return std::nullopt;
        }

        auto& my_book = get_orderbook<CompUniquePtr>();

        auto it = my_book.lower_bound(price);
        Price* pricenode_ptr = nullptr;

        if (it != my_book.end() && (*it)->price_ == price) {
            pricenode_ptr = it->get();
        } else {
            auto new_price_obj_uptr = std::make_unique<Price>(price);
            pricenode_ptr = new_price_obj_uptr.get();
            it = my_book.insert(it, std::move(new_price_obj_uptr));
        }

        ID_TYPE new_uoid = generate_uoid();
        pricenode_ptr->template insert_order<BookOrderPriority>(new_uoid, quantity);
        uoid_to_price_[new_uoid] = pricenode_ptr;

        return std::make_tuple(new_uoid, pricenode_ptr);
    }

    template <typename CompUniquePtr, DOUBLEOPTION FillOrderPriority, DOUBLEOPTION BookOrderPriority>
    std::tuple<std::optional<std::tuple<ID_TYPE, Price*>>, SIZE_TYPE, std::vector<LOBClearResult>>
    limit_match_book_price_quantity(PRICE_TYPE price, SIZE_TYPE quantity) {
        auto [remaining_quantity, clearings] = limit_match_price_quantity<CompUniquePtr, FillOrderPriority>(price, quantity);

        std::optional<std::tuple<ID_TYPE, Price*>> placed_order_info;
        if (remaining_quantity > 0) {
            placed_order_info = book_price_quantity<CompUniquePtr, BookOrderPriority>(price, remaining_quantity);
        }
        return {placed_order_info, remaining_quantity, std::move(clearings)};
    }

    template <typename CompUniquePtr, DOUBLEOPTION FillOrderPriority>
    std::tuple<SIZE_TYPE, std::vector<LOBClearResult>> market_match_quantity(SIZE_TYPE quantity) {
        std::vector<LOBClearResult> clearings;
        auto& counter_book = get_counter_orderbook<CompUniquePtr>();
        std::vector<ID_TYPE> exhausted_uoids_from_level;

        auto it = counter_book.begin();
        while (it != counter_book.end() && quantity > 0) {
            Price* price_node = it->get();
            exhausted_uoids_from_level.clear();
            LOBClearResult result = price_node->template clear_quantity<FillOrderPriority>(quantity, exhausted_uoids_from_level);
            if(!result.trades_.empty()){
                clearings.push_back(std::move(result));
            }
            for (ID_TYPE uoid_to_remove : exhausted_uoids_from_level) {
                uoid_to_price_.erase(uoid_to_remove);
            }
            if (price_node->get_total_quantity() == 0) {
                it = counter_book.erase(it);
            } else {
                ++it;
            }
        }
        return {quantity, std::move(clearings)};
    }

    template <typename CompUniquePtr>
    std::optional<std::tuple<PRICE_TYPE, SIZE_TYPE>> delete_limit_order(ID_TYPE target_uoid) {
        auto map_it = uoid_to_price_.find(target_uoid);
        if (map_it != uoid_to_price_.end()) {
            Price* pricenode = map_it->second;
            PRICE_TYPE price_of_pricenode = pricenode->price_;

            std::optional<LOBOrder> order_removed_data = pricenode->remove_order_from_container(target_uoid);
            if (order_removed_data) {
                uoid_to_price_.erase(map_it);
                if (pricenode->get_total_quantity() == 0) {
                    erase_price_level_if_empty(get_orderbook<CompUniquePtr>(), price_of_pricenode);
                }
                return std::make_tuple(price_of_pricenode, order_removed_data->quantity_);
            }
        }
        return std::nullopt;
    }


    template <typename CompUniquePtr, TRIPLEOPTION PriorityOption>
    std::optional<ModifyVolResult> modify_limit_order_vol(ID_TYPE order_id, SIZE_TYPE new_volume) {
        auto map_it = uoid_to_price_.find(order_id);
        if (map_it == uoid_to_price_.end()) {
            return std::nullopt;
        }
        Price* pricenode = map_it->second;
        LOBOrder* order = pricenode->get_order(order_id);

        if (!order) {
            return std::nullopt;
        }

        SIZE_TYPE old_volume = order->quantity_;
        PRICE_TYPE current_price = pricenode->price_;
        std::optional<ID_TYPE> new_uoid_opt = std::nullopt;
        bool removed = false;

        if (new_volume <= 0) {
            pricenode->remove_order_from_container(order_id);
            uoid_to_price_.erase(order_id);
            removed = true;
            if (pricenode->get_total_quantity() == 0) {
                erase_price_level_if_empty(get_orderbook<CompUniquePtr>(), current_price);
            }
            return ModifyVolResult(current_price, old_volume, 0, removed, std::nullopt);
        }

        if constexpr (PriorityOption == TRIPLEOPTION::INPLACE) {
            pricenode->total_quantity_ -= old_volume;
            pricenode->total_quantity_ += new_volume;
            order->quantity_ = new_volume;
            new_uoid_opt = order_id;
        } else {
            pricenode->remove_order_from_container(order_id);
            uoid_to_price_.erase(order_id);

            ID_TYPE new_gen_uoid = generate_uoid();
            new_uoid_opt = new_gen_uoid;

            if constexpr (PriorityOption == TRIPLEOPTION::FRONT) {
                pricenode->template insert_order<DOUBLEOPTION::FRONT>(new_gen_uoid, new_volume);
            } else {
                pricenode->template insert_order<DOUBLEOPTION::BACK>(new_gen_uoid, new_volume);
            }
            uoid_to_price_[new_gen_uoid] = pricenode;
        }
        return ModifyVolResult(current_price, old_volume, new_volume, removed, new_uoid_opt);
    }

    template <typename CompUniquePtr, TRIPLEOPTION PriorityOption>
    std::optional<ModifyVolResult> remove_limit_order_vol(ID_TYPE order_id, SIZE_TYPE cancel_amount) {
        auto map_it = uoid_to_price_.find(order_id);
        if (map_it == uoid_to_price_.end()) return std::nullopt;

        Price* pricenode = map_it->second;
        LOBOrder* order = pricenode->get_order(order_id);
        if(!order) return std::nullopt;

        SIZE_TYPE new_volume = (cancel_amount >= order->quantity_) ? 0 : (order->quantity_ - cancel_amount);
        return modify_limit_order_vol<CompUniquePtr, PriorityOption>(order_id, new_volume);
    }


    template <typename CompUniquePtr, TRIPLEOPTION PriorityOption>
    std::optional<std::tuple<ID_TYPE, ReplaceOrderResult>> replace_limit_order_vol(ID_TYPE order_id_old, SIZE_TYPE volume_new) {
        auto map_it = uoid_to_price_.find(order_id_old);
        if (map_it == uoid_to_price_.end()) {
            return std::nullopt;
        }
        Price* pricenode = map_it->second;

        auto opt_old_order_list_iter = pricenode->container.getOrderListIterator(order_id_old);
        if(!opt_old_order_list_iter){
            return std::nullopt;
        }
        std::list<LOBOrder>::iterator old_order_list_iter = *opt_old_order_list_iter;
        SIZE_TYPE old_volume = old_order_list_iter->quantity_;
        PRICE_TYPE price_val = pricenode->price_;

        ID_TYPE order_id_new = generate_uoid();

        pricenode->total_quantity_ -= old_volume;
        auto next_iter_hint_for_inplace_insert = pricenode->container.erase(old_order_list_iter);
        uoid_to_price_.erase(order_id_old);

        bool old_order_effectively_removed = true;

        if (volume_new <= 0) {
            if (pricenode->get_total_quantity() == 0) {
                erase_price_level_if_empty(get_orderbook<CompUniquePtr>(), price_val);
            }
            return std::make_tuple(order_id_new, ReplaceOrderResult(price_val, old_volume, old_order_effectively_removed));
        }

        if constexpr (PriorityOption == TRIPLEOPTION::INPLACE) {
            pricenode->insert_order_at_position(next_iter_hint_for_inplace_insert, order_id_new, volume_new);
        } else if constexpr (PriorityOption == TRIPLEOPTION::FRONT) {
            pricenode->template insert_order<DOUBLEOPTION::FRONT>(order_id_new, volume_new);
        } else {
            pricenode->template insert_order<DOUBLEOPTION::BACK>(order_id_new, volume_new);
        }
        uoid_to_price_[order_id_new] = pricenode;

        return std::make_tuple(order_id_new, ReplaceOrderResult(price_val, old_volume, old_order_effectively_removed));
    }


    template <typename CompUniquePtr, TRIPLEOPTION PriorityOption>
    std::optional<ModifyPriceResult> modify_limit_order_price(PRICE_TYPE new_price, ID_TYPE order_id_old) {
        auto map_it = uoid_to_price_.find(order_id_old);
        if (map_it == uoid_to_price_.end()) {
            return std::nullopt;
        }

        Price* old_pricenode = map_it->second;
        const LOBOrder* old_order_const_ptr = old_pricenode->get_order(order_id_old);
        assert(old_order_const_ptr != nullptr && "OrderBookCore: Inconsistency - UOID in map but not in Price level.");
        assert(old_order_const_ptr->quantity_ > 0 && "OrderBookCore: Inconsistency - Resting order has zero or negative volume.");

        PRICE_TYPE old_price = old_pricenode->price_;
        SIZE_TYPE original_volume = old_order_const_ptr->quantity_;
        ID_TYPE final_uoid = order_id_old; // For INPLACE, this will remain order_id_old

        if constexpr (PriorityOption == TRIPLEOPTION::INPLACE) {
            if (old_price == new_price) {
                // Price hasn't changed, no-op for priority, UOID preserved.
                return ModifyPriceResult(old_price, original_volume, order_id_old);
            }
        }

        // If execution reaches here, the order will be moved or re-booked.
        // Remove the order from its current location.
        auto removed_data = old_pricenode->remove_order_from_container(order_id_old);
        assert(removed_data.has_value() && removed_data->uoid_ == order_id_old && "Failed to remove order for price change.");
        uoid_to_price_.erase(order_id_old); // Remove from global UOID map

        if (old_pricenode->get_total_quantity() == 0) {
            erase_price_level_if_empty(get_orderbook<CompUniquePtr>(), old_price);
        }

        // Re-book the order
        if constexpr (PriorityOption == TRIPLEOPTION::INPLACE) {
            // For INPLACE with price change, re-book with the SAME UOID at the back of the new price level.
            Price* new_pricenode_ptr = nullptr;
            auto& target_book = get_orderbook<CompUniquePtr>();
            auto it_new_price_level = target_book.lower_bound(new_price);

            if (it_new_price_level != target_book.end() && (*it_new_price_level)->price_ == new_price) {
                new_pricenode_ptr = it_new_price_level->get();
            } else {
                auto new_price_obj_uptr = std::make_unique<Price>(new_price);
                new_pricenode_ptr = new_price_obj_uptr.get();
                target_book.insert(it_new_price_level, std::move(new_price_obj_uptr));
            }
            // Insert with original UOID (order_id_old) at the back of the queue.
            new_pricenode_ptr->template insert_order<DOUBLEOPTION::BACK>(order_id_old, original_volume);
            uoid_to_price_[order_id_old] = new_pricenode_ptr; // Re-map the original UOID to the new price node.
            // final_uoid is already order_id_old, which is intended.
        } else { // TRIPLEOPTION::FRONT or TRIPLEOPTION::BACK
            // For FRONT/BACK, a new UOID is generated by book_price_quantity.
            constexpr DOUBLEOPTION bookPriority = (PriorityOption == TRIPLEOPTION::FRONT) ? DOUBLEOPTION::FRONT : DOUBLEOPTION::BACK;
            auto book_result_tuple_opt = book_price_quantity<CompUniquePtr, bookPriority>(new_price, original_volume);
            assert(book_result_tuple_opt.has_value());
            final_uoid = std::get<0>(*book_result_tuple_opt); // New UOID from booking
        }
        return ModifyPriceResult(old_price, original_volume, final_uoid);
    }

    template <typename CompUniquePtr, TRIPLEOPTION PriorityOption>
    std::optional<ModifyPriceVolResult> modify_limit_order_price_vol(PRICE_TYPE new_price, SIZE_TYPE new_volume, ID_TYPE order_id_old) {
        auto map_it = uoid_to_price_.find(order_id_old);
        if (map_it == uoid_to_price_.end()) {
            return std::nullopt;
        }

        Price* old_pricenode = map_it->second;
        LOBOrder* old_order_modifiable_ptr = old_pricenode->get_order(order_id_old);
        assert(old_order_modifiable_ptr != nullptr && "OrderBookCore: Inconsistency - UOID in map but not in Price level (price_vol).");
        assert(old_order_modifiable_ptr->quantity_ > 0 && "OrderBookCore: Inconsistency - Resting order has zero or negative volume (price_vol).");

        PRICE_TYPE old_price = old_pricenode->price_;
        SIZE_TYPE old_volume = old_order_modifiable_ptr->quantity_;
        ID_TYPE final_uoid = order_id_old;
        bool old_level_removed_flag = false;

        if (new_volume <= 0) {
            auto removed_data = old_pricenode->remove_order_from_container(order_id_old);
            assert(removed_data.has_value() && removed_data->uoid_ == order_id_old);
            uoid_to_price_.erase(order_id_old);
            if (old_pricenode->get_total_quantity() == 0) {
                erase_price_level_if_empty(get_orderbook<CompUniquePtr>(), old_price);
                old_level_removed_flag = true;
            }
            return ModifyPriceVolResult(old_price, old_volume, 0, old_level_removed_flag, std::nullopt);
        }

        if constexpr (PriorityOption == TRIPLEOPTION::INPLACE) {
            if (old_price == new_price) {
                old_pricenode->total_quantity_ -= old_volume;
                old_pricenode->total_quantity_ += new_volume;
                old_order_modifiable_ptr->quantity_ = new_volume;
                return ModifyPriceVolResult(old_price, old_volume, new_volume, false, order_id_old);
            }
        }

        auto removed_data = old_pricenode->remove_order_from_container(order_id_old);
        assert(removed_data.has_value() && removed_data->uoid_ == order_id_old);
        uoid_to_price_.erase(order_id_old);
        if (old_pricenode->get_total_quantity() == 0) {
            erase_price_level_if_empty(get_orderbook<CompUniquePtr>(), old_price);
            old_level_removed_flag = true;
        }

        bool generateNewUoid = true; // Default for FRONT/BACK or INPLACE with price change

        if constexpr (PriorityOption == TRIPLEOPTION::INPLACE) {
            generateNewUoid = false;
            final_uoid = order_id_old;
        } else {
            final_uoid = generate_uoid(); // Generate for FRONT/BACK
        }

        Price* new_pricenode_ptr = nullptr;
        auto& target_book = get_orderbook<CompUniquePtr>();
        auto it_new_price_level = target_book.lower_bound(new_price);

        if (it_new_price_level != target_book.end() && (*it_new_price_level)->price_ == new_price) {
            new_pricenode_ptr = it_new_price_level->get();
        } else {
            auto new_price_obj_uptr = std::make_unique<Price>(new_price);
            new_pricenode_ptr = new_price_obj_uptr.get();
            target_book.insert(it_new_price_level, std::move(new_price_obj_uptr));
        }

        // Corrected: Use explicit DOUBLEOPTION based on PriorityOption
        if constexpr (PriorityOption == TRIPLEOPTION::INPLACE) { // INPLACE (with price change) goes to back of new queue
            new_pricenode_ptr->template insert_order<DOUBLEOPTION::BACK>(final_uoid, new_volume);
        } else if constexpr (PriorityOption == TRIPLEOPTION::FRONT) {
            new_pricenode_ptr->template insert_order<DOUBLEOPTION::FRONT>(final_uoid, new_volume);
        } else { // TRIPLEOPTION::BACK
            new_pricenode_ptr->template insert_order<DOUBLEOPTION::BACK>(final_uoid, new_volume);
        }
        uoid_to_price_[final_uoid] = new_pricenode_ptr;

        return ModifyPriceVolResult(old_price, old_volume, new_volume, old_level_removed_flag, final_uoid);
    }

    std::optional<PRICE_TYPE> get_price_of_order(ID_TYPE uoid) const {
        auto it = uoid_to_price_.find(uoid);
        if (it != uoid_to_price_.end()) {
            return it->second->price_;
        }
        return std::nullopt;
    }

    const LOBOrder* get_order(ID_TYPE target_uoid) const {
        auto it = uoid_to_price_.find(target_uoid);
        if (it != uoid_to_price_.end()) {
            return it->second->get_order(target_uoid);
        }
        return nullptr;
    }

    void flush() {
        buy_prices_.clear();
        sell_prices_.clear();
        uoid_to_price_.clear();
        next_uoid_ = 1;
    }

    std::pair<std::vector<PRICE_SIZE_TYPE>, std::vector<PRICE_SIZE_TYPE>> get_state_l2() const {
        std::vector<PRICE_SIZE_TYPE> bids, asks;
        bids.reserve(buy_prices_.size() * 2);
        asks.reserve(sell_prices_.size() * 2);

        for (const auto& priceUPtr : buy_prices_) {
            bids.push_back(priceUPtr->price_);
            bids.push_back(priceUPtr->get_total_quantity());
        }
        for (const auto& priceUPtr : sell_prices_) {
            asks.push_back(priceUPtr->price_);
            asks.push_back(priceUPtr->get_total_quantity());
        }
        return {bids, asks};
    }

    void printOrderBook() const {
        std::cout << "------ SELL SIDE ------ (Price, Total Quantity)" << std::endl;
        for (const auto& priceUPtr : sell_prices_) {
            std::cout << "Price: " << priceUPtr->price_ << ", Qty: " << priceUPtr->get_total_quantity() << std::endl;
        }

        std::cout << "------ BUY SIDE ------ (Price, Total Quantity)" << std::endl;
        for (const auto& priceUPtr : buy_prices_) {
            std::cout << "Price: " << priceUPtr->price_ << ", Qty: " << priceUPtr->get_total_quantity() << std::endl;
        }
        std::cout << "======== Orders in uoid_to_price_ map (" << uoid_to_price_.size() << " entries) ======== " << std::endl;
    }

    template <typename CompUniquePtr>
    PRICE_TYPE get_price_for_volume(SIZE_TYPE volume) const {
        SIZE_TYPE remaining_vol = volume;
        PRICE_TYPE current_total_price = 0;
        const auto& counter_book = get_counter_orderbook_const<CompUniquePtr>();
        for (const auto& pricenode_uptr : counter_book) {
            if (remaining_vol == 0) break;
            if (remaining_vol >= pricenode_uptr->get_total_quantity()) {
                remaining_vol -= pricenode_uptr->get_total_quantity();
                current_total_price += pricenode_uptr->get_total_quantity() * pricenode_uptr->price_;
            } else {
                current_total_price += remaining_vol * pricenode_uptr->price_;
                remaining_vol = 0;
            }
        }
        if (remaining_vol > 0) return PRICE_DEFAULT;
        return current_total_price;
    }

    template <typename CompUniquePtr>
    SIZE_TYPE get_available_volume() const {
        SIZE_TYPE volume = 0;
        const auto& counter_book = get_counter_orderbook_const<CompUniquePtr>();
        for (const auto& pricenode_uptr : counter_book) {
            volume += pricenode_uptr->get_total_quantity();
        }
        return volume;
    }

    template <typename CompUniquePtr>
    SIZE_TYPE get_volume_for_price(PRICE_TYPE target_price) const {
        SIZE_TYPE current_total_volume = 0;
        const auto& counter_book = get_counter_orderbook_const<CompUniquePtr>();
        auto price_val_comparator = get_counter_price_val_comparator<CompUniquePtr>();

        for (const auto& pricenode_uptr : counter_book) {
            if (price_val_comparator(pricenode_uptr->price_, target_price)) {
                current_total_volume += pricenode_uptr->get_total_quantity();
            } else {
                break;
            }
        }
        return current_total_volume;
    }
};

ID_TYPE OrderBookCore::next_uoid_{1};


class OrderBookWrapper {
private:
    OrderBookCore core_;
    std::unordered_map<ID_TYPE, SIDE> order_side_map_;

public:
    void print_book() const {
        core_.printOrderBook();
    }

    ID_TYPE generate_new_uoid() { return core_.generate_uoid(); }
    size_t get_num_orders() const { return core_.get_num_orders(); }
    const LOBOrder* get_lob_order(ID_TYPE order_id) const { return core_.get_order(order_id); }
    std::optional<SIDE> get_order_side(ID_TYPE order_id) const {
        auto it = order_side_map_.find(order_id);
        if (it != order_side_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    std::optional<PRICE_TYPE> get_price_for_order(ID_TYPE order_id) const {
        return core_.get_price_of_order(order_id);
    }

    template <DOUBLEOPTION FillOrderPrio, DOUBLEOPTION BookOrderPrio>
    auto limit_match_book_price_quantity(SIDE side, PRICE_TYPE price, SIZE_TYPE quantity) {
        if (side == SIDE::BID) {
            auto result = core_.template limit_match_book_price_quantity<PriceUniquePtrCompareDescending, FillOrderPrio, BookOrderPrio>(price, quantity);
            if (std::get<0>(result).has_value()) {
                order_side_map_[std::get<0>(std::get<0>(result).value())] = SIDE::BID;
            }
            return result;
        } else {
            auto result = core_.template limit_match_book_price_quantity<PriceUniquePtrCompareAscending, FillOrderPrio, BookOrderPrio>(price, quantity);
            if (std::get<0>(result).has_value()) {
                order_side_map_[std::get<0>(std::get<0>(result).value())] = SIDE::ASK;
            }
            return result;
        }
    }

    template <DOUBLEOPTION FillOrderPrio>
    auto limit_match_price_quantity(SIDE side, PRICE_TYPE price, SIZE_TYPE quantity) {
        if (side == SIDE::BID) {
            return core_.template limit_match_price_quantity<PriceUniquePtrCompareDescending, FillOrderPrio>(price, quantity);
        } else {
            return core_.template limit_match_price_quantity<PriceUniquePtrCompareAscending, FillOrderPrio>(price, quantity);
        }
    }

    template <DOUBLEOPTION FillOrderPrio>
    auto market_match_quantity(SIDE side, SIZE_TYPE quantity) {
        if (side == SIDE::BID) {
            return core_.template market_match_quantity<PriceUniquePtrCompareDescending, FillOrderPrio>(quantity);
        } else {
            return core_.template market_match_quantity<PriceUniquePtrCompareAscending, FillOrderPrio>(quantity);
        }
    }

    template <DOUBLEOPTION BookOrderPrio>
    auto book_price_quantity(SIDE side, PRICE_TYPE price, SIZE_TYPE quantity) {
        std::optional<std::tuple<ID_TYPE, Price*>> result;
        if (side == SIDE::BID) {
            result = core_.template book_price_quantity<PriceUniquePtrCompareDescending, BookOrderPrio>(price, quantity);
        } else {
            result = core_.template book_price_quantity<PriceUniquePtrCompareAscending, BookOrderPrio>(price, quantity);
        }
        if (result) {
            order_side_map_[std::get<0>(result.value())] = side;
        }
        return result;
    }

    auto delete_limit_order(ID_TYPE target_uoid) {
        auto map_it = order_side_map_.find(target_uoid);
        if (map_it == order_side_map_.end()) {
            return std::optional<std::tuple<PRICE_TYPE, SIZE_TYPE>>{};
        }
        SIDE order_side = map_it->second;

        std::optional<std::tuple<PRICE_TYPE, SIZE_TYPE>> result;
        if (order_side == SIDE::BID) {
            result = core_.template delete_limit_order<PriceUniquePtrCompareDescending>(target_uoid);
        } else {
            result = core_.template delete_limit_order<PriceUniquePtrCompareAscending>(target_uoid);
        }

        if (result) {
            order_side_map_.erase(map_it);
        }
        return result;
    }

    template <TRIPLEOPTION PrioOpt>
    auto modify_limit_order_vol(ID_TYPE order_id, SIZE_TYPE new_volume) {
        auto map_it = order_side_map_.find(order_id);
        if (map_it == order_side_map_.end()) {
            return std::optional<ModifyVolResult>{};
        }
        SIDE order_side = map_it->second;

        std::optional<ModifyVolResult> result;
        if (order_side == SIDE::BID) {
            result = core_.template modify_limit_order_vol<PriceUniquePtrCompareDescending, PrioOpt>(order_id, new_volume);
        } else {
            result = core_.template modify_limit_order_vol<PriceUniquePtrCompareAscending, PrioOpt>(order_id, new_volume);
        }

        if (result) {
            if (result->removed) {
                order_side_map_.erase(order_id);
            } else if (result->new_uoid && result->new_uoid.value() != order_id) {
                order_side_map_.erase(order_id);
                order_side_map_[result->new_uoid.value()] = order_side;
            }
        }
        return result;
    }

    template <TRIPLEOPTION PrioOpt>
    auto remove_limit_order_vol(ID_TYPE order_id, SIZE_TYPE cancel_amount) {
        auto map_it = order_side_map_.find(order_id);
        if (map_it == order_side_map_.end()) {
            return std::optional<ModifyVolResult>{};
        }
        SIDE order_side = map_it->second;
        std::optional<ModifyVolResult> result;

        if (order_side == SIDE::BID) {
            result = core_.template remove_limit_order_vol<PriceUniquePtrCompareDescending, PrioOpt>(order_id, cancel_amount);
        } else {
            result = core_.template remove_limit_order_vol<PriceUniquePtrCompareAscending, PrioOpt>(order_id, cancel_amount);
        }
         if (result) {
            if (result->removed) {
                order_side_map_.erase(order_id);
            } else if (result->new_uoid && result->new_uoid.value() != order_id) {
                order_side_map_.erase(order_id);
                order_side_map_[result->new_uoid.value()] = order_side;
            }
        }
        return result;
    }

    template <TRIPLEOPTION PrioOpt>
    auto replace_limit_order_vol(ID_TYPE order_id_old, SIZE_TYPE volume_new) {
        auto map_it = order_side_map_.find(order_id_old);
        if (map_it == order_side_map_.end()) {
            return std::optional<std::tuple<ID_TYPE, ReplaceOrderResult>>{};
        }
        SIDE order_side = map_it->second;

        std::optional<std::tuple<ID_TYPE, ReplaceOrderResult>> result;
        if (order_side == SIDE::BID) {
            result = core_.template replace_limit_order_vol<PriceUniquePtrCompareDescending, PrioOpt>(order_id_old, volume_new);
        } else {
            result = core_.template replace_limit_order_vol<PriceUniquePtrCompareAscending, PrioOpt>(order_id_old, volume_new);
        }

        if (result) {
            order_side_map_.erase(order_id_old);
            ID_TYPE new_uoid_from_core = std::get<0>(result.value());
            if(volume_new > 0) {
                order_side_map_[new_uoid_from_core] = order_side;
            }
        }
        return result;
    }

    template <TRIPLEOPTION NewOrderPrio>
    auto modify_limit_order_price_vol(ID_TYPE order_id, PRICE_TYPE price, SIZE_TYPE volume) {
        auto map_it = order_side_map_.find(order_id);
        if (map_it == order_side_map_.end()) {
            return std::optional<ModifyPriceVolResult>{};
        }
        SIDE order_side = map_it->second;

        std::optional<ModifyPriceVolResult> result;
        if (order_side == SIDE::BID) {
            result = core_.template modify_limit_order_price_vol<PriceUniquePtrCompareDescending, NewOrderPrio>(price, volume, order_id);
        } else {
            result = core_.template modify_limit_order_price_vol<PriceUniquePtrCompareAscending, NewOrderPrio>(price, volume, order_id);
        }

        if (result) {
            order_side_map_.erase(order_id);
            if (result->new_uoid) {
                order_side_map_[result->new_uoid.value()] = order_side;
            }
        }
        return result;
    }

    template <TRIPLEOPTION NewOrderPrio>
    auto modify_limit_order_price(ID_TYPE order_id, PRICE_TYPE price) {
        auto map_it = order_side_map_.find(order_id);
        if (map_it == order_side_map_.end()) {
            return std::optional<ModifyPriceResult>{};
        }
        SIDE order_side = map_it->second;

        std::optional<ModifyPriceResult> result;
        if (order_side == SIDE::BID) {
            result = core_.template modify_limit_order_price<PriceUniquePtrCompareDescending, NewOrderPrio>(price, order_id);
        } else {
            result = core_.template modify_limit_order_price<PriceUniquePtrCompareAscending, NewOrderPrio>(price, order_id);
        }

        if (result) {
            order_side_map_.erase(order_id);
            if (result->new_uoid) {
                order_side_map_[result->new_uoid.value()] = order_side;
            }
        }
        return result;
    }

    void flush() {
        core_.flush();
        order_side_map_.clear();
    }

    auto get_state_l2() const {
        return core_.get_state_l2();
    }
};

#endif //EXCHANGE_ORDERBOOKCORE_H