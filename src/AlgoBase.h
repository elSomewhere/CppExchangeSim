// file: src/AlgoBase.h
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
             * @param exchange_name The name (symbol) of the exchange this algo trades on.
             */
            explicit AlgoBase(SymbolType exchange_name) // <<<< MODIFIED: Removed agent_id parameter >>>>
                    : BaseAlgoEventProcessor<DerivedAlgo>(), // <<<< MODIFIED: Call base default constructor >>>>
                      exchange_name_(std::move(exchange_name)),
                      next_client_order_id_(1),
                      inventory_() {
                // Logging here will use the ID before it's set by the bus (likely 0)
                // This is generally acceptable, or logging can be moved to a post-registration setup.
                LogMessage(LogLevel::INFO, this->get_logger_source(), "AlgoBase constructed for exchange: " + exchange_name_ + ". Agent ID will be set upon registration.");
                // Subscriptions MOVED to setup_subscriptions()
            }

            virtual ~AlgoBase() override = default;

            void setup_subscriptions() {
                if (!this->bus_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "AlgoBase cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
                    return;
                }
                // ID is now set by the bus, so this->get_id() is valid here if called after registration.
                LogMessage(LogLevel::INFO, this->get_logger_source(), "AlgoBase agent " + std::to_string(this->get_id()) + " setting up subscriptions for exchange: " + exchange_name_);
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

            void create_full_cancel_all_limit_orders() {
                std::vector<ClientOrderIdType> ack_cids = inventory_.get_all_acknowledged_limit_orders_cid();
                int cancel_attempts = 0;
                for (ClientOrderIdType cid : ack_cids) {
                    if (inventory_.is_limit_order_acknowledged(cid)) {
                        auto details_opt = inventory_.get_acknowledged_limit_order_details(cid);
                        if (details_opt) {
                            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Attempting full cancel for acknowledged limit order CID: " + std::to_string(cid));
                            if (create_full_cancel_limit_order(cid)) {
                                cancel_attempts++;
                            }
                        } else {
                            LogMessage(LogLevel::WARNING, this->get_logger_source(), "Internal inconsistency: Could not retrieve details for acknowledged limit order CID: " + std::to_string(cid) + " during cancel-all.");
                        }
                    } else {
                        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Skipping cancel for CID: " + std::to_string(cid) + " - no longer acknowledged.");
                    }
                }
                if (cancel_attempts > 0) {
                    LogMessage(LogLevel::INFO, this->get_logger_source(), "Sent full cancel requests for " + std::to_string(cancel_attempts) + " acknowledged limit orders on exchange " + exchange_name_);
                } else {
                    LogMessage(LogLevel::INFO, this->get_logger_source(), "No acknowledged limit orders found to cancel on exchange " + exchange_name_);
                }
            }


            std::optional<ClientOrderIdType> create_market_order(
                    const SymbolType& symbol,
                    ModelEvents::Side side,
                    QuantityType quantity,
                    Duration timeout
            ) {
                if (symbol != this->exchange_name_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Market order symbol '" + symbol + "' does not match algo exchange '" + this->exchange_name_ + "'.");
                    return std::nullopt;
                }
                if (quantity <= 0) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Invalid market order quantity: " + std::to_string(quantity));
                    return std::nullopt;
                }
                if (!this->bus_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot create market order: EventBus not set.");
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
                    publish_wrapper(topic, stream_id, order_evt_ptr);

                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Created market order: CID=" + std::to_string(cid) + ", Qty=" + std::to_string(quantity) + ", Side=" + ModelEvents::side_to_string(side) + ", Symbol=" + symbol);
                    return cid;

                } catch (const std::invalid_argument& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Failed to create market order in inventory (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--;
                    return std::nullopt;
                } catch (const std::exception& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Unexpected error creating market order (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--;
                    return std::nullopt;
                }
            }

            std::optional<ClientOrderIdType> create_limit_order(
                    const SymbolType& symbol,
                    ModelEvents::Side side,
                    PriceType price,
                    QuantityType quantity,
                    Duration timeout
            ) {
                if (symbol != this->exchange_name_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Limit order symbol '" + symbol + "' does not match algo exchange '" + this->exchange_name_ + "'.");
                    return std::nullopt;
                }
                if (price <= 0) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Invalid limit order price: " + std::to_string(price));
                    return std::nullopt;
                }
                if (quantity <= 0) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Invalid limit order quantity: " + std::to_string(quantity));
                    return std::nullopt;
                }
                if (!this->bus_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot create limit order: EventBus not set.");
                    return std::nullopt;
                }

                ClientOrderIdType cid = next_client_order_id_++;

                try {
                    inventory_.limit_order_create_new(ModelSideToInventorySide(side), price, quantity, cid, symbol);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto order_evt_ptr = std::make_shared<const ModelEvents::LimitOrderEvent>(
                            current_time, symbol, side, price, quantity, timeout, cid
                    );

                    std::string stream_id = format_stream_id("order", this->get_id(), cid);
                    std::string topic = format_topic("LimitOrderEvent", symbol);
                    publish_wrapper(topic, stream_id, order_evt_ptr);

                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Created limit order: CID=" + std::to_string(cid) + ", Px=" + std::to_string(price) + ", Qty=" + std::to_string(quantity) + ", Side=" + ModelEvents::side_to_string(side) + ", Symbol=" + symbol);
                    return cid;

                } catch (const std::invalid_argument& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Failed to create limit order in inventory (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--;
                    return std::nullopt;
                } catch (const std::exception& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Unexpected error creating limit order (CID " + std::to_string(cid) + "): " + e.what());
                    next_client_order_id_--;
                    return std::nullopt;
                }
            }

            bool create_partial_cancel_limit_order(
                    ClientOrderIdType cid_target_order,
                    QuantityType cancel_quantity
            ) {
                if (cancel_quantity <= 0) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Invalid partial cancel quantity: " + std::to_string(cancel_quantity) + " for target CID: " + std::to_string(cid_target_order));
                    return false;
                }
                if (!this->bus_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot create partial cancel limit order: EventBus not set.");
                    return false;
                }

                auto details_opt = inventory_.get_acknowledged_limit_order_details(cid_target_order);
                if (!details_opt) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted partial cancel on non-acknowledged or non-existent limit order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_price, t_current_qty] = *details_opt;

                if (cancel_quantity >= t_current_qty) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Partial cancel quantity (" + std::to_string(cancel_quantity) + ") must be less than target order quantity (" + std::to_string(t_current_qty) + ") for CID: " + std::to_string(cid_target_order) + ". Use full cancel instead.");
                    return false;
                }
                if (t_symbol != exchange_name_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Internal inconsistency: Target order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_);
                    return false;
                }


                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    inventory_.limit_order_partial_cancel_create(cid_cancel, cid_target_order, cancel_quantity);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::PartialCancelLimitOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cancel_quantity, cid_cancel
                    );

                    std::string stream_id = format_stream_id("order", this->get_id(), cid_target_order);
                    std::string topic = format_topic("PartialCancelLimitOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Created partial cancel for limit order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order) + ", CancelQty=" + std::to_string(cancel_quantity));
                    return true;

                } catch (const std::out_of_range& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create partial cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create partial cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Unexpected error creating partial cancel for limit order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }


            bool create_full_cancel_limit_order(ClientOrderIdType cid_target_order) {
                if (!this->bus_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot create full cancel limit order: EventBus not set.");
                    return false;
                }

                auto details_opt = inventory_.get_acknowledged_limit_order_details(cid_target_order);
                if (!details_opt) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted full cancel on non-acknowledged or non-existent limit order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_price, t_qty] = *details_opt;
                if (t_symbol != exchange_name_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Internal inconsistency: Target order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_ + " during full cancel.");
                    return false;
                }

                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    inventory_.limit_order_full_cancel_create(cid_cancel, cid_target_order);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::FullCancelLimitOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cid_cancel
                    );

                    std::string stream_id = format_stream_id("order", this->get_id(), cid_target_order);
                    std::string topic = format_topic("FullCancelLimitOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Created full cancel for limit order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order));
                    return true;

                } catch (const std::out_of_range& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create full cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create full cancel for target limit CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Unexpected error creating full cancel for limit order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }

            bool create_full_cancel_market_order(ClientOrderIdType cid_target_order) {
                if (!this->bus_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot create full cancel market order: EventBus not set.");
                    return false;
                }

                auto details_opt = inventory_.get_acknowledged_market_order_details(cid_target_order);
                if (!details_opt) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted full cancel on non-acknowledged or non-existent market order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_qty] = *details_opt;
                if (t_symbol != exchange_name_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Internal inconsistency: Target market order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_ + " during full cancel.");
                    return false;
                }

                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    inventory_.market_order_full_cancel_create(cid_cancel, cid_target_order);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::FullCancelMarketOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cid_cancel
                    );

                    std::string stream_id = format_stream_id("market_order", this->get_id(), cid_target_order);
                    std::string topic = format_topic("FullCancelMarketOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Created full cancel for market order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order));
                    return true;

                } catch (const std::out_of_range& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create full cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create full cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Unexpected error creating full cancel for market order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }

            bool create_partial_cancel_market_order(
                    ClientOrderIdType cid_target_order,
                    QuantityType cancel_quantity
            ) {
                if (cancel_quantity <= 0) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Invalid partial cancel quantity: " + std::to_string(cancel_quantity) + " for target market CID: " + std::to_string(cid_target_order));
                    return false;
                }
                if (!this->bus_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot create partial cancel market order: EventBus not set.");
                    return false;
                }

                auto details_opt = inventory_.get_acknowledged_market_order_details(cid_target_order);
                if (!details_opt) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted partial cancel on non-acknowledged or non-existent market order CID: " + std::to_string(cid_target_order));
                    return false;
                }
                auto [t_cid, t_symbol, t_side, t_current_qty] = *details_opt;
                if (cancel_quantity >= t_current_qty) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Partial cancel quantity (" + std::to_string(cancel_quantity) + ") must be less than target market order quantity (" + std::to_string(t_current_qty) + ") for CID: " + std::to_string(cid_target_order) + ". Use full cancel instead.");
                    return false;
                }
                if (t_symbol != exchange_name_) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Internal inconsistency: Target market order CID=" + std::to_string(cid_target_order) + " has symbol " + t_symbol + " but algo is for " + exchange_name_);
                    return false;
                }

                ClientOrderIdType cid_cancel = next_client_order_id_++;

                try {
                    inventory_.market_order_partial_cancel_create(cid_cancel, cid_target_order, cancel_quantity);

                    Timestamp current_time = this->bus_->get_current_time();
                    auto cancel_evt_ptr = std::make_shared<const ModelEvents::PartialCancelMarketOrderEvent>(
                            current_time, exchange_name_, cid_target_order, cancel_quantity, cid_cancel
                    );

                    std::string stream_id = format_stream_id("market_order", this->get_id(), cid_target_order);
                    std::string topic = format_topic("PartialCancelMarketOrderEvent", exchange_name_);
                    publish_wrapper(topic, stream_id, cancel_evt_ptr);

                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Created partial cancel for market order: CancelCID=" + std::to_string(cid_cancel) + ", TargetCID=" + std::to_string(cid_target_order) + ", CancelQty=" + std::to_string(cancel_quantity));
                    return true;

                } catch (const std::out_of_range& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create partial cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::logic_error& e) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not create partial cancel for target market CID=" + std::to_string(cid_target_order) + " (Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                } catch (const std::exception& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Unexpected error creating partial cancel for market order (Target CID " + std::to_string(cid_target_order) + ", Cancel CID " + std::to_string(cid_cancel) + "): " + e.what());
                    next_client_order_id_--;
                    return false;
                }
            }


            //--------------------------------------------------------------------------
            // Event Handling (Public Overloads for CRTP Dispatch)
            //--------------------------------------------------------------------------

            void handle_event(const ModelEvents::LTwoOrderBookEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try { on_LTwoOrderBookEvent(event); }
                catch (const std::exception& e) { handle_exception("on_LTwoOrderBookEvent", e); }
                catch (...) { handle_unknown_exception("on_LTwoOrderBookEvent"); }
            }

            void handle_event(const ModelEvents::TradeEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try { on_TradeEvent(event); }
                catch (const std::exception& e) { handle_exception("on_TradeEvent", e); }
                catch (...) { handle_unknown_exception("on_TradeEvent"); }
            }

            void handle_event(const ModelEvents::LimitOrderAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
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
                catch (const std::logic_error& e) { handle_inventory_exception("limit_order_execute_reject_new", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_LimitOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_LimitOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::FullFillLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.core_limit_order_execute_full_fill(event.client_order_id);
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

            void handle_event(const ModelEvents::FullCancelLimitOrderAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_full_cancel_acknowledge(event.client_order_id);
                    on_FullCancelLimitOrderAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_full_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelLimitOrderAckEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelLimitOrderAckEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelLimitAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_partial_cancel_acknowledge(event.client_order_id, event.remaining_qty);
                    on_PartialCancelLimitAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_partial_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelLimitAckEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelLimitAckEvent"); }
            }

            void handle_event(const ModelEvents::FullCancelLimitOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_full_cancel_reject(event.client_order_id);
                    on_FullCancelLimitOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_full_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelLimitOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelLimitOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelLimitOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_partial_cancel_reject(event.client_order_id);
                    on_PartialCancelLimitOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_partial_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelLimitOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelLimitOrderRejectEvent"); }
            }

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

            void handle_event(const ModelEvents::FullCancelMarketOrderAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_execute_full_cancel_acknowledge(event.client_order_id);
                    on_FullCancelMarketOrderAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_execute_full_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelMarketOrderAckEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelMarketOrderAckEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelMarketAckEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_execute_partial_cancel_acknowledge(event.client_order_id, event.remaining_qty);
                    on_PartialCancelMarketAckEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_execute_partial_cancel_acknowledge", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelMarketAckEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelMarketAckEvent"); }
            }

            void handle_event(const ModelEvents::FullCancelMarketOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_full_cancel_reject(event.client_order_id);
                    on_FullCancelMarketOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_full_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_FullCancelMarketOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelMarketOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::PartialCancelMarketOrderRejectEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.market_order_partial_cancel_reject(event.client_order_id);
                    on_PartialCancelMarketOrderRejectEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("market_order_partial_cancel_reject", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelMarketOrderRejectEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelMarketOrderRejectEvent"); }
            }

            void handle_event(const ModelEvents::Bang& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                try { on_Bang(event); }
                catch (const std::exception& e) { handle_exception("on_Bang", e); }
                catch (...) { handle_unknown_exception("on_Bang"); }
            }

            void handle_event(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                if (event.symbol != exchange_name_) return;
                try {
                    inventory_.limit_order_execute_expired(event.client_order_id);
                    on_AckTriggerExpiredLimitOrderEvent(event);
                } catch (const std::out_of_range& e) { handle_inventory_exception("limit_order_execute_expired (from AckTrigger)", event.client_order_id, e); }
                catch (const std::logic_error& e) { handle_inventory_exception("limit_order_execute_expired (from AckTrigger)", event.client_order_id, e); }
                catch (const std::exception& e) { handle_exception("on_AckTriggerExpiredLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_AckTriggerExpiredLimitOrderEvent"); }
            }

            void handle_event(const ModelEvents::CheckLimitOrderExpirationEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                try { on_CheckLimitOrderExpirationEvent(event); }
                catch (const std::exception& e) { handle_exception("on_CheckLimitOrderExpirationEvent", e); }
                catch (...) { handle_unknown_exception("on_CheckLimitOrderExpirationEvent"); }
            }

            void handle_event(const ModelEvents::LimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received LimitOrderEvent (typically outgoing): " + event.to_string());
                try { on_LimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_LimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_LimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::MarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received MarketOrderEvent (typically outgoing): " + event.to_string());
                try { on_MarketOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_MarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_MarketOrderEvent"); }
            }
            void handle_event(const ModelEvents::PartialCancelLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received PartialCancelLimitOrderEvent (typically outgoing): " + event.to_string());
                try { on_PartialCancelLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelLimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::PartialCancelMarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received PartialCancelMarketOrderEvent (typically outgoing): " + event.to_string());
                try { on_PartialCancelMarketOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_PartialCancelMarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_PartialCancelMarketOrderEvent"); }
            }
            void handle_event(const ModelEvents::FullCancelLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received FullCancelLimitOrderEvent (typically outgoing): " + event.to_string());
                try { on_FullCancelLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_FullCancelLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelLimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::FullCancelMarketOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received FullCancelMarketOrderEvent (typically outgoing): " + event.to_string());
                try { on_FullCancelMarketOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_FullCancelMarketOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_FullCancelMarketOrderEvent"); }
            }
            void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received TriggerExpiredLimitOrderEvent (typically internal to exchange adapter): " + event.to_string());
                try { on_TriggerExpiredLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_TriggerExpiredLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_TriggerExpiredLimitOrderEvent"); }
            }
            void handle_event(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event, TopicId pub_topic_id, AgentId pub_id, Timestamp time, StreamId s_id, SequenceNumber seq) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "AlgoBase received RejectTriggerExpiredLimitOrderEvent (typically internal to exchange adapter): " + event.to_string());
                try { on_RejectTriggerExpiredLimitOrderEvent(event); }
                catch (const std::exception& e) { handle_exception("on_RejectTriggerExpiredLimitOrderEvent", e); }
                catch (...) { handle_unknown_exception("on_RejectTriggerExpiredLimitOrderEvent"); }
            }

        protected:

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

            virtual void on_LimitOrderEvent(const ModelEvents::LimitOrderEvent& event) = 0;
            virtual void on_MarketOrderEvent(const ModelEvents::MarketOrderEvent& event) = 0;
            virtual void on_PartialCancelLimitOrderEvent(const ModelEvents::PartialCancelLimitOrderEvent& event) = 0;
            virtual void on_PartialCancelMarketOrderEvent(const ModelEvents::PartialCancelMarketOrderEvent& event) = 0;
            virtual void on_FullCancelLimitOrderEvent(const ModelEvents::FullCancelLimitOrderEvent& event) = 0;
            virtual void on_FullCancelMarketOrderEvent(const ModelEvents::FullCancelMarketOrderEvent& event) = 0;
            virtual void on_TriggerExpiredLimitOrderEvent(const ModelEvents::TriggerExpiredLimitOrderEvent& event) = 0;
            virtual void on_RejectTriggerExpiredLimitOrderEvent(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event) = 0;


            template <typename E>
            void publish_wrapper(const std::string& topic, const std::string& stream_id_str, const std::shared_ptr<const E>& event_ptr) {
                if (!event_ptr) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted to publish a null event_ptr via wrapper. Topic: " + topic);
                    return;
                }
                this->publish(topic, event_ptr, stream_id_str);
                LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Scheduled event for topic '" + topic + "' on stream '" + stream_id_str + "' event: " + event_ptr->to_string());
            }

            template <typename T>
            static std::string format_topic(const std::string& event_name, const T& identifier) {
                std::ostringstream oss;
                oss << event_name << "." << identifier;
                return oss.str();
            }

            template <typename T>
            static std::string format_stream_id(const std::string& type, AgentId agent_id, const T& order_id) {
                std::ostringstream oss;
                oss << type << "_" << agent_id << "_" << order_id;
                return oss.str();
            }

            void handle_exception(const char* handler_name, const std::exception& e) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(), std::string("Exception in ") + handler_name + ": " + e.what());
            }

            void handle_unknown_exception(const char* handler_name) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(), std::string("Unknown exception in ") + handler_name);
            }

            void handle_inventory_exception(const char* inventory_method_name, ClientOrderIdType cid, const std::exception& e) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(), "Inventory exception in " + std::string(inventory_method_name) + " for CID " + std::to_string(cid) + ": " + e.what());
                LogMessage(LogLevel::ERROR, this->get_logger_source(), "Inventory Snapshot:\n" + inventory_.snapshot());
            }


        private:
            const SymbolType exchange_name_;
            ClientOrderIdType next_client_order_id_;
            InventoryCore inventory_;

        };

    }
}