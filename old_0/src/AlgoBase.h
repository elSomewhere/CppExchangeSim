// file: src/AlgoBase.h
//================
// AlgoBase.h
//================

#pragma once // Use pragma once for header guard

#include "EventBus.h"       // Core event bus system
#include "Inventory.h"      // Order inventory management
#include "Model.h"          // Event definitions and aliases
#include "Globals.h"        // Global type definitions (SIDE_TYPE etc.)

#include <string>
#include <vector>
#include <optional>
#include <memory>           // For std::shared_ptr
#include <stdexcept>        // For exception handling
#include <sstream>          // For string formatting (topics, stream IDs)
#include <cmath>            // For isnan (if needed)
#include <type_traits>      // For static_assert or type checks
#include <limits>           // For numeric limits (if needed)
#include <map>              // For potential state management in derived classes

// Use the logging macros defined in EventBus.h (or replace with your own)
#define LOG_DEBUG(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::DEBUG, (logger_source), (message))
#define LOG_INFO(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::INFO, (logger_source), (message))
#define LOG_WARNING(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::WARNING, (logger_source), (message))
#define LOG_ERROR(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::ERROR, (logger_source), (message))


namespace trading {
    namespace algo {

// Use aliases for brevity and consistency
        using EventBusSystem::AgentId;
        using EventBusSystem::Timestamp;
        using EventBusSystem::Duration;
        using EventBusSystem::TopicId;
        using EventBusSystem::StreamId;
        using EventBusSystem::SequenceNumber;
        using ModelEvents::SymbolType;
        using ModelEvents::Side; // Use ModelEvents::Side internally
        using ModelEvents::PriceType;
        using ModelEvents::QuantityType;
        using ModelEvents::ClientOrderIdType;
        using ModelEvents::ExchangeOrderIdType;
        using trading::inventory::InventoryCore; // Alias for InventoryCore

// Use the specific Event Bus type alias from Model.h
        using AlgoEventBus = ModelEventBus<>;
// Use the BASE Event Processor alias from Model.h
        template<typename Derived>
        using BaseAlgoEventProcessor = ModelEventProcessor<Derived>;


// --- Helper Function for Side Conversion --- (Keep as before)
        inline trading::inventory::SIDE_TYPE ModelSideToInventorySide(ModelEvents::Side model_side) {
            switch (model_side) {
                case ModelEvents::Side::BUY:  return trading::inventory::SIDE_TYPE::BID;
                case ModelEvents::Side::SELL: return trading::inventory::SIDE_TYPE::ASK;
                default:
                    throw std::logic_error("Unknown ModelEvents::Side value encountered during conversion.");
            }
        }

/**
 * @brief Abstract CRTP base class for trading algorithms.
 *
 * Provides core functionality for order management, inventory tracking,
 * and event handling via the EventBus using the EventProcessor CRTP base.
 * Derived classes must inherit using CRTP (e.g., class MyAlgo : public AlgoBase<MyAlgo>)
 * and implement the abstract `on_...` event handler methods.
 *
 * @tparam DerivedAlgo The most derived algorithm class type (CRTP).
 */
        template<typename DerivedAlgo> // CRTP Parameter
        class AlgoBase : public BaseAlgoEventProcessor<DerivedAlgo> {
        public:
            // Make types from the actual base accessible
            using EventVariant = typename BaseAlgoEventProcessor<DerivedAlgo>::EventVariant;
            using ScheduledEvent = typename BaseAlgoEventProcessor<DerivedAlgo>::ScheduledEvent;


            /**
             * @brief Constructor for AlgoBase.
             * @param agent_id Unique identifier for this algorithm instance.
             * @param exchange_name The name (symbol) of the exchange this algo trades on.
             */
            explicit AlgoBase(AgentId agent_id, SymbolType exchange_name)
                    : BaseAlgoEventProcessor<DerivedAlgo>(agent_id),
                      exchange_name_(std::move(exchange_name)),
                      next_client_order_id_(1),
                      inventory_() {
                // LOG_INFO is fine here, doesn't depend on bus subscriptions
                LOG_INFO(this->get_logger_source(), "AlgoBase constructed for agent " + std::to_string(this->get_id()) + " on exchange: " + exchange_name_);
                // Subscriptions MOVED to setup_subscriptions()
            }

            virtual ~AlgoBase() override = default;

