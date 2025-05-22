//================
// Inventory.h
//================

#ifndef TRADING_INVENTORY_H
#define TRADING_INVENTORY_H

#include <iostream> // For temporary logging/errors (replace with proper logger in production)
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>       // For std::unique_ptr, std::make_unique
#include <optional>
#include <stdexcept>
#include <tuple>
#include <sstream>      // For snapshot()
#include <type_traits>  // For std::is_base_of
#include <limits>       // For numeric_limits if needed (though Globals has defaults)
#include <algorithm>    // For potential future use (e.g., std::find_if)
#include <map>          // For sorted snapshot output

// Include the provided Globals.h definitions
#include "Globals.h" // Assuming Globals.h is in the include path

namespace trading {
namespace inventory {

// --- Type Definitions (aligning with Python types and Globals.h) ---

// Assuming SYMBOL_TYPE is represented by a string.
// Potential Optimization: Use integer IDs or std::string_view if symbols are managed elsewhere.
using SYMBOL_TYPE = std::string;

// Using types defined in Globals.h
using QUANTITY_TYPE = SIZE_TYPE;
using PRICE_TYPE = PRICE_TYPE; // Already defined
using CID_TYPE = ID_TYPE; // Assuming Client ID is the same type as general IDs
using SIDE_TYPE = SIDE; // Already defined as enum class

// Using a floating-point type for average price is common, but Globals.h uses PRICE_TYPE (int64_t).
// Let's stick to PRICE_TYPE for consistency with the provided headers, assuming prices are scaled integers.
using AVG_PRICE_TYPE = PRICE_TYPE;


// --- Order Structures (similar to Python dataclasses) ---

// Base Order structure
struct Order {
    CID_TYPE cid;
    SYMBOL_TYPE symbol;
    SIDE_TYPE side;

    // Virtual destructor is crucial for polymorphism with unique_ptr
    virtual ~Order() = default;

    // Helper for snapshot/logging
    virtual std::string get_type_name() const { return "Order"; }
    virtual std::string get_details() const {
        return ", " + symbol + ", " + (side == SIDE::ASK ? "sell" : "buy");
    }


protected:
    // Protected constructor to prevent direct instantiation of base class
    Order(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side)
        : cid(p_cid), symbol(std::move(p_symbol)), side(p_side) {}
};

// Market Order structure
struct MarketOrder : public Order {
    QUANTITY_TYPE quantity;

    MarketOrder(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side, QUANTITY_TYPE p_quantity)
        : Order(p_cid, std::move(p_symbol), p_side), quantity(p_quantity) {}

    std::string get_type_name() const override { return "MarketOrder"; }
    std::string get_details() const override {
        return Order::get_details() + ", Q:" + std::to_string(quantity);
    }
};

// Limit Order structure
struct LimitOrder : public Order {
    PRICE_TYPE price;
    QUANTITY_TYPE quantity;

    LimitOrder(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side, PRICE_TYPE p_price, QUANTITY_TYPE p_quantity)
        : Order(p_cid, std::move(p_symbol), p_side), price(p_price), quantity(p_quantity) {}

    std::string get_type_name() const override { return "LimitOrder"; }
     std::string get_details() const override {
        return Order::get_details() + ", Q:" + std::to_string(quantity) + " @ P:" + std::to_string(price);
    }
};

// --- Cancel Request Structures ---

// Base class for Cancel Requests (optional, but keeps structure)
struct CancelOrderRequest : public Order {
    CID_TYPE cid_target_order;

protected:
    CancelOrderRequest(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side, CID_TYPE p_cid_target_order)
        : Order(p_cid, std::move(p_symbol), p_side), cid_target_order(p_cid_target_order) {}

     std::string get_details() const override {
         return Order::get_details() + ", target=" + std::to_string(cid_target_order);
     }
};

// Limit Order Full Cancel Request
struct LimitOrderFullCancel : public CancelOrderRequest {
    LimitOrderFullCancel(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side, CID_TYPE p_cid_target_order)
        : CancelOrderRequest(p_cid, std::move(p_symbol), p_side, p_cid_target_order) {}

    std::string get_type_name() const override { return "LimitOrderFullCancel"; }
};

// Limit Order Partial Cancel Request
struct LimitOrderPartialCancel : public CancelOrderRequest {
    QUANTITY_TYPE quantity; // Quantity to cancel

    LimitOrderPartialCancel(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side, CID_TYPE p_cid_target_order, QUANTITY_TYPE p_quantity)
        : CancelOrderRequest(p_cid, std::move(p_symbol), p_side, p_cid_target_order), quantity(p_quantity) {}

    std::string get_type_name() const override { return "LimitOrderPartialCancel"; }
     std::string get_details() const override {
         return CancelOrderRequest::get_details() + ", Q:" + std::to_string(quantity);
     }
};

// Market Order Full Cancel Request
struct MarketOrderFullCancel : public CancelOrderRequest {
    MarketOrderFullCancel(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side, CID_TYPE p_cid_target_order)
        : CancelOrderRequest(p_cid, std::move(p_symbol), p_side, p_cid_target_order) {}

     std::string get_type_name() const override { return "MarketOrderFullCancel"; }
};

// Market Order Partial Cancel Request
struct MarketOrderPartialCancel : public CancelOrderRequest {
    QUANTITY_TYPE quantity; // Quantity to cancel

    MarketOrderPartialCancel(CID_TYPE p_cid, SYMBOL_TYPE p_symbol, SIDE_TYPE p_side, CID_TYPE p_cid_target_order, QUANTITY_TYPE p_quantity)
        : CancelOrderRequest(p_cid, std::move(p_symbol), p_side, p_cid_target_order), quantity(p_quantity) {}

    std::string get_type_name() const override { return "MarketOrderPartialCancel"; }
    std::string get_details() const override {
         return CancelOrderRequest::get_details() + ", Q:" + std::to_string(quantity);
     }
};


// --- InventoryCore Class ---

class InventoryCore {
public:
    InventoryCore() {
        // No explicit initialization needed for empty maps
        // TODO: Add proper logging initialization if needed
        // std::cerr << "InventoryCore initialized (using std::cerr for logs)" << std::endl;
    }

    // Explicitly default or delete copy/move operations for clarity/safety
    InventoryCore(const InventoryCore&) = delete;
    InventoryCore& operator=(const InventoryCore&) = delete;
    InventoryCore(InventoryCore&&) = default; // Allow moving
    InventoryCore& operator=(InventoryCore&&) = default; // Allow moving

    ~InventoryCore() = default;

    //--------------------------------------------------------------------------
    // Public Getters
    //--------------------------------------------------------------------------

    /**
     * @brief Retrieve an order by its CID (const reference).
     * @param cid The client ID of the order.
     * @return Constant reference to the order.
     * @throws std::out_of_range If the order with the given CID is not found.
     */
    const Order& get_order_by_cid(CID_TYPE cid) const {
        auto it = orders_by_cid_.find(cid);
        if (it == orders_by_cid_.end()) {
            throw std::out_of_range("Order with CID " + std::to_string(cid) + " not found.");
        }
        return *(it->second);
    }

    /**
     * @brief Get all currently pending orders (new placements and cancellations).
     * @return A vector of pointers to all pending orders. Pointers are valid as long as the InventoryCore exists and the order is not finalized.
     * @note This involves iterating through multiple maps, potentially impacting performance if called frequently in tight loops.
     */
    std::vector<const Order*> get_all_pending_orders() const {
        std::vector<const Order*> res;
        res.reserve(pending_orders_limit_.size() +
                    pending_orders_market_.size() +
                    pending_orders_limit_fullcancel_.size() +
                    pending_orders_limit_partialcancel_.size() +
                    pending_orders_market_fullcancel_.size() +
                    pending_orders_market_partialcancel_.size());

        for(const auto& pair : pending_orders_limit_) res.push_back(pair.second);
        for(const auto& pair : pending_orders_market_) res.push_back(pair.second);
        for(const auto& pair : pending_orders_limit_fullcancel_) res.push_back(pair.second);
        for(const auto& pair : pending_orders_limit_partialcancel_) res.push_back(pair.second);
        for(const auto& pair : pending_orders_market_fullcancel_) res.push_back(pair.second);
        for(const auto& pair : pending_orders_market_partialcancel_) res.push_back(pair.second);

        return res;
    }


    /**
     * @brief Get the CIDs of all pending market orders.
     * @return A vector of CIDs for all pending market orders.
     */
    std::vector<CID_TYPE> get_all_pending_market_orders_cid() const {
        std::vector<CID_TYPE> cids;
        cids.reserve(pending_orders_market_.size());
        for (const auto& pair : pending_orders_market_) {
            cids.push_back(pair.first);
        }
        return cids;
    }

    /**
     * @brief Get the CIDs of all pending limit orders.
     * @return A vector of CIDs for all pending limit orders.
     */
    std::vector<CID_TYPE> get_all_pending_limit_orders_cid() const {
        std::vector<CID_TYPE> cids;
        cids.reserve(pending_orders_limit_.size());
        for (const auto& pair : pending_orders_limit_) {
            cids.push_back(pair.first);
        }
        return cids;
    }

    /**
     * @brief Get the CIDs of all acknowledged market orders.
     * @return A vector of CIDs for all acknowledged market orders.
     */
    std::vector<CID_TYPE> get_all_acknowledged_market_orders_cid() const {
        std::vector<CID_TYPE> cids;
        cids.reserve(acknowledged_orders_market_.size());
        for (const auto& pair : acknowledged_orders_market_) {
            cids.push_back(pair.first);
        }
        return cids;
    }

    /**
     * @brief Get the CIDs of all acknowledged limit orders.
     * @return A vector of CIDs for all acknowledged limit orders.
     */
    std::vector<CID_TYPE> get_all_acknowledged_limit_orders_cid() const {
        std::vector<CID_TYPE> cids;
        cids.reserve(acknowledged_orders_limit_.size());
        for (const auto& pair : acknowledged_orders_limit_) {
            cids.push_back(pair.first);
        }
        return cids;
    }

     /**
     * @brief Get all acknowledged orders (Limit and Market).
     * @return A vector of const pointers to all acknowledged orders. Pointers are valid as long as the InventoryCore exists and the order is not finalized.
     */
    std::vector<const Order*> get_all_acknowledged_orders() const {
        std::vector<const Order*> res;
        res.reserve(acknowledged_orders_limit_.size() + acknowledged_orders_market_.size());

        for(const auto& pair : acknowledged_orders_limit_) res.push_back(pair.second);
        for(const auto& pair : acknowledged_orders_market_) res.push_back(pair.second);

        return res;
    }

    /**
     * @brief Get detailed information about an acknowledged limit order.
     * @param cid_order The client ID of the order.
     * @return An optional tuple containing (cid, symbol, side, price, quantity) if the order is found and is an acknowledged limit order, otherwise std::nullopt.
     */
    std::optional<std::tuple<CID_TYPE, SYMBOL_TYPE, SIDE_TYPE, PRICE_TYPE, QUANTITY_TYPE>>
    get_acknowledged_limit_order_details(CID_TYPE cid_order) const {
        const LimitOrder* target_order = get_acknowledged_limit_order(cid_order);
        if (target_order) {
            return std::make_tuple(
                target_order->cid,
                target_order->symbol,
                target_order->side,
                target_order->price,
                target_order->quantity
            );
        }
        return std::nullopt;
    }

    /**
     * @brief Get detailed information about an acknowledged market order.
     * @param cid_order The client ID of the order.
     * @return An optional tuple containing (cid, symbol, side, quantity) if the order is found and is an acknowledged market order, otherwise std::nullopt.
     */
    std::optional<std::tuple<CID_TYPE, SYMBOL_TYPE, SIDE_TYPE, QUANTITY_TYPE>>
    get_acknowledged_market_order_details(CID_TYPE cid_order) const {
        const MarketOrder* target_order = get_acknowledged_market_order(cid_order);
        if (target_order) {
            return std::make_tuple(
                target_order->cid,
                target_order->symbol,
                target_order->side,
                target_order->quantity
            );
        }
        return std::nullopt;
    }

     /**
     * @brief Check if a limit order with the given CID is currently in an acknowledged state.
     * @param cid_order The client ID of the limit order.
     * @return True if the order is acknowledged, False otherwise.
     */
    bool is_limit_order_acknowledged(CID_TYPE cid_order) const {
         return acknowledged_orders_limit_.count(cid_order) > 0;
    }


    //--------------------------------------------------------------------------
    // Market Orders Logic
    //--------------------------------------------------------------------------

    /**
     * @brief Create a new market order and place it in the pending state.
     * @param cid_order The client ID for the new order. Must be unique.
     * @param symbol The symbol for the order.
     * @param quantity_order The quantity for the order.
     * @param side_order The side of the order (BID or ASK).
     * @throws std::invalid_argument If an order with the given CID already exists.
     */
    void market_order_create_new(CID_TYPE cid_order, const SYMBOL_TYPE& symbol, QUANTITY_TYPE quantity_order, SIDE_TYPE side_order) {
        if (orders_by_cid_.count(cid_order)) {
             throw std::invalid_argument("Order with CID " + std::to_string(cid_order) + " already exists.");
        }

        auto order_ptr = std::make_unique<MarketOrder>(cid_order, symbol, side_order, quantity_order);
        MarketOrder* raw_ptr = order_ptr.get(); // Get raw pointer before moving ownership

        orders_by_cid_[cid_order] = std::move(order_ptr);
        pending_orders_market_[cid_order] = raw_ptr;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Created new market order: cid=" << cid_order << ", symbol=" << symbol << ", quantity=" << quantity_order << ", side=" << (side_order == SIDE::ASK ? "sell" : "buy") << std::endl;
    }

    /**
     * @brief Move a market order from pending to acknowledged state.
     * @param cid_order The client ID of the order to acknowledge.
     * @throws std::out_of_range If the order is not found in pending market orders.
     */
    void market_order_execute_acknowledge_new(CID_TYPE cid_order) {
        auto it_pending = pending_orders_market_.find(cid_order);
        if (it_pending == pending_orders_market_.end()) {
            // std::cerr << "ERROR: Failed to acknowledge market order: cid=" << cid_order << " not found in pending orders" << std::endl;
            throw std::out_of_range("Missing pending market order for cid_order " + std::to_string(cid_order));
        }

        MarketOrder* order_ptr = it_pending->second;
        pending_orders_market_.erase(it_pending);
        acknowledged_orders_market_[cid_order] = order_ptr;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Acknowledged market order: cid=" << cid_order << std::endl;
    }

    /**
     * @brief Handle a partially executed market order, updating its remaining quantity.
     * @param cid_order The client ID of the partially filled order.
     * @param quantity_leaves The remaining quantity after the partial fill.
     * @return Reference to the updated market order.
     * @throws std::out_of_range If the order is not found in acknowledged market orders.
     */
    MarketOrder& core_market_order_execute_partial_fill(CID_TYPE cid_order, QUANTITY_TYPE quantity_leaves) {
        auto it_ack = acknowledged_orders_market_.find(cid_order);
        if (it_ack == acknowledged_orders_market_.end()) {
            //  std::cerr << "ERROR: Failed to process partial fill for market order: cid=" << cid_order << " not found in acknowledged orders" << std::endl;
            throw std::out_of_range("Missing acknowledged market order for cid_order " + std::to_string(cid_order));
        }

        MarketOrder* order = it_ack->second;
        // QUANTITY_TYPE original_quantity = order->quantity; // Keep if needed for logging
        order->quantity = quantity_leaves;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Partially filled market order: cid=" << cid_order << ", original_quantity=" << original_quantity << ", leaves_quantity=" << quantity_leaves << std::endl;

        return *order;
    }

     /**
     * @brief Handle a fully executed market order (terminal state). Removes the order.
     * @param cid_order The client ID of the fully filled market order.
     * @return A unique_ptr to the MarketOrder that was filled. Ownership is transferred to the caller.
     * @throws std::out_of_range If the order is not found in acknowledged market orders.
     * @throws std::logic_error If internal state inconsistency is detected (e.g., missing from master list).
     * @note This also cleans up any pending cancellations targeting this order.
     */
    std::unique_ptr<MarketOrder> core_market_order_execute_full_fill(CID_TYPE cid_order) {
        // 1. Check and remove from acknowledged map
        auto it_ack = acknowledged_orders_market_.find(cid_order);
        if (it_ack == acknowledged_orders_market_.end()) {
            //  std::cerr << "ERROR: Failed to process full fill for market order: cid=" << cid_order << " not found in acknowledged orders" << std::endl;
             throw std::out_of_range("Missing acknowledged market order for cid_order=" + std::to_string(cid_order));
        }
        // Don't delete pointer yet, just remove entry
        acknowledged_orders_market_.erase(it_ack);

        // 2. Clean up any pending cancellations targeting this order
        cleanup_pending_cancellations_for_target(cid_order, "Market");

        // 3. Find and remove from the master map, transferring ownership
        auto it_master = orders_by_cid_.find(cid_order);
        if (it_master == orders_by_cid_.end()) {
             // This indicates an internal inconsistency
             // std::cerr << "ERROR: Internal inconsistency. Order cid=" << cid_order << " found in acknowledged_market but not in orders_by_cid_" << std::endl;
             throw std::logic_error("Order missing from master list during full fill for cid_order=" + std::to_string(cid_order));
        }

        // Cast the base Order pointer to MarketOrder before returning.
        // Use dynamic_cast for safety, although static_cast could work if structure is guaranteed.
        std::unique_ptr<Order> base_ptr = std::move(it_master->second);
        orders_by_cid_.erase(it_master); // Remove entry from map

        MarketOrder* derived_ptr = dynamic_cast<MarketOrder*>(base_ptr.get());
        if (!derived_ptr) {
            // Should not happen if internal state is correct
            // std::cerr << "ERROR: Internal inconsistency. Failed dynamic_cast during full fill for market order cid=" << cid_order << std::endl;
             // Put the pointer back to avoid memory leak before throwing
             orders_by_cid_[cid_order] = std::move(base_ptr);
            throw std::logic_error("Type mismatch during full fill for market order cid=" + std::to_string(cid_order));
        }
        base_ptr.release(); // Release ownership from the base unique_ptr

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Fully filled market order: cid=" << cid_order << std::endl;

        return std::unique_ptr<MarketOrder>(derived_ptr); // Transfer ownership to new unique_ptr
    }