            // Add this new method
            void setup_subscriptions() {
                if (!this->bus_) {
                    LOG_ERROR(this->get_logger_source(), "AlgoBase cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
                    return;
                }
                LOG_INFO(this->get_logger_source(), "AlgoBase agent " + std::to_string(this->get_id()) + " setting up subscriptions for exchange: " + exchange_name_);
                this->subscribe(this->format_topic("LTwoOrderBookEvent", exchange_name_));
                this->subscribe(this->format_topic("LimitOrderAckEvent", this->get_id()));
                this->subscribe(this->format_topic("FullFillLimitOrderEvent", this->get_id()));
                this->subscribe(this->format_topic("PartialFillLimitOrderEvent", this->get_id()));
                this->subscribe(this->format_topic("FullCancelLimitOrderAckEvent", this->get_id()));
                this->subscribe(this->format_topic("MarketOrderAckEvent", this->get_id()));
                this->subscribe(this->format_topic("FullFillMarketOrderEvent", this->get_id()));
                this->subscribe(this->format_topic("PartialFillMarketOrderEvent", this->get_id()));
                this->subscribe(this->format_topic("MarketOrderExpiredEvent", this->get_id()));
                this->subscribe(this->format_topic("LimitOrderExpiredEvent", this->get_id()));
                this->subscribe(this->format_topic("FullCancelLimitOrderRejectEvent", this->get_id()));
                this->subscribe(this->format_topic("PartialCancelLimitOrderRejectEvent", this->get_id()));
                this->subscribe(this->format_topic("FullCancelMarketOrderRejectEvent", this->get_id()));
                this->subscribe(this->format_topic("PartialCancelMarketOrderRejectEvent", this->get_id()));
                this->subscribe("Bang");
                this->subscribe(this->format_topic("TradeEvent", exchange_name_));
                this->subscribe(this->format_topic("AckTriggerExpiredLimitOrderEvent", this->get_id()));
                this->subscribe(this->format_topic("LimitOrderRejectEvent", this->get_id()));
                this->subscribe(this->format_topic("MarketOrderRejectEvent", this->get_id()));
                this->subscribe(this->format_topic("PartialCancelLimitAckEvent", this->get_id()));
                this->subscribe(this->format_topic("PartialCancelMarketAckEvent", this->get_id()));
                this->subscribe(this->format_topic("FullCancelMarketOrderAckEvent", this->get_id()));
            }

            // Prevent copying and assignment
            AlgoBase(const AlgoBase&) = delete;
            AlgoBase& operator=(const AlgoBase&) = delete;
            // Allow moving (though typically not moved after registration)
            AlgoBase(AlgoBase&&) = default;
            AlgoBase& operator=(AlgoBase&&) = default;

            //--------------------------------------------------------------------------
            // Public Accessors (for derived classes)
            //--------------------------------------------------------------------------

            /** @brief Get a modifiable reference to the inventory core. */
            InventoryCore& get_inventory() { return inventory_; }

            /** @brief Get a constant reference to the inventory core. */
            const InventoryCore& get_inventory() const { return inventory_; }

            /** @brief Get the name of the exchange this algo trades on. */
            const SymbolType& get_exchange_name() const { return exchange_name_; }

            //--------------------------------------------------------------------------
            // Order Management API (Public methods for derived classes)
            //--------------------------------------------------------------------------

            /**
             * @brief Cancel all currently acknowledged limit orders managed by this algo's inventory.
             * Skips orders that are still pending acknowledgment.
             */
            void create_full_cancel_all_limit_orders() {
                std::vector<ClientOrderIdType> ack_cids = inventory_.get_all_acknowledged_limit_orders_cid();
                int cancel_attempts = 0;
                for (ClientOrderIdType cid : ack_cids) {
                    // Check if the order still exists in inventory's acknowledged state before cancelling
                    // (handles potential race conditions if order was filled/cancelled between get_all and now)
                    if (inventory_.is_limit_order_acknowledged(cid)) {
                        auto details_opt = inventory_.get_acknowledged_limit_order_details(cid);
                        if (details_opt) {
                            // The inventory already tracks orders by symbol, but double-check for safety?
                            // Not strictly necessary if inventory is correctly managed per-algo instance.
                            // Let's assume inventory only holds orders for this->exchange_name_
                            // auto [t_cid, t_symbol, t_side, t_price, t_qty] = *details_opt;
                            // if (t_symbol == this->exchange_name_) { ... }

                            LOG_DEBUG(this->get_logger_source(), "Attempting full cancel for acknowledged limit order CID: " + std::to_string(cid));
                            if (create_full_cancel_limit_order(cid)) {
                                cancel_attempts++;
                            }
                        } else {
                            // This suggests an internal inconsistency (acknowledged but no details)
                            LOG_WARNING(this->get_logger_source(), "Internal inconsistency: Could not retrieve details for acknowledged limit order CID: " + std::to_string(cid) + " during cancel-all.");
                        }
                    } else {
                        LOG_DEBUG(this->get_logger_source(), "Skipping cancel for CID: " + std::to_string(cid) + " - no longer acknowledged.");
                    }
                }
                if (cancel_attempts > 0) {
                    LOG_INFO(this->get_logger_source(), "Sent full cancel requests for " + std::to_string(cancel_attempts) + " acknowledged limit orders on exchange " + exchange_name_);
                } else {
                    LOG_INFO(this->get_logger_source(), "No acknowledged limit orders found to cancel on exchange " + exchange_name_);
                }
            }


            /**
             * @brief Create and publish a Market Order request.
             * @param symbol The instrument symbol (should match algo's exchange_name_).
             * @param side BUY or SELL (ModelEvents::Side).
             * @param quantity Order quantity (must be > 0).
             * @param timeout Duration for the order's validity.
             * @return The Client Order ID (CID) if the order was successfully created locally, otherwise std::nullopt.
             */
            std::optional<ClientOrderIdType> create_market_order(
                    const SymbolType& symbol,
                    ModelEvents::Side side,
                    QuantityType quantity,
                    Duration timeout
            ) {
                if (symbol != this->exchange_name_) {
                    LOG_ERROR(this->get_logger_source(), "Market order symbol '" + symbol + "' does not match algo exchange '" + this->exchange_name_ + "'.");
                    return std::nullopt;
                }
                if (quantity <= 0) {
                    LOG_ERROR(this->get_logger_source(), "Invalid market order quantity: " + std::to_string(quantity));
                    return std::nullopt;
                }
                if (!this->bus_) { // Access bus_ pointer from EventProcessor base
                    LOG_ERROR(this->get_logger_source(), "Cannot create market order: EventBus not set.");
                    return std::nullopt;
                }

                ClientOrderIdType cid = next_client_order_id_++;

                try {
                    inventory_.market_order_create_new(cid, symbol, quantity, ModelSideToInventorySide(side));

                    Timestamp current_time = this->bus_->get_current_time();
                    auto order_evt_ptr = std::make_shared<const ModelEvents::MarketOrderEvent>(
                            current_time, symbol, side, quantity, timeout, cid
                    );

                    std::string stream_id = format_stream_id("market_order", this->get_id(), cid);
                    std::string topic = format_topic("MarketOrderEvent", symbol);
                    // Use the simplified publish_wrapper
                    publish_wrapper(topic, stream_id, order_evt_ptr);

                    LOG_DEBUG(this->get_logger_source(), "Created market order: CID=" + std::to_string(cid) + ", Qty=" + std::to_string(quantity) + ", Side=" + ModelEvents::side_to_string(side) + ", Symbol=" + symbol);
                    return cid;

                } catch (const std::invalid_argument& e) {
                    LOG_ERROR(this->get_logger_source(), "Failed to create market order in inventory (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--; // Roll back CID on failure
                    return std::nullopt;
                } catch (const std::exception& e) {
                    LOG_ERROR(this->get_logger_source(), "Unexpected error creating market order (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--; // Roll back CID on failure
                    return std::nullopt;
                }
            }

            /**
            * @brief Create and publish a Limit Order request.
            * @param symbol The instrument symbol (should match algo's exchange_name_).
            * @param side BUY or SELL (ModelEvents::Side).
            * @param price Limit price (must be > 0).
            * @param quantity Order quantity (must be > 0).
            * @param timeout Duration for the order's validity.
            * @return The Client Order ID (CID) if the order was successfully created locally, otherwise std::nullopt.
            */
            std::optional<ClientOrderIdType> create_limit_order(
                    const SymbolType& symbol,
                    ModelEvents::Side side,
                    PriceType price,
                    QuantityType quantity,
                    Duration timeout
            ) {
                if (symbol != this->exchange_name_) {
                    LOG_ERROR(this->get_logger_source(), "Limit order symbol '" + symbol + "' does not match algo exchange '" + this->exchange_name_ + "'.");
                    return std::nullopt;
                }
                if (price <= 0) {
                    LOG_ERROR(this->get_logger_source(), "Invalid limit order price: " + std::to_string(price));
                    return std::nullopt;
                }
                if (quantity <= 0) {
                    LOG_ERROR(this->get_logger_source(), "Invalid limit order quantity: " + std::to_string(quantity));
                    return std::nullopt;
                }
                if (!this->bus_) {
                    LOG_ERROR(this->get_logger_source(), "Cannot create limit order: EventBus not set.");
                    return std::nullopt;
                }

                ClientOrderIdType cid = next_client_order_id_++;

                try {
                    inventory_.limit_order_create_new(ModelSideToInventorySide(side), price, quantity, cid, symbol);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto order_evt_ptr = std::make_shared<const ModelEvents::LimitOrderEvent>(
                            current_time, symbol, side, price, quantity, timeout, cid
                    );

                    std::string stream_id = format_stream_id("order", this->get_id(), cid); // "order" stream used in python
                    std::string topic = format_topic("LimitOrderEvent", symbol);
                    publish_wrapper(topic, stream_id, order_evt_ptr);

                    LOG_DEBUG(this->get_logger_source(), "Created limit order: CID=" + std::to_string(cid) + ", Px=" + std::to_string(price) + ", Qty=" + std::to_string(quantity) + ", Side=" + ModelEvents::side_to_string(side) + ", Symbol=" + symbol);
                    return cid;

                } catch (const std::invalid_argument& e) {
                    LOG_ERROR(this->get_logger_source(), "Failed to create limit order in inventory (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--;
                    return std::nullopt;
                } catch (const std::exception& e) {
                    LOG_ERROR(this->get_logger_source(), "Unexpected error creating limit order (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--;
                    return std::nullopt;
                }
            }

            /**
             * @brief Create and publish a request to partially cancel an acknowledged limit order.
             * @param cid_target_order The CID of the limit order to partially cancel.
             * @param cancel_quantity The amount to cancel (must be > 0 and < order's current quantity).
             * @return True if the cancel request was successfully created and published, False otherwise.
             */
            bool create_partial_cancel_limit_order(
                    ClientOrderIdType cid_target_order,
                    QuantityType cancel_quantity
            ) {
                if (cancel_quantity <= 0) {
                    LOG_ERROR(this->get_logger_source(), "Invalid partial cancel quantity: " + std::to_string(cancel_quantity) + " for target CID: " + std::to_string(cid_target_order));
                    return false;
                }
                if (!this->bus_) {
                    LOG_ERROR(this->get_logger_source(), "Cannot create partial cancel limit order: EventBus not set.");
                    return false;
                }

                // Pre-check inventory state before generating CID
                auto details_opt = inventory_.get_acknowledged_limit_order_details(cid_target_order);
                if (!details_opt) {
                    LOG_WARNING(this->get_logger_source(), "Attempted partial cancel on non-acknowledged or non-existent limit order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_price, t_current_qty] = *details_opt;

                if (cancel_quantity >= t_current_qty) {
                    LOG_ERROR(this->get_logger_source(), "Partial cancel quantity (" + std::to_string(cancel_quantity) + ") must be less than target order quantity (" + std::to_string(t_current_qty) + ") for CID: " + std::to_string(cid_target_order) + ". Use full cancel instead.");
                    return false;
                }
                if (t_symbol != exchange_name_) {
                    LOG_ERROR(this->get_logger_source(), "Internal inconsistency: Target order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_);
                    return false; // Should not happen if inventory is managed correctly
                }


                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    // This call might throw if target is already cancel-pending
                    inventory_.limit_order_partial_cancel_create(cid_cancel, cid_target_order, cancel_quantity);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::PartialCancelLimitOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cancel_quantity, cid_cancel
                    );

                    std::string stream_id = format_stream_id("order", this->get_id(), cid_target_order); // Stream relates to target order
                    std::string topic = format_topic("PartialCancelLimitOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LOG_DEBUG(this->get_logger_source(), "Created partial cancel for limit order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order) + ", CancelQty=" + std::to_string(cancel_quantity));
                    return true;

                } catch (const std::out_of_range& e) { // Target not found (should have been caught above, but belt-and-suspenders)
                    LOG_WARNING(this->get_logger_source(), "Could not create partial cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) { // Catches invalid_argument (CID exists) or logic_error (already pending)
                    LOG_WARNING(this->get_logger_source(), "Could not create partial cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LOG_ERROR(this->get_logger_source(), "Unexpected error creating partial cancel for limit order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }


            /**
             * @brief Create and publish a request to fully cancel an acknowledged limit order.
             * @param cid_target_order The CID of the limit order to fully cancel.
             * @return True if the cancel request was successfully created and published, False otherwise.
             */
            bool create_full_cancel_limit_order(ClientOrderIdType cid_target_order) {
                if (!this->bus_) {
                    LOG_ERROR(this->get_logger_source(), "Cannot create full cancel limit order: EventBus not set.");
                    return false;
                }

                // Pre-check inventory
                auto details_opt = inventory_.get_acknowledged_limit_order_details(cid_target_order);
                if (!details_opt) {
                    LOG_WARNING(this->get_logger_source(), "Attempted full cancel on non-acknowledged or non-existent limit order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_price, t_qty] = *details_opt;
                if (t_symbol != exchange_name_) {
                    LOG_ERROR(this->get_logger_source(), "Internal inconsistency: Target order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_ + " during full cancel.");
                    return false;
                }

                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    inventory_.limit_order_full_cancel_create(cid_cancel, cid_target_order);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::FullCancelLimitOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cid_cancel
                    );

                    std::string stream_id = format_stream_id("order", this->get_id(), cid_target_order); // Stream relates to target
                    std::string topic = format_topic("FullCancelLimitOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LOG_DEBUG(this->get_logger_source(), "Created full cancel for limit order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order));
                    return true;

                } catch (const std::out_of_range& e) {
                    LOG_WARNING(this->get_logger_source(), "Could not create full cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) {
                    LOG_WARNING(this->get_logger_source(), "Could not create full cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LOG_ERROR(this->get_logger_source(), "Unexpected error creating full cancel for limit order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }

            /**
             * @brief Create and publish a request to fully cancel an acknowledged market order.
             * @param cid_target_order The CID of the market order to fully cancel.
             * @return True if the cancel request was successfully created and published, False otherwise.
             */
            bool create_full_cancel_market_order(ClientOrderIdType cid_target_order) {
                if (!this->bus_) {
                    LOG_ERROR(this->get_logger_source(), "Cannot create full cancel market order: EventBus not set.");
                    return false;
                }

                // Pre-check inventory
                auto details_opt = inventory_.get_acknowledged_market_order_details(cid_target_order);
                if (!details_opt) {
                    LOG_WARNING(this->get_logger_source(), "Attempted full cancel on non-acknowledged or non-existent market order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_qty] = *details_opt;
                if (t_symbol != exchange_name_) {
                    LOG_ERROR(this->get_logger_source(), "Internal inconsistency: Target market order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_ + " during full cancel.");
                    return false;
                }

                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    inventory_.market_order_full_cancel_create(cid_cancel, cid_target_order);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::FullCancelMarketOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cid_cancel
                    );

                    std::string stream_id = format_stream_id("market_order", this->get_id(), cid_target_order); // Stream relates to target
                    std::string topic = format_topic("FullCancelMarketOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LOG_DEBUG(this->get_logger_source(), "Created full cancel for market order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order));
                    return true;

                } catch (const std::out_of_range& e) {
                    LOG_WARNING(this->get_logger_source(), "Could not create full cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) {
                    LOG_WARNING(this->get_logger_source(), "Could not create full cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LOG_ERROR(this->get_logger_source(), "Unexpected error creating full cancel for market order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }

            /**
             * @brief Create and publish a request to partially cancel an acknowledged market order.
             * @param cid_target_order The CID of the market order to partially cancel.
             * @param cancel_quantity The amount to cancel (must be > 0 and < order's current quantity).
             * @return True if the cancel request was successfully created and published, False otherwise.
             */
            bool create_partial_cancel_market_order(
                    ClientOrderIdType cid_target_order,
                    QuantityType cancel_quantity
            ) {
                if (cancel_quantity <= 0) {
                    LOG_ERROR(this->get_logger_source(), "Invalid partial cancel quantity: " + std::to_string(cancel_quantity) + " for target market CID: " + std::to_string(cid_target_order));
                    return false;
                }
                if (!this->bus_) {
                    LOG_ERROR(this->get_logger_source(), "Cannot create partial cancel market order: EventBus not set.");
                    return false;
                }

                // Pre-check inventory
                auto details_opt = inventory_.get_acknowledged_market_order_details(cid_target_order);
                if (!details_opt) {
                    LOG_WARNING(this->get_logger_source(), "Attempted partial cancel on non-acknowledged or non-existent market order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_current_qty] = *details_opt;
                if (cancel_quantity >= t_current_qty) {
                    LOG_ERROR(this->get_logger_source(), "Partial cancel quantity (" + std::to_string(cancel_quantity) + ") must be less than target market order quantity (" + std::to_string(t_current_qty) + ") for CID: " + std::to_string(cid_target_order) + ". Use full cancel instead.");
                    return false;
                }
                if (t_symbol != exchange_name_) {
                    LOG_ERROR(this->get_logger_source(), "Internal inconsistency: Target market order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_);
                    return false;
                }

                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    inventory_.market_order_partial_cancel_create(cid_cancel, cid_target_order, cancel_quantity);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::PartialCancelMarketOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cancel_quantity, cid_cancel
                    );

                    std::string stream_id = format_stream_id("market_order", this->get_id(), cid_target_order); // Stream relates to target
                    std::string topic = format_topic("PartialCancelMarketOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LOG_DEBUG(this->get_logger_source(), "Created partial cancel for market order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order) + ", CancelQty=" + std::to_string(cancel_quantity));
                    return true;

                } catch (const std::out_of_range& e) {
                    LOG_WARNING(this->get_logger_source(), "Could not create partial cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) {
                    LOG_WARNING(this->get_logger_source(), "Could not create partial cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LOG_ERROR(this->get_logger_source(), "Unexpected error creating partial cancel for market order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }


            //--------------------------------------------------------------------------
            // Event Handling (Public Overloads for CRTP Dispatch)
            //
            // NOTE: These are public as they are called by the base EventProcessor
            // via the CRTP static_cast. They perform basic checks/inventory updates
            // and then delegate to the protected PURE VIRTUAL on_... methods which
            // must be implemented by the concrete derived algorithm class.
            //--------------------------------------------------------------------------

            // --- Market Data Handlers ---
            void handle_event(const ModelEvents::LTwoOrderBookEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return; // Ignore market data for other exchanges
                try { on_LTwoOrderBookEvent(event); }
                catch (const std::exception& e) { handle_exception("on_LTwoOrderBookEvent", e); }
                catch (...) { handle_unknown_exception("on_LTwoOrderBookEvent"); }
            }

            void handle_event(const ModelEvents::TradeEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return; // Ignore trades for other exchanges
                try { on_TradeEvent(event); }
                catch (const std::exception& e) { handle_exception("on_TradeEvent", e); }
                catch (...) { handle_unknown_exception("on_TradeEvent"); }
            }

            // --- Limit Order Handlers ---
            void handle_event(const ModelEvents::LimitOrderAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                // Check symbol just in case (though topic subscription should filter)
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_acknowledge_new(event.client_order_id);
                    on_LimitOrderAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_acknowledge_new", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_LimitOrderAckEvent", e); }
                catch (...) { handle_unknown_exception("on_LimitOrderAckEvent"); }
            }

            void handle_event(const ModelEvents::LimitOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_reject_new(event.client_order_id);
                    on_LimitOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_reject_new", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("limit_order_execute_reject_new", event.client_order_id, e); } // e.g., if already rejected
                catch (const std::exception& e) { handle_exception("on_LimitOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_LimitOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::FullFillLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    // Inventory update must happen first to remove the order
                    inventory_.core_limit_order_execute_full_fill(event.client_order_id);
                    // Then notify the derived algo
                    on_FullFillLimitOrderEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("core_limit_order_execute_full_fill", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("core_limit_order_execute_full_fill", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullFillLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_FullFillLimitOrderEvent"); }
            }

            void handle_event(const ModelEvents::PartialFillLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.core_limit_order_execute_partial_fill(event.client_order_id, event.leaves_qty, event.fill_qty);
                    on_PartialFillLimitOrderEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("core_limit_order_execute_partial_fill", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("core_limit_order_execute_partial_fill", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialFillLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialFillLimitOrderEvent"); }
            }

            void handle_event(const ModelEvents::LimitOrderExpiredEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_expired(event.client_order_id);
                    on_LimitOrderExpiredEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_expired", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("limit_order_execute_expired", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_LimitOrderExpiredEvent", e); }
                catch (...) { handle_unknown_exception("on_LimitOrderExpiredEvent"); }
            }

            // --- Limit Order Cancel Handlers ---
            void handle_event(const ModelEvents::FullCancelLimitOrderAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_full_cancel_acknowledge(event.client_order_id); // client_order_id is the CANCEL request CID
                    on_FullCancelLimitOrderAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_full_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelLimitOrderAckEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelLimitOrderAckEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelLimitAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_partial_cancel_acknowledge(event.client_order_id, event.remaining_qty); // client_order_id is CANCEL request CID
                    on_PartialCancelLimitAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_partial_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelLimitAckEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelLimitAckEvent"); }
            }

            void handle_event(const ModelEvents::FullCancelLimitOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_full_cancel_reject(event.client_order_id); // client_order_id is CANCEL request CID
                    on_FullCancelLimitOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_full_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelLimitOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelLimitOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelLimitOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_partial_cancel_reject(event.client_order_id); // client_order_id is CANCEL request CID
                    on_PartialCancelLimitOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_partial_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelLimitOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelLimitOrderRejectEvent"); }
            }

            // --- Market Order Handlers ---
            void handle_event(const ModelEvents::MarketOrderAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_execute_acknowledge_new(event.client_order_id);
                    on_MarketOrderAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_execute_acknowledge_new", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_MarketOrderAckEvent", e); }
                catch (...) { handle_unknown_exception("on_MarketOrderAckEvent"); }
            }

            void handle_event(const ModelEvents::MarketOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_execute_reject_new(event.client_order_id);
                    on_MarketOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_execute_reject_new", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("market_order_execute_reject_new", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_MarketOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_MarketOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::FullFillMarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.core_market_order_execute_full_fill(event.client_order_id);
                    on_FullFillMarketOrderEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("core_market_order_execute_full_fill", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("core_market_order_execute_full_fill", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullFillMarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_FullFillMarketOrderEvent"); }
            }

            void handle_event(const ModelEvents::PartialFillMarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.core_market_order_execute_partial_fill(event.client_order_id, event.leaves_qty);
                    on_PartialFillMarketOrderEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("core_market_order_execute_partial_fill", event.client_order_id, e); }
                    // Add logic error catch? Partial fill shouldn't normally have state issues unless qty is weird.
                catch (const std::exception& e) { handle_exception("on_PartialFillMarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialFillMarketOrderEvent"); }
            }

            void handle_event(const ModelEvents::MarketOrderExpiredEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_execute_expired(event.client_order_id);
                    on_MarketOrderExpiredEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_execute_expired", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("market_order_execute_expired", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_MarketOrderExpiredEvent", e); }
                catch (...) { handle_unknown_exception("on_MarketOrderExpiredEvent"); }
            }

            // --- Market Order Cancel Handlers ---
            void handle_event(const ModelEvents::FullCancelMarketOrderAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_execute_full_cancel_acknowledge(event.client_order_id); // client_order_id is CANCEL request CID
                    on_FullCancelMarketOrderAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_execute_full_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelMarketOrderAckEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelMarketOrderAckEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelMarketAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_execute_partial_cancel_acknowledge(event.client_order_id, event.remaining_qty); // client_order_id is CANCEL request CID
                    on_PartialCancelMarketAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_execute_partial_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelMarketAckEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelMarketAckEvent"); }
            }

            void handle_event(const ModelEvents::FullCancelMarketOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_full_cancel_reject(event.client_order_id); // client_order_id is CANCEL request CID
                    on_FullCancelMarketOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_full_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelMarketOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelMarketOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelMarketOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_partial_cancel_reject(event.client_order_id); // client_order_id is CANCEL request CID
                    on_PartialCancelMarketOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_partial_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelMarketOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelMarketOrderRejectEvent"); }
            }

            // --- Other Handlers ---
            void handle_event(const ModelEvents::Bang& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                // No inventory update for Bang
                try { on_Bang(event); }
                catch (const std::exception& e) { handle_exception("on_Bang", e); }
                catch (...) { handle_unknown_exception("on_Bang"); }
            }

            void handle_event(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                // This event confirms an order expired *because of a timeout trigger*.
                // The inventory state should be equivalent to receiving LimitOrderExpiredEvent.
                if (event.symbol != exchange_name_) return;
                try {
                    // Update inventory as if it expired normally
                    inventory_.limit_order_execute_expired(event.client_order_id); // client_order_id is the ORIGINAL order CID
                    on_AckTriggerExpiredLimitOrderEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_expired (from AckTrigger)", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("limit_order_execute_expired (from AckTrigger)", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_AckTriggerExpiredLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_AckTriggerExpiredLimitOrderEvent"); }
            }

            void handle_event(const ModelEvents::CheckLimitOrderExpirationEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                // This event might not be directly relevant for many algos to subscribe to,
                // but if it's in the EventVariant, a handler is needed.
                // It's typically processed by an internal scheduler/timer component.
                // For now, we'll just delegate to an on_ method.
                // Symbol check might not be relevant here if it's not market-specific.
                try { on_CheckLimitOrderExpirationEvent(event); }
                catch (const std::exception& e) { handle_exception("on_CheckLimitOrderExpirationEvent", e); }
                catch (...) { handle_unknown_exception("on_CheckLimitOrderExpirationEvent"); }
            }

            // --- ADDED HANDLERS FOR MISSING REQUEST EVENTS ---
            void handle_event(const ModelEvents::LimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received LimitOrderEvent (typically outgoing): " + event.to_string());
                try { on_LimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_LimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_LimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::MarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received MarketOrderEvent (typically outgoing): " + event.to_string());
                try { on_MarketOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_MarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_MarketOrderEvent"); }
            }
            void handle_event(const ModelEvents::PartialCancelLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received PartialCancelLimitOrderEvent (typically outgoing): " + event.to_string());
                try { on_PartialCancelLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelLimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::PartialCancelMarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received PartialCancelMarketOrderEvent (typically outgoing): " + event.to_string());
                try { on_PartialCancelMarketOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelMarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelMarketOrderEvent"); }
            }
            void handle_event(const ModelEvents::FullCancelLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received FullCancelLimitOrderEvent (typically outgoing): " + event.to_string());
                try { on_FullCancelLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_FullCancelLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelLimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::FullCancelMarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received FullCancelMarketOrderEvent (typically outgoing): " + event.to_string());
                try { on_FullCancelMarketOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_FullCancelMarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelMarketOrderEvent"); }
            }
            void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received TriggerExpiredLimitOrderEvent (typically internal to exchange adapter): " + event.to_string());
                try { on_TriggerExpiredLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_TriggerExpiredLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_TriggerExpiredLimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LOG_WARNING(this->get_logger_source(), "AlgoBase received RejectTriggerExpiredLimitOrderEvent (typically internal to exchange adapter): " + event.to_string());
                try { on_RejectTriggerExpiredLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_RejectTriggerExpiredLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_RejectTriggerExpiredLimitOrderEvent"); }
            }



            //--------------------------------------------------------------------------
            // Abstract Event Handlers (to be implemented by concrete derived classes)
            //--------------------------------------------------------------------------
        protected: // Make these protected, as they are implementation details for derived classes

            virtual void on_LTwoOrderBookEvent(const ModelEvents::LTwoOrderBookEvent& event) = 0;
            virtual void on_TradeEvent(const ModelEvents::TradeEvent& event) = 0;
            virtual void on_LimitOrderAckEvent(const ModelEvents::LimitOrderAckEvent& event) = 0;
            virtual void on_LimitOrderRejectEvent(const ModelEvents::LimitOrderRejectEvent& event) = 0;
            virtual void on_FullFillLimitOrderEvent(const ModelEvents::FullFillLimitOrderEvent& event) = 0;
            virtual void on_PartialFillLimitOrderEvent(const ModelEvents::PartialFillLimitOrderEvent& event) = 0;
            virtual void on_LimitOrderExpiredEvent(const ModelEvents::LimitOrderExpiredEvent& event) = 0;
            virtual void on_FullCancelLimitOrderAckEvent(const ModelEvents::FullCancelLimitOrderAckEvent& event) = 0;
            virtual void on_PartialCancelLimitAckEvent(const ModelEvents::PartialCancelLimitAckEvent& event) = 0;
            virtual void on_FullCancelLimitOrderRejectEvent(const ModelEvents::FullCancelLimitOrderRejectEvent& event) = 0;
            virtual void on_PartialCancelLimitOrderRejectEvent(const ModelEvents::PartialCancelLimitOrderRejectEvent& event) = 0;
            virtual void on_MarketOrderAckEvent(const ModelEvents::MarketOrderAckEvent& event) = 0;
            virtual void on_MarketOrderRejectEvent(const ModelEvents::MarketOrderRejectEvent& event) = 0;
            virtual void on_FullFillMarketOrderEvent(const ModelEvents::FullFillMarketOrderEvent& event) = 0;
            virtual void on_PartialFillMarketOrderEvent(const ModelEvents::PartialFillMarketOrderEvent& event) = 0;
            virtual void on_MarketOrderExpiredEvent(const ModelEvents::MarketOrderExpiredEvent& event) = 0;
            virtual void on_FullCancelMarketOrderAckEvent(const ModelEvents::FullCancelMarketOrderAckEvent& event) = 0;
            virtual void on_PartialCancelMarketAckEvent(const ModelEvents::PartialCancelMarketAckEvent& event) = 0;
            virtual void on_FullCancelMarketOrderRejectEvent(const ModelEvents::FullCancelMarketOrderRejectEvent& event) = 0;
            virtual void on_PartialCancelMarketOrderRejectEvent(const ModelEvents::PartialCancelMarketOrderRejectEvent& event) = 0;
            virtual void on_Bang(const ModelEvents::Bang& event) = 0;
            virtual void on_AckTriggerExpiredLimitOrderEvent(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event) = 0;
            virtual void on_CheckLimitOrderExpirationEvent(const ModelEvents::CheckLimitOrderExpirationEvent& event) = 0;

            // --- ADDED PURE VIRTUALS FOR MISSING REQUEST EVENTS ---
            virtual void on_LimitOrderEvent(const ModelEvents::LimitOrderEvent& event) = 0;
            virtual void on_MarketOrderEvent(const ModelEvents::MarketOrderEvent& event) = 0;
            virtual void on_PartialCancelLimitOrderEvent(const ModelEvents::PartialCancelLimitOrderEvent& event) = 0;
            virtual void on_PartialCancelMarketOrderEvent(const ModelEvents::PartialCancelMarketOrderEvent& event) = 0;
            virtual void on_FullCancelLimitOrderEvent(const ModelEvents::FullCancelLimitOrderEvent& event) = 0;
            virtual void on_FullCancelMarketOrderEvent(const ModelEvents::FullCancelMarketOrderEvent& event) = 0;
            virtual void on_TriggerExpiredLimitOrderEvent(const ModelEvents::TriggerExpiredLimitOrderEvent& event) = 0;
            virtual void on_RejectTriggerExpiredLimitOrderEvent(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event) = 0;


            //--------------------------------------------------------------------------
            // Protected Helper methods
            //--------------------------------------------------------------------------

            /**
             * @brief Wrapper for publishing events, adding logging.
             * Uses the `publish` method inherited from EventProcessor.
             * @tparam E The concrete event type.
             * @param topic The topic string to publish to.
             * @param stream_id_str Optional stream ID string.
             * @param event_ptr A shared_ptr to the constant event object.
             */
            template <typename E>
            void publish_wrapper(const std::string& topic, const std::string& stream_id_str, const std::shared_ptr<const E>& event_ptr) {
                if (!event_ptr) {
                    LOG_WARNING(this->get_logger_source(), "Attempted to publish a null event_ptr via wrapper. Topic: " + topic);
                    return;
                }
                // Use the base class publish method which takes shared_ptr
                this->publish(topic, event_ptr, stream_id_str);
                // Log *after* successful publish call (though it's async scheduling)
                LOG_DEBUG(this->get_logger_source(), "Scheduled event for topic '" + topic + "' on stream '" + stream_id_str + "' event: " + event_ptr->to_string());
            }

            /**
             * @brief Formats a topic string (e.g., "EventType.Identifier").
             */
            template <typename T>
            static std::string format_topic(const std::string& event_name, const T& identifier) {
                std::ostringstream oss;
                // Consider using fmt::format or std::format if available (C++20) for potentially better performance/syntax
                oss << event_name << "." << identifier;
                return oss.str();
            }

            /**
              * @brief Formats a stream ID string (e.g., "type_agentid_orderid").
              */
            template <typename T>
            static std::string format_stream_id(const std::string& type, AgentId agent_id, const T& order_id) {
                std::ostringstream oss;
                oss << type << "_" << agent_id << "_" << order_id;
                return oss.str();
            }

            /** @brief Logs standard exceptions from derived `on_...` handlers. */
            void handle_exception(const char* handler_name, const std::exception& e) {
                LOG_ERROR(this->get_logger_source(), std::string("Exception in ") + handler_name + ": " + e.what());
                // Optionally dump inventory state on error:
                // LOG_ERROR(this->get_logger_source(), "Inventory Snapshot:\n" + inventory_.snapshot());
            }

            /** @brief Logs unknown exceptions from derived `on_...` handlers. */
            void handle_unknown_exception(const char* handler_name) {
                LOG_ERROR(this->get_logger_source(), std::string("Unknown exception in ") + handler_name);
                // Optionally dump inventory state on error:
                // LOG_ERROR(this->get_logger_source(), "Inventory Snapshot:\n" + inventory_.snapshot());
            }

            /** @brief Logs exceptions originating from InventoryCore interactions. */
            void handle_inventory_exception(const char* inventory_method_name, ClientOrderIdType cid, const std::exception& e) {
                LOG_ERROR(this->get_logger_source(), "Inventory exception in " + std::string(inventory_method_name) + " for CID " + std::to_string(cid) + ": " + e.what());
                // Dump inventory state for debugging inventory issues
                LOG_ERROR(this->get_logger_source(), "Inventory Snapshot:\n" + inventory_.snapshot());
            }


        private:
            //--------------------------------------------------------------------------
            // Private Member Variables
            //--------------------------------------------------------------------------
            const SymbolType exchange_name_;
            ClientOrderIdType next_client_order_id_;
            InventoryCore inventory_;

            // Note: bus_, id_, is_processing_flag_, reentrant_event_queue_, sub_stream_last_ts_
            // are inherited from EventBusSystem::EventProcessor base class.

        }; // class AlgoBase

    } // namespace algo
} // namespace trading