    /**
     * @brief Handle a rejected new market order (terminal state). Removes the order.
     * @param cid_order The client ID of the rejected order.
     * @throws std::out_of_range If the order is not found in pending market orders.
     * @throws std::logic_error If internal state inconsistency is detected (e.g., missing from master list).
     */
    void market_order_execute_reject_new(CID_TYPE cid_order) {
        // 1. Check and remove from pending map
        auto it_pending = pending_orders_market_.find(cid_order);
        if (it_pending == pending_orders_market_.end()) {
            //  std::cerr << "ERROR: Failed to process rejection for market order: cid=" << cid_order << " not found in pending orders" << std::endl;
             throw std::out_of_range("Missing pending market order for rejection: cid_order=" + std::to_string(cid_order));
        }
        pending_orders_market_.erase(it_pending);

        // 2. Find and remove from the master map (deletes the object)
        size_t removed_count = orders_by_cid_.erase(cid_order);
        if (removed_count == 0) {
            // This indicates an internal inconsistency
            // std::cerr << "ERROR: Internal inconsistency. Order cid=" << cid_order << " found in pending_market but not in orders_by_cid_" << std::endl;
            throw std::logic_error("Order missing from master list during rejection for cid_order=" + std::to_string(cid_order));
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Rejected new market order: cid=" << cid_order << std::endl;
    }

    /**
     * @brief Handle an expired market order (terminal state). Removes the order.
     * @param cid_order The client ID of the expired market order.
     * @throws std::out_of_range If the order is not found in acknowledged market orders.
     * @throws std::logic_error If internal state inconsistency is detected (e.g., missing from master list).
     * @note This also cleans up any pending cancellations targeting this order.
     */
    void market_order_execute_expired(CID_TYPE cid_order) {
         // 1. Check and remove from acknowledged map
        auto it_ack = acknowledged_orders_market_.find(cid_order);
        if (it_ack == acknowledged_orders_market_.end()) {
            //  std::cerr << "ERROR: Failed to process expiration for market order: cid=" << cid_order << " not found in acknowledged orders" << std::endl;
             throw std::out_of_range("Missing acknowledged market order for expiration: cid_order=" + std::to_string(cid_order));
        }
        acknowledged_orders_market_.erase(it_ack);

        // 2. Clean up any pending cancellations targeting this order
        cleanup_pending_cancellations_for_target(cid_order, "Market");

        // 3. Find and remove from the master map (deletes the object)
        size_t removed_count = orders_by_cid_.erase(cid_order);
        if (removed_count == 0) {
            // This indicates an internal inconsistency
            // std::cerr << "ERROR: Internal inconsistency. Order cid=" << cid_order << " found in acknowledged_market but not in orders_by_cid_" << std::endl;
             throw std::logic_error("Order missing from master list during expiration for cid_order=" + std::to_string(cid_order));
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Expired market order: cid=" << cid_order << std::endl;
    }


    //--------------------------------------------------------------------------
    // Partial Cancel Market Orders Logic
    //--------------------------------------------------------------------------

    /**
     * @brief Create a request to partially cancel an acknowledged market order.
     * @param cid_order The client ID for the new cancel request. Must be unique.
     * @param cid_target_order The client ID of the acknowledged market order to cancel.
     * @param quantity_cancel The quantity to cancel.
     * @throws std::invalid_argument If the new cancel CID already exists.
     * @throws std::out_of_range If the target order is not found in acknowledged market orders.
     * @throws std::logic_error If the target order is already pending a full or partial cancellation.
     */
    void market_order_partial_cancel_create(CID_TYPE cid_order, CID_TYPE cid_target_order, QUANTITY_TYPE quantity_cancel) {
         if (orders_by_cid_.count(cid_order)) {
             throw std::invalid_argument("Cancel Order with CID " + std::to_string(cid_order) + " already exists.");
         }

        const MarketOrder* target_order = get_acknowledged_market_order(cid_target_order);
        if (!target_order) {
            // std::cerr << "ERROR: Failed to create partial cancel: Target market order cid=" << cid_target_order << " not found or not acknowledged." << std::endl;
            throw std::out_of_range("Missing acknowledged market order for cid_target_order " + std::to_string(cid_target_order));
        }

        if (partial_cancel_pending_market_orders_.count(cid_target_order)) {
            //  std::cerr << "ERROR: Failed to create partial cancel: Target market order cid=" << cid_target_order << " is already partial-cancel-pending." << std::endl;
             throw std::logic_error("Target market order cid=" + std::to_string(cid_target_order) + " is already partial-cancel-pending.");
        }
        if (full_cancel_pending_market_orders_.count(cid_target_order)) {
            //  std::cerr << "ERROR: Failed to create partial cancel: Target market order cid=" << cid_target_order << " is already full-cancel-pending." << std::endl;
             throw std::logic_error("Target market order cid=" + std::to_string(cid_target_order) + " is already full-cancel-pending.");
        }


        auto cancel_ptr = std::make_unique<MarketOrderPartialCancel>(cid_order, target_order->symbol, target_order->side, cid_target_order, quantity_cancel);
        MarketOrderPartialCancel* raw_ptr = cancel_ptr.get();

        orders_by_cid_[cid_order] = std::move(cancel_ptr);
        pending_orders_market_partialcancel_[cid_order] = raw_ptr;
        partial_cancel_pending_market_orders_[cid_target_order] = raw_ptr; // Mark target as pending cancel

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Created partial cancel for market order: cancel_cid=" << cid_order << ", target_cid=" << cid_target_order << ", quantity=" << quantity_cancel << std::endl;
    }

     /**
     * @brief Acknowledge a partial cancellation of a market order. Updates the target order's quantity.
     * @param cid The client ID of the cancellation request.
     * @param quantity_leaves The remaining quantity on the target order after the partial cancellation.
     * @throws std::out_of_range If the cancellation request is not found in the pending partial cancels.
     * @note Handles race conditions where the target order might have been filled/expired/cancelled before this ack arrives.
     */
    void market_order_execute_partial_cancel_acknowledge(CID_TYPE cid, QUANTITY_TYPE quantity_leaves) {
        // 1. Find and remove the pending cancellation request
        auto it_pending_cancel = pending_orders_market_partialcancel_.find(cid);
        if (it_pending_cancel == pending_orders_market_partialcancel_.end()) {
            // std::cerr << "ERROR: Failed to acknowledge partial market cancel: Cancel order cid=" << cid << " not found in pending partial cancels." << std::endl;
            throw std::out_of_range("Missing pending partial market cancel order for cid=" + std::to_string(cid));
        }
        MarketOrderPartialCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE cid_target_order = cancel_order->cid_target_order;
        pending_orders_market_partialcancel_.erase(it_pending_cancel);

        // Remove the cancel request itself from the master list (deletes object)
        size_t removed_master = orders_by_cid_.erase(cid);
        // If removed_master is 0, it's an internal inconsistency but proceed with target handling
         if (removed_master == 0) {
            //  std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid << " was in pending_market_partialcancel_ but not in orders_by_cid_." << std::endl;
         }


        // 2. Remove the target from the "partial cancel pending" state map
        // Check if it exists before erasing to avoid trying to erase non-existent key
        if (partial_cancel_pending_market_orders_.count(cid_target_order)) {
            partial_cancel_pending_market_orders_.erase(cid_target_order);
        }


        // 3. Find the target order in the acknowledged map
        auto it_target = acknowledged_orders_market_.find(cid_target_order);
        if (it_target == acknowledged_orders_market_.end()) {
            // Race condition: Target order was likely filled, expired, or full-cancelled before this ack arrived.
            // std::cerr << "WARNING: Race condition: Cannot acknowledge partial cancel: Target market order cid=" << cid_target_order
            //           << " not found (may have been filled, expired, or cancelled)." << std::endl;
            // No further action needed on the target order, already removed from ack list and master list.
            return;
        }

        // 4. Update the target order's quantity
        MarketOrder* placed_order = it_target->second;
        // QUANTITY_TYPE original_quantity = placed_order->quantity; // Keep for logging if needed
        placed_order->quantity = quantity_leaves;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Acknowledged partial cancel for market order: cancel_cid=" << cid << ", target_cid=" << cid_target_order << ", original_quantity=" << original_quantity << ", leaves_quantity=" << quantity_leaves << std::endl;
    }

     /**
     * @brief Handle a rejected partial cancellation of a market order (terminal state for the cancel request).
     * @param cid The client ID of the rejected cancellation request.
     * @throws std::out_of_range If the cancellation request is not found in pending partial cancels.
     */
    void market_order_partial_cancel_reject(CID_TYPE cid) {
         // 1. Find and remove the pending cancellation request
        auto it_pending_cancel = pending_orders_market_partialcancel_.find(cid);
        if (it_pending_cancel == pending_orders_market_partialcancel_.end()) {
            // std::cerr << "ERROR: Failed to process rejection for partial market cancel: Cancel cid=" << cid << " not found in pending." << std::endl;
            throw std::out_of_range("Missing pending partial market cancel order for cid=" + std::to_string(cid));
        }
        MarketOrderPartialCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE target_cid = cancel_order->cid_target_order;
        pending_orders_market_partialcancel_.erase(it_pending_cancel);

        // 2. Remove the target from the "partial cancel pending" state map
        // Check if it exists before erasing
        if (partial_cancel_pending_market_orders_.count(target_cid)) {
             partial_cancel_pending_market_orders_.erase(target_cid);
        }


        // 3. Find and remove the cancel request from the master map (deletes object)
        size_t removed_master = orders_by_cid_.erase(cid);
        if (removed_master == 0) {
            // This indicates an internal inconsistency
            // std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid << " was in pending_market_partialcancel_ but not in orders_by_cid_." << std::endl;
            // Continue, as the primary goal (removing from pending state) is done.
        }


        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Rejected partial cancel for market order: cancel_cid=" << cid << ", target_cid=" << target_cid << std::endl;
    }

    //--------------------------------------------------------------------------
    // Full Cancel Market Orders Logic
    //--------------------------------------------------------------------------

    /**
     * @brief Create a request to fully cancel an acknowledged market order.
     * @param cid The client ID for the new cancel request. Must be unique.
     * @param cid_target The client ID of the acknowledged market order to cancel.
     * @throws std::invalid_argument If the new cancel CID already exists.
     * @throws std::out_of_range If the target order is not found in acknowledged market orders.
     * @throws std::logic_error If the target order is already pending a full or partial cancellation.
     */
    void market_order_full_cancel_create(CID_TYPE cid, CID_TYPE cid_target) {
        if (orders_by_cid_.count(cid)) {
            //  std::cerr << "WARNING: Attempt to create full market cancel with duplicate CID: " << cid << std::endl;
             throw std::invalid_argument("Order with CID " + std::to_string(cid) + " already exists");
        }

        const MarketOrder* target_order = get_acknowledged_market_order(cid_target);
        if (!target_order) {
            // std::cerr << "ERROR: Failed to create full market cancel: Target order cid=" << cid_target << " not found or not acknowledged." << std::endl;
            throw std::out_of_range("Missing acknowledged market order for cid_target " + std::to_string(cid_target));
        }

        if (full_cancel_pending_market_orders_.count(cid_target)) {
            //  std::cerr << "ERROR: Failed to create full market cancel: Target order cid=" << cid_target << " is already full-cancel-pending." << std::endl;
             throw std::logic_error("Target market order cid=" + std::to_string(cid_target) + " is already full-cancel-pending.");
        }
        if (partial_cancel_pending_market_orders_.count(cid_target)) {
            // std::cerr << "ERROR: Failed to create full market cancel: Target order cid=" << cid_target << " is already partial-cancel-pending." << std::endl;
            throw std::logic_error("Target market order cid=" + std::to_string(cid_target) + " is already partial-cancel-pending.");
        }


        auto cancel_ptr = std::make_unique<MarketOrderFullCancel>(cid, target_order->symbol, target_order->side, cid_target);
        MarketOrderFullCancel* raw_ptr = cancel_ptr.get();

        orders_by_cid_[cid] = std::move(cancel_ptr);
        pending_orders_market_fullcancel_[cid] = raw_ptr;
        full_cancel_pending_market_orders_[cid_target] = raw_ptr; // Mark target as pending cancel

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Created full cancel for market order: cancel_cid=" << cid << ", target_cid=" << cid_target << std::endl;
    }

     /**
     * @brief Acknowledge a full cancellation of a market order (terminal state for both cancel request and target order).
     * @param cid The client ID of the cancellation request.
     * @throws std::out_of_range If the cancellation request is not found in pending full cancels.
     * @note Handles race conditions where the target order might have been filled/expired before this ack arrives.
     */
    void market_order_execute_full_cancel_acknowledge(CID_TYPE cid) {
        // 1. Find and remove the pending full cancellation request
        auto it_pending_cancel = pending_orders_market_fullcancel_.find(cid);
        if (it_pending_cancel == pending_orders_market_fullcancel_.end()) {
            // std::cerr << "ERROR: Failed to acknowledge full market cancel: Cancel order cid=" << cid << " not found." << std::endl;
            throw std::out_of_range("Missing pending full market cancel order for cid=" + std::to_string(cid));
        }
        MarketOrderFullCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE target_cid = cancel_order->cid_target_order;
        pending_orders_market_fullcancel_.erase(it_pending_cancel);

        // Remove the cancel request itself from the master list (deletes object)
        size_t removed_master_cancel = orders_by_cid_.erase(cid);
        if (removed_master_cancel == 0) {
            //  std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid << " was in pending_market_fullcancel_ but not in orders_by_cid_." << std::endl;
        }


        // 2. Remove the target from the "full cancel pending" state map
        // Check if exists before erasing
        if (full_cancel_pending_market_orders_.count(target_cid)) {
            full_cancel_pending_market_orders_.erase(target_cid);
        }
        // Also attempt removal from partial cancel pending map in case of weird state transitions (should ideally not happen)
        if (partial_cancel_pending_market_orders_.count(target_cid)) {
             partial_cancel_pending_market_orders_.erase(target_cid);
        }


        // 3. Find and remove the target order from the acknowledged map
        auto it_target_ack = acknowledged_orders_market_.find(target_cid);
        if (it_target_ack == acknowledged_orders_market_.end()) {
            // Race condition: Target order was likely filled or expired before this ack arrived.
            //  std::cerr << "WARNING: Race condition: Cannot acknowledge full cancel: Target market order cid=" << target_cid
            //            << " not found in acknowledged orders (may have been filled or expired)." << std::endl;
             // The target is already gone from acknowledged list. It should also be gone from master list if handled correctly elsewhere.
             // We still need to ensure the cancel request object is deleted (done above).
             return;
        }
        acknowledged_orders_market_.erase(it_target_ack);


        // 4. Find and remove the target order from the master map (deletes the target object)
        size_t removed_master_target = orders_by_cid_.erase(target_cid);
        if (removed_master_target == 0) {
             // This indicates an internal inconsistency if it was found in acknowledged map but not here.
             // std::cerr << "ERROR: Internal inconsistency. Target market order cid=" << target_cid << " was in acknowledged_orders_market_ but not in orders_by_cid_ during full cancel ack." << std::endl;
             // Don't throw here, proceed as the main goal is achieved. Log is important.
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Acknowledged full cancel for market order: cancel_cid=" << cid << ", target_cid=" << target_cid << std::endl;
    }

    /**
     * @brief Handle a rejected full cancellation of a market order (terminal state for the cancel request).
     * @param cid_order The client ID of the rejected cancellation request.
     * @throws std::out_of_range If the cancellation request is not found in pending full cancels.
     */
    void market_order_full_cancel_reject(CID_TYPE cid_order) {
        // 1. Find and remove the pending cancellation request
        auto it_pending_cancel = pending_orders_market_fullcancel_.find(cid_order);
        if (it_pending_cancel == pending_orders_market_fullcancel_.end()) {
            // std::cerr << "ERROR: Failed to process rejection for full market cancel: cid=" << cid_order << " not found." << std::endl;
            throw std::out_of_range("Missing required full market cancel order: cid=" + std::to_string(cid_order));
        }
        MarketOrderFullCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE target_cid = cancel_order->cid_target_order;
        pending_orders_market_fullcancel_.erase(it_pending_cancel);

        // 2. Remove the target from the "full cancel pending" state map
        // Check if exists before erasing
        if (full_cancel_pending_market_orders_.count(target_cid)) {
            full_cancel_pending_market_orders_.erase(target_cid);
        }


        // 3. Find and remove the cancel request from the master map (deletes object)
        size_t removed_master = orders_by_cid_.erase(cid_order);
        if (removed_master == 0) {
            // std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid_order << " was in pending_market_fullcancel_ but not in orders_by_cid_." << std::endl;
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Rejected full cancel for market order: cancel_cid=" << cid_order << ", target_cid=" << target_cid << std::endl;
    }

    //--------------------------------------------------------------------------
    // Limit Orders Logic
    //--------------------------------------------------------------------------

     /**
     * @brief Create a new limit order and place it in the pending state.
     * @param side The side of the order (BID or ASK).
     * @param price The price for the order.
     * @param quantity The quantity for the order.
     * @param cid The client ID for the new order. Must be unique.
     * @param symbol The symbol for the order.
     * @throws std::invalid_argument If an order with the given CID already exists.
     */
    void limit_order_create_new(SIDE_TYPE side, PRICE_TYPE price, QUANTITY_TYPE quantity, CID_TYPE cid, const SYMBOL_TYPE& symbol) {
        if (orders_by_cid_.count(cid)) {
            // std::cerr << "WARNING: Attempt to create limit order with duplicate CID: " << cid << std::endl;
            throw std::invalid_argument("Order with CID " + std::to_string(cid) + " already exists");
        }

        auto order_ptr = std::make_unique<LimitOrder>(cid, symbol, side, price, quantity);
        LimitOrder* raw_ptr = order_ptr.get();

        orders_by_cid_[cid] = std::move(order_ptr);
        pending_orders_limit_[cid] = raw_ptr;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Created new limit order: cid=" << cid << ", symbol=" << symbol << ", price=" << price << ", quantity=" << quantity << ", side=" << (side == SIDE::ASK ? "sell" : "buy") << std::endl;
    }

     /**
     * @brief Move a limit order from pending to acknowledged state.
     * @param cid_order The client ID of the order to acknowledge.
     * @throws std::out_of_range If the order is not found in pending limit orders.
     */
    void limit_order_execute_acknowledge_new(CID_TYPE cid_order) {
        auto it_pending = pending_orders_limit_.find(cid_order);
        if (it_pending == pending_orders_limit_.end()) {
            //  std::cerr << "ERROR: Failed to acknowledge limit order: cid=" << cid_order << " not found in pending orders." << std::endl;
             throw std::out_of_range("Missing pending limit order for cid_order " + std::to_string(cid_order));
        }

        LimitOrder* order_ptr = it_pending->second;
        pending_orders_limit_.erase(it_pending);
        acknowledged_orders_limit_[cid_order] = order_ptr;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Acknowledged limit order: cid=" << cid_order << std::endl;
    }

    /**
     * @brief Handle a rejected new limit order (terminal state). Removes the order.
     * @param cid_order The client ID of the rejected order.
     * @throws std::out_of_range If the order is not found in pending limit orders.
     * @throws std::logic_error If internal state inconsistency is detected (e.g., missing from master list).
     */
    void limit_order_execute_reject_new(CID_TYPE cid_order) {
        // 1. Check and remove from pending map
        auto it_pending = pending_orders_limit_.find(cid_order);
        if (it_pending == pending_orders_limit_.end()) {
            //  std::cerr << "ERROR: Failed to process rejection for limit order: cid=" << cid_order << " not found in pending orders." << std::endl;
             throw std::out_of_range("Missing pending limit order for rejection: cid_order=" + std::to_string(cid_order));
        }
        pending_orders_limit_.erase(it_pending);

        // 2. Find and remove from the master map (deletes the object)
        size_t removed_count = orders_by_cid_.erase(cid_order);
        if (removed_count == 0) {
            // std::cerr << "ERROR: Internal inconsistency. Order cid=" << cid_order << " found in pending_limit but not in orders_by_cid_." << std::endl;
            throw std::logic_error("Order missing from master list during rejection for cid_order=" + std::to_string(cid_order));
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Rejected new limit order: cid=" << cid_order << std::endl;
    }

    /**
     * @brief Handle a partially filled limit order, updating its remaining quantity.
     * @param cid_order The client ID of the partially filled order.
     * @param quantity_leaves The remaining quantity after the partial fill.
     * @param quantity_fill The quantity that was filled in this execution.
     * @return Reference to the updated limit order.
     * @throws std::out_of_range If the order is not found in acknowledged limit orders.
     * @throws std::logic_error If quantity_leaves + quantity_fill does not equal the order's quantity before the update.
     */
    LimitOrder& core_limit_order_execute_partial_fill(CID_TYPE cid_order, QUANTITY_TYPE quantity_leaves, QUANTITY_TYPE quantity_fill) {
        auto it_ack = acknowledged_orders_limit_.find(cid_order);
        if (it_ack == acknowledged_orders_limit_.end()) {
            //  std::cerr << "ERROR: Failed to process partial fill for limit order: cid=" << cid_order << " not found in acknowledged orders." << std::endl;
             throw std::out_of_range("Acknowledged limit order not found for cid_order " + std::to_string(cid_order));
        }

        LimitOrder* placed_order = it_ack->second;
        QUANTITY_TYPE original_quantity = placed_order->quantity;

        // Basic sanity check - can be made more robust if needed
        // Allow for small potential rounding issues if QUANTITY_TYPE were float/double, but it's int64_t
        if ((quantity_leaves < 0) || (quantity_fill <= 0) || ((quantity_leaves + quantity_fill) != original_quantity)) {
            std::string error_msg = "Quantity invalid during partial fill: leaves (" + std::to_string(quantity_leaves) +
                                    ") + filled (" + std::to_string(quantity_fill) + ") inconsistent with order quantity (" +
                                    std::to_string(original_quantity) + ") for cid=" + std::to_string(cid_order);
            // std::cerr << "ERROR: " << error_msg << std::endl;
            throw std::logic_error(error_msg);
        }

        placed_order->quantity = quantity_leaves;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Partially filled limit order: cid=" << cid_order << ", fill_quantity=" << quantity_fill << ", leaves_quantity=" << quantity_leaves << std::endl;

        return *placed_order;
    }

     /**
     * @brief Handle a fully filled limit order (terminal state). Removes the order.
     * @param cid_order The client ID of the fully filled limit order.
     * @return A unique_ptr to the LimitOrder that was filled. Ownership is transferred to the caller.
     * @throws std::out_of_range If the order is not found in acknowledged limit orders.
     * @throws std::logic_error If internal state inconsistency is detected.
     * @note This also cleans up any pending cancellations targeting this order.
     */
    std::unique_ptr<LimitOrder> core_limit_order_execute_full_fill(CID_TYPE cid_order) {
         // 1. Check and remove from acknowledged map
        auto it_ack = acknowledged_orders_limit_.find(cid_order);
        if (it_ack == acknowledged_orders_limit_.end()) {
            //  std::cerr << "ERROR: Failed to process full fill for limit order: cid=" << cid_order << " not found in acknowledged orders." << snapshot() << std::endl;
             throw std::out_of_range("Missing acknowledged limit order for cid_order=" + std::to_string(cid_order));
        }
        acknowledged_orders_limit_.erase(it_ack);

        // 2. Clean up any pending cancellations targeting this order
        cleanup_pending_cancellations_for_target(cid_order, "Limit");

        // 3. Find and remove from the master map, transferring ownership
        auto it_master = orders_by_cid_.find(cid_order);
        if (it_master == orders_by_cid_.end()) {
            //  std::cerr << "ERROR: Internal inconsistency. Order cid=" << cid_order << " found in acknowledged_limit but not in orders_by_cid_. " << snapshot() << std::endl;
             throw std::logic_error("Order missing from master list during full fill for cid_order=" + std::to_string(cid_order));
        }

        std::unique_ptr<Order> base_ptr = std::move(it_master->second);
        orders_by_cid_.erase(it_master);

        LimitOrder* derived_ptr = dynamic_cast<LimitOrder*>(base_ptr.get());
        if (!derived_ptr) {
            //  std::cerr << "ERROR: Internal inconsistency. Failed dynamic_cast during full fill for limit order cid=" << cid_order << std::endl;
             orders_by_cid_[cid_order] = std::move(base_ptr); // Put back to avoid leak
             throw std::logic_error("Type mismatch during full fill for limit order cid=" + std::to_string(cid_order));
        }
        base_ptr.release();

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Fully filled limit order: cid=" << cid_order << std::endl;

        return std::unique_ptr<LimitOrder>(derived_ptr);
    }


    /**
     * @brief Handle an expired limit order (terminal state). Removes the order.
     * @param cid_order The client ID of the expired limit order.
     * @throws std::out_of_range If the order is not found in acknowledged limit orders.
     * @throws std::logic_error If internal state inconsistency is detected.
     * @note This also cleans up any pending cancellations targeting this order.
     */
    void limit_order_execute_expired(CID_TYPE cid_order) {
        // 1. Check and remove from acknowledged map
        auto it_ack = acknowledged_orders_limit_.find(cid_order);
        if (it_ack == acknowledged_orders_limit_.end()) {
            //  std::cerr << "ERROR: Failed to process expiration for limit order: cid=" << cid_order << " not found in acknowledged orders." << std::endl;
             throw std::out_of_range("Missing acknowledged limit order for expiration: cid_order=" + std::to_string(cid_order));
        }
        acknowledged_orders_limit_.erase(it_ack);

        // 2. Clean up any pending cancellations targeting this order
        cleanup_pending_cancellations_for_target(cid_order, "Limit");

        // 3. Find and remove from the master map (deletes the object)
        size_t removed_count = orders_by_cid_.erase(cid_order);
        if (removed_count == 0) {
            // std::cerr << "ERROR: Internal inconsistency. Order cid=" << cid_order << " found in acknowledged_limit but not in orders_by_cid_." << std::endl;
            throw std::logic_error("Order missing from master list during expiration for cid_order=" + std::to_string(cid_order));
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Expired limit order: cid=" << cid_order << std::endl;
    }


    //--------------------------------------------------------------------------
    // Partial Cancel Limit Orders Logic
    //--------------------------------------------------------------------------

    /**
     * @brief Create a request to partially cancel an acknowledged limit order.
     * @param cid_order The client ID for the new cancel request. Must be unique.
     * @param cid_target_order The client ID of the acknowledged limit order to cancel.
     * @param quantity_cancel The quantity to cancel.
     * @throws std::invalid_argument If the new cancel CID already exists.
     * @throws std::out_of_range If the target order is not found in acknowledged limit orders.
     * @throws std::logic_error If the target order is already pending a full or partial cancellation.
     */
    void limit_order_partial_cancel_create(CID_TYPE cid_order, CID_TYPE cid_target_order, QUANTITY_TYPE quantity_cancel) {
        if (orders_by_cid_.count(cid_order)) {
             throw std::invalid_argument("Cancel Order with CID " + std::to_string(cid_order) + " already exists.");
        }

        const LimitOrder* target_order = get_acknowledged_limit_order(cid_target_order);
        if (!target_order) {
            // std::cerr << "ERROR: Failed to create partial limit cancel: Target order cid=" << cid_target_order << " not found or not acknowledged." << std::endl;
            throw std::out_of_range("Missing acknowledged limit order for cid_target_order=" + std::to_string(cid_target_order));
        }

        if (partial_cancel_pending_limit_orders_.count(cid_target_order)) {
            // std::cerr << "ERROR: Failed to create partial limit cancel: Target order cid=" << cid_target_order << " is already partial-cancel-pending." << std::endl;
             throw std::logic_error("Target limit order cid=" + std::to_string(cid_target_order) + " is already partial-cancel-pending.");
        }
         if (full_cancel_pending_limit_orders_.count(cid_target_order)) {
            // std::cerr << "ERROR: Failed to create partial limit cancel: Target order cid=" << cid_target_order << " is already full-cancel-pending." << std::endl;
            throw std::logic_error("Target limit order cid=" + std::to_string(cid_target_order) + " is already full-cancel-pending.");
        }

        auto cancel_ptr = std::make_unique<LimitOrderPartialCancel>(cid_order, target_order->symbol, target_order->side, cid_target_order, quantity_cancel);
        LimitOrderPartialCancel* raw_ptr = cancel_ptr.get();

        orders_by_cid_[cid_order] = std::move(cancel_ptr);
        pending_orders_limit_partialcancel_[cid_order] = raw_ptr;
        partial_cancel_pending_limit_orders_[cid_target_order] = raw_ptr; // Mark target as pending cancel

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Created partial cancel for limit order: cancel_cid=" << cid_order << ", target_cid=" << cid_target_order << ", quantity=" << quantity_cancel << std::endl;
    }

    /**
     * @brief Acknowledge a partial cancellation of a limit order. Updates the target order's quantity.
     * @param cid_order The client ID of the cancellation request.
     * @param quantity_leaves The remaining quantity on the target order after the partial cancellation.
     * @throws std::out_of_range If the cancellation request is not found in the pending partial cancels.
     * @note Handles race conditions where the target order might have been filled/expired/cancelled before this ack arrives.
     */
    void limit_order_execute_partial_cancel_acknowledge(CID_TYPE cid_order, QUANTITY_TYPE quantity_leaves) {
        // 1. Find and remove the pending cancellation request
        auto it_pending_cancel = pending_orders_limit_partialcancel_.find(cid_order);
        if (it_pending_cancel == pending_orders_limit_partialcancel_.end()) {
            // std::cerr << "ERROR: Failed to acknowledge partial limit cancel: Cancel order cid=" << cid_order << " not found." << std::endl;
            throw std::out_of_range("Cancel order cid=" + std::to_string(cid_order) + " not found in pending partial limit cancellations");
        }
        LimitOrderPartialCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE target_cid = cancel_order->cid_target_order;
        pending_orders_limit_partialcancel_.erase(it_pending_cancel);

        // Remove the cancel request itself from the master list (deletes object)
        size_t removed_master = orders_by_cid_.erase(cid_order);
         if (removed_master == 0) {
            //  std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid_order << " was in pending_limit_partialcancel_ but not in orders_by_cid_." << std::endl;
         }

        // 2. Remove the target from the "partial cancel pending" state map
        // Check if exists before removing
        if(partial_cancel_pending_limit_orders_.count(target_cid)) {
            partial_cancel_pending_limit_orders_.erase(target_cid);
        }

        // 3. Find the target order in the acknowledged map
        auto it_target = acknowledged_orders_limit_.find(target_cid);
        if (it_target == acknowledged_orders_limit_.end()) {
            // Race condition: Target order was likely filled, expired, or full-cancelled before this ack arrived.
            // std::cerr << "WARNING: Race condition: Cannot acknowledge partial limit cancel: Target order cid=" << target_cid
            //           << " not found (may have been filled, expired or cancelled)." << std::endl;
            return;
        }

        // 4. Update the target order's quantity
        LimitOrder* placed_order = it_target->second;
        // QUANTITY_TYPE original_quantity = placed_order->quantity; // Keep for logging if needed
        placed_order->quantity = quantity_leaves;

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Acknowledged partial cancel for limit order: cancel_cid=" << cid_order << ", target_cid=" << target_cid << ", original_quantity=" << original_quantity << ", leaves_quantity=" << quantity_leaves << std::endl;
    }


    /**
     * @brief Handle a rejected partial cancellation of a limit order (terminal state for the cancel request).
     * @param cid_order The client ID of the rejected cancellation request.
     * @throws std::out_of_range If the cancellation request is not found in pending partial cancels.
     */
    void limit_order_partial_cancel_reject(CID_TYPE cid_order) {
        // 1. Find and remove the pending cancellation request
        auto it_pending_cancel = pending_orders_limit_partialcancel_.find(cid_order);
        if (it_pending_cancel == pending_orders_limit_partialcancel_.end()) {
            // std::cerr << "ERROR: Failed to process rejection for partial limit cancel: cid=" << cid_order << " not found." << std::endl;
            throw std::out_of_range("Cancel order cid=" + std::to_string(cid_order) + " missing from pending partial limit cancels");
        }
        LimitOrderPartialCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE target_cid = cancel_order->cid_target_order;
        pending_orders_limit_partialcancel_.erase(it_pending_cancel);

        // 2. Remove the target from the "partial cancel pending" state map
        // Check if exists before removing
        if (partial_cancel_pending_limit_orders_.count(target_cid)) {
            partial_cancel_pending_limit_orders_.erase(target_cid);
        }

        // 3. Find and remove the cancel request from the master map (deletes object)
        size_t removed_master = orders_by_cid_.erase(cid_order);
        if (removed_master == 0) {
            // std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid_order << " was in pending_limit_partialcancel_ but not in orders_by_cid_." << std::endl;
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Rejected partial cancel for limit order: cancel_cid=" << cid_order << ", target_cid=" << target_cid << std::endl;
    }

    //--------------------------------------------------------------------------
    // Full Cancel Limit Orders Logic
    //--------------------------------------------------------------------------

    /**
     * @brief Create a request to fully cancel an acknowledged limit order.
     * @param cid_order The client ID for the new cancel request. Must be unique.
     * @param cid_target_order The client ID of the acknowledged limit order to cancel.
     * @throws std::invalid_argument If the new cancel CID already exists.
     * @throws std::out_of_range If the target order is not found in acknowledged limit orders.
     * @throws std::logic_error If the target order is already pending a full or partial cancellation.
     */
    void limit_order_full_cancel_create(CID_TYPE cid_order, CID_TYPE cid_target_order) {
        if (orders_by_cid_.count(cid_order)) {
             throw std::invalid_argument("Cancel Order with CID " + std::to_string(cid_order) + " already exists.");
        }

        const LimitOrder* target_order = get_acknowledged_limit_order(cid_target_order);
        if (!target_order) {
            // std::cerr << "ERROR: Failed to create full limit cancel: Target order cid=" << cid_target_order << " not found or not acknowledged." << std::endl;
            throw std::out_of_range("Missing acknowledged limit order for cid_target_order=" + std::to_string(cid_target_order));
        }

        if (full_cancel_pending_limit_orders_.count(cid_target_order)) {
            //  std::cerr << "ERROR: Failed to create full limit cancel: Target order cid=" << cid_target_order << " is already full-cancel-pending." << std::endl;
             throw std::logic_error("Target limit order cid=" + std::to_string(cid_target_order) + " is already full-cancel-pending.");
        }
        if (partial_cancel_pending_limit_orders_.count(cid_target_order)) {
            //  std::cerr << "ERROR: Failed to create full limit cancel: Target order cid=" << cid_target_order << " is already partial-cancel-pending." << std::endl;
             throw std::logic_error("Target limit order cid=" + std::to_string(cid_target_order) + " is already partial-cancel-pending.");
        }


        auto cancel_ptr = std::make_unique<LimitOrderFullCancel>(cid_order, target_order->symbol, target_order->side, cid_target_order);
        LimitOrderFullCancel* raw_ptr = cancel_ptr.get();

        orders_by_cid_[cid_order] = std::move(cancel_ptr);
        pending_orders_limit_fullcancel_[cid_order] = raw_ptr;
        full_cancel_pending_limit_orders_[cid_target_order] = raw_ptr; // Mark target as pending cancel

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Created full cancel for limit order: cancel_cid=" << cid_order << ", target_cid=" << cid_target_order << std::endl;
    }

    /**
     * @brief Acknowledge a full cancellation of a limit order (terminal state for both cancel request and target order).
     * @param cid_order The client ID of the cancellation request.
     * @throws std::out_of_range If the cancellation request is not found in pending full cancels.
     * @note Handles race conditions where the target order might have been filled/expired before this ack arrives.
     */
    void limit_order_execute_full_cancel_acknowledge(CID_TYPE cid_order) {
        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Processing full limit cancel acknowledgment: cancel_cid=" << cid_order << std::endl;

        // 1. Find and remove the pending full cancellation request
        auto it_pending_cancel = pending_orders_limit_fullcancel_.find(cid_order);
        if (it_pending_cancel == pending_orders_limit_fullcancel_.end()) {
            // std::cerr << "ERROR: Failed to acknowledge full limit cancel: Cancel order cid=" << cid_order << " not found." << std::endl;
            throw std::out_of_range("Cancellation order " + std::to_string(cid_order) + " not found in pending full limit cancellations");
        }
        LimitOrderFullCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE target_cid = cancel_order->cid_target_order;
        pending_orders_limit_fullcancel_.erase(it_pending_cancel);

        // Remove the cancel request itself from the master list (deletes object)
        size_t removed_master_cancel = orders_by_cid_.erase(cid_order);
        if (removed_master_cancel == 0) {
            //  std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid_order << " was in pending_limit_fullcancel_ but not in orders_by_cid_." << std::endl;
        }

        // 2. Remove the target from the "full cancel pending" state map
        // Check if exists before removing
        if(full_cancel_pending_limit_orders_.count(target_cid)) {
             full_cancel_pending_limit_orders_.erase(target_cid);
        }
        // Also attempt removal from partial cancel pending map
        if(partial_cancel_pending_limit_orders_.count(target_cid)) {
            partial_cancel_pending_limit_orders_.erase(target_cid);
        }


        // 3. Find and remove the target order from the acknowledged map
        auto it_target_ack = acknowledged_orders_limit_.find(target_cid);
        if (it_target_ack == acknowledged_orders_limit_.end()) {
            // Race condition: Target order was likely filled or expired before this ack arrived.
            //  std::cerr << "WARNING: Race condition: Cannot acknowledge full limit cancel: Target order cid=" << target_cid
            //            << " not found in acknowledged orders (may have been filled or expired)." << std::endl;
             return;
        }
        acknowledged_orders_limit_.erase(it_target_ack);


        // 4. Find and remove the target order from the master map (deletes the target object)
        size_t removed_master_target = orders_by_cid_.erase(target_cid);
        if (removed_master_target == 0) {
            //  std::cerr << "ERROR: Internal inconsistency. Target limit order cid=" << target_cid << " was in acknowledged_orders_limit_ but not in orders_by_cid_ during full cancel ack." << std::endl;
            // Logged, proceed.
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Acknowledged full cancel for limit order: cancel_cid=" << cid_order << ", target_cid=" << target_cid << std::endl;
    }

    /**
     * @brief Handle a rejected full cancellation of a limit order (terminal state for the cancel request).
     * @param cid_order The client ID of the rejected cancellation request.
     * @throws std::out_of_range If the cancellation request is not found in pending full cancels.
     */
    void limit_order_full_cancel_reject(CID_TYPE cid_order) {
        // 1. Find and remove the pending cancellation request
        auto it_pending_cancel = pending_orders_limit_fullcancel_.find(cid_order);
        if (it_pending_cancel == pending_orders_limit_fullcancel_.end()) {
            // std::cerr << "ERROR: Failed to process rejection for full limit cancel: cid=" << cid_order << " not found." << std::endl;
            throw std::out_of_range("Cancel order cid=" + std::to_string(cid_order) + " not found in pending full cancels");
        }
        LimitOrderFullCancel* cancel_order = it_pending_cancel->second;
        CID_TYPE target_cid = cancel_order->cid_target_order;
        pending_orders_limit_fullcancel_.erase(it_pending_cancel);

        // 2. Remove the target from the "full cancel pending" state map
        // Check if exists before removing
        if(full_cancel_pending_limit_orders_.count(target_cid)) {
            full_cancel_pending_limit_orders_.erase(target_cid);
        }

        // 3. Find and remove the cancel request from the master map (deletes object)
        size_t removed_master = orders_by_cid_.erase(cid_order);
        if (removed_master == 0) {
            // std::cerr << "WARNING: Internal inconsistency. Cancel order cid=" << cid_order << " was in pending_limit_fullcancel_ but not in orders_by_cid_." << std::endl;
        }

        // TODO: Replace with proper logging
        // std::cerr << "DEBUG: Rejected full cancel for limit order: cancel_cid=" << cid_order << ", target_cid=" << target_cid << std::endl;
    }

    //--------------------------------------------------------------------------
    // Snapshot / Debugging
    //--------------------------------------------------------------------------

     /**
     * @brief Return a detailed snapshot of the current inventory state.
     * @return A formatted string containing the full state of the inventory.
     */
    std::string snapshot() const {
        std::ostringstream oss;
        oss << "=== INVENTORY SNAPSHOT ===\n";

        // Lambda helper to print order details
        auto print_order_info = [&](const Order* base_order) -> std::string {
             if (!base_order) return "nullptr";
             return base_order->get_type_name() + base_order->get_details();
        };
        // Lambda helper for cancel pending target entries
        auto print_cancel_target_info = [&](CID_TYPE target_cid, const CancelOrderRequest* cancel_req) -> std::string {
             if (!cancel_req) return "Target CID " + std::to_string(target_cid) + ": Pending Cancel=nullptr";
             std::string details = "Target CID " + std::to_string(target_cid) + ": Pending Cancel CID=" + std::to_string(cancel_req->cid) + " (" + cancel_req->symbol;
             // Add quantity for partial cancels
             if (const auto* pc = dynamic_cast<const LimitOrderPartialCancel*>(cancel_req)) {
                 details += ", Q:" + std::to_string(pc->quantity);
             } else if (const auto* pc = dynamic_cast<const MarketOrderPartialCancel*>(cancel_req)) {
                 details += ", Q:" + std::to_string(pc->quantity);
             }
             details += ")";
             return details;
        };

        // --- Use std::map for sorted output in snapshot ---
        // Note: This adds overhead to the snapshot function itself, but not to core operations.
        std::map<CID_TYPE, const Order*> sorted_orders_by_cid;
        for(const auto& pair : orders_by_cid_) sorted_orders_by_cid[pair.first] = pair.second.get();

        std::map<CID_TYPE, const LimitOrder*> sorted_pending_limit;
        for(const auto& pair : pending_orders_limit_) sorted_pending_limit[pair.first] = pair.second;

        std::map<CID_TYPE, const MarketOrder*> sorted_pending_market;
        for(const auto& pair : pending_orders_market_) sorted_pending_market[pair.first] = pair.second;

        std::map<CID_TYPE, const LimitOrderFullCancel*> sorted_pending_limit_fc;
        for(const auto& pair : pending_orders_limit_fullcancel_) sorted_pending_limit_fc[pair.first] = pair.second;

        std::map<CID_TYPE, const LimitOrderPartialCancel*> sorted_pending_limit_pc;
        for(const auto& pair : pending_orders_limit_partialcancel_) sorted_pending_limit_pc[pair.first] = pair.second;

        std::map<CID_TYPE, const MarketOrderFullCancel*> sorted_pending_market_fc;
        for(const auto& pair : pending_orders_market_fullcancel_) sorted_pending_market_fc[pair.first] = pair.second;

        std::map<CID_TYPE, const MarketOrderPartialCancel*> sorted_pending_market_pc;
        for(const auto& pair : pending_orders_market_partialcancel_) sorted_pending_market_pc[pair.first] = pair.second;

        std::map<CID_TYPE, const LimitOrder*> sorted_ack_limit;
        for(const auto& pair : acknowledged_orders_limit_) sorted_ack_limit[pair.first] = pair.second;

        std::map<CID_TYPE, const MarketOrder*> sorted_ack_market;
        for(const auto& pair : acknowledged_orders_market_) sorted_ack_market[pair.first] = pair.second;

        std::map<CID_TYPE, const LimitOrderFullCancel*> sorted_target_pending_limit_fc;
        for(const auto& pair : full_cancel_pending_limit_orders_) sorted_target_pending_limit_fc[pair.first] = pair.second;

        std::map<CID_TYPE, const LimitOrderPartialCancel*> sorted_target_pending_limit_pc;
        for(const auto& pair : partial_cancel_pending_limit_orders_) sorted_target_pending_limit_pc[pair.first] = pair.second;

        std::map<CID_TYPE, const MarketOrderFullCancel*> sorted_target_pending_market_fc;
        for(const auto& pair : full_cancel_pending_market_orders_) sorted_target_pending_market_fc[pair.first] = pair.second;

        std::map<CID_TYPE, const MarketOrderPartialCancel*> sorted_target_pending_market_pc;
        for(const auto& pair : partial_cancel_pending_market_orders_) sorted_target_pending_market_pc[pair.first] = pair.second;
        // --- End sorted maps ---


        // Print all orders by CID
        oss << "\n-- ALL ORDERS BY CID: " << sorted_orders_by_cid.size() << " --\n";
        for (const auto& pair : sorted_orders_by_cid) {
            oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }

        // Pending Orders
        oss << "\n-- PENDING LIMIT ORDERS: " << sorted_pending_limit.size() << " --\n";
        for (const auto& pair : sorted_pending_limit) {
            oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }
        oss << "\n-- PENDING MARKET ORDERS: " << sorted_pending_market.size() << " --\n";
        for (const auto& pair : sorted_pending_market) {
             oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }

        // Pending Cancellations
         oss << "\n-- PENDING LIMIT ORDER FULL CANCELS: " << sorted_pending_limit_fc.size() << " --\n";
        for (const auto& pair : sorted_pending_limit_fc) {
             oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }
        oss << "\n-- PENDING LIMIT ORDER PARTIAL CANCELS: " << sorted_pending_limit_pc.size() << " --\n";
        for (const auto& pair : sorted_pending_limit_pc) {
             oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }
        oss << "\n-- PENDING MARKET ORDER FULL CANCELS: " << sorted_pending_market_fc.size() << " --\n";
        for (const auto& pair : sorted_pending_market_fc) {
             oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }
        oss << "\n-- PENDING MARKET ORDER PARTIAL CANCELS: " << sorted_pending_market_pc.size() << " --\n";
        for (const auto& pair : sorted_pending_market_pc) {
             oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }

        // Acknowledged Orders
         oss << "\n-- ACKNOWLEDGED LIMIT ORDERS: " << sorted_ack_limit.size() << " --\n";
        for (const auto& pair : sorted_ack_limit) {
             oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }
        oss << "\n-- ACKNOWLEDGED MARKET ORDERS: " << sorted_ack_market.size() << " --\n";
        for (const auto& pair : sorted_ack_market) {
             oss << "  " << pair.first << ": " << print_order_info(pair.second) << "\n";
        }

        // Cancel Pending Target Orders
         oss << "\n-- FULL CANCEL PENDING TARGET LIMIT ORDERS: " << sorted_target_pending_limit_fc.size() << " --\n";
        for (const auto& pair : sorted_target_pending_limit_fc) {
             oss << "  " << print_cancel_target_info(pair.first, pair.second) << "\n";
        }
        oss << "\n-- PARTIAL CANCEL PENDING TARGET LIMIT ORDERS: " << sorted_target_pending_limit_pc.size() << " --\n";
        for (const auto& pair : sorted_target_pending_limit_pc) {
            oss << "  " << print_cancel_target_info(pair.first, pair.second) << "\n";
        }
        oss << "\n-- FULL CANCEL PENDING TARGET MARKET ORDERS: " << sorted_target_pending_market_fc.size() << " --\n";
        for (const auto& pair : sorted_target_pending_market_fc) {
             oss << "  " << print_cancel_target_info(pair.first, pair.second) << "\n";
        }
        oss << "\n-- PARTIAL CANCEL PENDING TARGET MARKET ORDERS: " << sorted_target_pending_market_pc.size() << " --\n";
        for (const auto& pair : sorted_target_pending_market_pc) {
             oss << "  " << print_cancel_target_info(pair.first, pair.second) << "\n";
        }

        oss << "\n=== END SNAPSHOT ===\n";
        return oss.str();
    }


private:
    // --- Core Data Structures ---

    // Master map holding ownership of all order objects (placements and cancels).
    // Key: CID of the order/cancel request. Value: Unique pointer to the Order object.
    // Using unordered_map for O(1) average-case lookup/insertion/deletion.
    std::unordered_map<CID_TYPE, std::unique_ptr<Order>> orders_by_cid_;

    // --- Pending Orders/Requests (Waiting for Acknowledgement/Rejection) ---
    // These maps store non-owning raw pointers to objects owned by orders_by_cid_.
    // Performance Note: Using specific types avoids dynamic_cast during state transitions.

    std::unordered_map<CID_TYPE, LimitOrder*> pending_orders_limit_;
    std::unordered_map<CID_TYPE, MarketOrder*> pending_orders_market_;
    std::unordered_map<CID_TYPE, LimitOrderFullCancel*> pending_orders_limit_fullcancel_;
    std::unordered_map<CID_TYPE, LimitOrderPartialCancel*> pending_orders_limit_partialcancel_;
    std::unordered_map<CID_TYPE, MarketOrderFullCancel*> pending_orders_market_fullcancel_;
    std::unordered_map<CID_TYPE, MarketOrderPartialCancel*> pending_orders_market_partialcancel_;

    // --- Acknowledged Orders (Live/Working Orders) ---
    // These maps store non-owning raw pointers to objects owned by orders_by_cid_.
    std::unordered_map<CID_TYPE, LimitOrder*> acknowledged_orders_limit_;
    std::unordered_map<CID_TYPE, MarketOrder*> acknowledged_orders_market_;

    // --- Tracking Orders That Have a Cancellation Pending Against Them ---
    // Key: CID of the TARGET order. Value: Raw pointer to the PENDING CANCELLATION request (owned by orders_by_cid_).
    // This allows quick checks before creating new cancels and cleanup during fills/expiries.
    std::unordered_map<CID_TYPE, LimitOrderPartialCancel*> partial_cancel_pending_limit_orders_;
    std::unordered_map<CID_TYPE, LimitOrderFullCancel*> full_cancel_pending_limit_orders_;
    std::unordered_map<CID_TYPE, MarketOrderPartialCancel*> partial_cancel_pending_market_orders_;
    std::unordered_map<CID_TYPE, MarketOrderFullCancel*> full_cancel_pending_market_orders_;


    // --- Private Helper Getters (const correct) ---

    /** @brief Get raw pointer to a pending market order by CID. */
    MarketOrder* get_pending_market_order(CID_TYPE cid_order) const {
        auto it = pending_orders_market_.find(cid_order);
        return (it != pending_orders_market_.end()) ? it->second : nullptr;
    }

    /** @brief Get raw pointer to a pending limit order by CID. */
    LimitOrder* get_pending_limit_order(CID_TYPE cid_order) const {
        auto it = pending_orders_limit_.find(cid_order);
        return (it != pending_orders_limit_.end()) ? it->second : nullptr;
    }


    /** @brief Get raw pointer to an acknowledged market order by CID. */
    MarketOrder* get_acknowledged_market_order(CID_TYPE cid_order) const {
        auto it = acknowledged_orders_market_.find(cid_order);
        return (it != acknowledged_orders_market_.end()) ? it->second : nullptr;
    }

     /** @brief Get raw pointer to an acknowledged limit order by CID. */
    LimitOrder* get_acknowledged_limit_order(CID_TYPE cid_order) const {
         auto it = acknowledged_orders_limit_.find(cid_order);
         return (it != acknowledged_orders_limit_.end()) ? it->second : nullptr;
     }


    // --- Private Helper for Cleanup ---

    /**
     * @brief Removes any pending cancellation requests targeting the given order CID.
     * This is called when the target order reaches a terminal state (fill, expiry, cancel ack).
     * It removes the cancellation request from its pending state map, its target-pending map,
     * and crucially, from the master orders_by_cid_ map (deleting the cancellation object).
     * @param target_cid The CID of the order being finalized.
     * @param order_type_str String "Limit" or "Market" for selecting the correct maps.
     */
    void cleanup_pending_cancellations_for_target(CID_TYPE target_cid, const std::string& order_type_str) {
        // --- Clean up FULL cancel pending ---
        if (order_type_str == "Limit") {
            auto it_full_pending_target = full_cancel_pending_limit_orders_.find(target_cid);
            if (it_full_pending_target != full_cancel_pending_limit_orders_.end()) {
                LimitOrderFullCancel* cancel_req = it_full_pending_target->second; // Use specific type
                CID_TYPE cancel_cid = cancel_req->cid;

                // std::cerr << "WARNING: Race condition: Limit order cid=" << target_cid
                //           << " was finalized while a full cancellation (cid=" << cancel_cid << ") was pending. Cleaning up cancel request." << std::endl;

                // Remove from target pending map BEFORE erasing from master, to avoid using dangling pointer
                full_cancel_pending_limit_orders_.erase(it_full_pending_target); // Erase using iterator is efficient

                // Remove from specific pending cancel map
                pending_orders_limit_fullcancel_.erase(cancel_cid);

                // Remove from master map (deletes object)
                orders_by_cid_.erase(cancel_cid);
            }
        } else { // Assuming "Market" if not "Limit"
            auto it_full_pending_target = full_cancel_pending_market_orders_.find(target_cid);
             if (it_full_pending_target != full_cancel_pending_market_orders_.end()) {
                MarketOrderFullCancel* cancel_req = it_full_pending_target->second; // Use specific type
                CID_TYPE cancel_cid = cancel_req->cid;

                // std::cerr << "WARNING: Race condition: Market order cid=" << target_cid
                //           << " was finalized while a full cancellation (cid=" << cancel_cid << ") was pending. Cleaning up cancel request." << std::endl;

                // Remove from target pending map
                full_cancel_pending_market_orders_.erase(it_full_pending_target);

                // Remove from specific pending cancel map
                pending_orders_market_fullcancel_.erase(cancel_cid);

                // Remove from master map (deletes object)
                orders_by_cid_.erase(cancel_cid);
             }
        }

        // --- Clean up PARTIAL cancel pending ---
        if (order_type_str == "Limit") {
            auto it_partial_pending_target = partial_cancel_pending_limit_orders_.find(target_cid);
            if (it_partial_pending_target != partial_cancel_pending_limit_orders_.end()) {
                LimitOrderPartialCancel* cancel_req = it_partial_pending_target->second; // Use specific type
                CID_TYPE cancel_cid = cancel_req->cid;

                // std::cerr << "WARNING: Race condition: Limit order cid=" << target_cid
                //           << " was finalized while a partial cancellation (cid=" << cancel_cid << ") was pending. Cleaning up cancel request." << std::endl;

                // Remove from target pending map
                partial_cancel_pending_limit_orders_.erase(it_partial_pending_target);

                // Remove from specific pending cancel map
                pending_orders_limit_partialcancel_.erase(cancel_cid);

                // Remove from master map (deletes object)
                orders_by_cid_.erase(cancel_cid);
            }
        } else { // Assuming "Market"
            auto it_partial_pending_target = partial_cancel_pending_market_orders_.find(target_cid);
             if (it_partial_pending_target != partial_cancel_pending_market_orders_.end()) {
                 MarketOrderPartialCancel* cancel_req = it_partial_pending_target->second; // Use specific type
                 CID_TYPE cancel_cid = cancel_req->cid;

                //  std::cerr << "WARNING: Race condition: Market order cid=" << target_cid
                //            << " was finalized while a partial cancellation (cid=" << cancel_cid << ") was pending. Cleaning up cancel request." << std::endl;

                 // Remove from target pending map
                 partial_cancel_pending_market_orders_.erase(it_partial_pending_target);

                 // Remove from specific pending cancel map
                 pending_orders_market_partialcancel_.erase(cancel_cid);

                 // Remove from master map (deletes object)
                 orders_by_cid_.erase(cancel_cid);
            }
        }
    }


}; // class InventoryCore

} // namespace inventory
} // namespace trading

#endif // TRADING_INVENTORY_H