// file: src/EventModelExchangeAdapter.h
#pragma once

#include "Model.h"          // Event types, ModelEventProcessor, ModelEventBus
#include "ExchangeServer.h" // The C++ ExchangeServer
#include "Globals.h"        // For SIDE, TIME_TYPE, ID_TYPE etc.

#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <optional>
#include <memory>
#include <algorithm> // For std::remove_if, std::equal
#include <sstream>   // For string formatting
#include <stdexcept> // For std::runtime_error

// Use the logging macros from EventBus.h (or Model.h if it re-exports them)
#ifndef LOG_DEBUG
#define LOG_DEBUG(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::DEBUG, (logger_source), (message))
#define LOG_INFO(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::INFO, (logger_source), (message))
#define LOG_WARNING(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::WARNING, (logger_source), (message))
#define LOG_ERROR(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::ERROR, (logger_source), (message))
#endif

class EventModelExchangeAdapter : public ModelEventProcessor<EventModelExchangeAdapter> {
public:
    // Type aliases for convenience from base and ModelEvents
    using Base = ModelEventProcessor<EventModelExchangeAdapter>;
    using AgentId = EventBusSystem::AgentId;
    using TopicId = EventBusSystem::TopicId;
    using StreamId = EventBusSystem::StreamId;
    using SequenceNumber = EventBusSystem::SequenceNumber;
    using Timestamp = EventBusSystem::Timestamp;

    using SymbolType = ModelEvents::SymbolType;
    using PriceType = ModelEvents::PriceType;     // int64_t
    using QuantityType = ModelEvents::QuantityType; // int64_t
    using ClientOrderIdType = ModelEvents::ClientOrderIdType; // uint64_t
    using ExchangeOrderIdType = ModelEvents::ExchangeOrderIdType; // uint64_t, maps to Globals.h ID_TYPE
    using Duration = ModelEvents::Duration;       // std::chrono::steady_clock::duration

    // Globals.h types used by ExchangeServer
    using ExchangeIDType = ::ID_TYPE; // Explicitly ::ID_TYPE
    using ExchangePriceType = ::PRICE_TYPE;
    using ExchangeQuantityType = ::SIZE_TYPE;
    using ExchangeTimeType = ::TIME_TYPE; // std::chrono::microseconds::rep (long long)
    using ExchangeSide = ::SIDE;         // enum class SIDE { BID, ASK, NONE };


    EventModelExchangeAdapter(SymbolType symbol, AgentId agent_id, ModelEventBus<>* bus_ptr_for_init_only = nullptr)
        : Base(agent_id),
          exchange_(),
          symbol_(std::move(symbol)),
          auto_publish_orderbook_(true) {
        // Crucially, do NOT pass the bus_ptr_for_init_only to Base constructor or call this->set_event_bus here.
        // this->bus_ will be set by TopicBasedEventBus::register_entity.
        // For agents that might need the bus pointer during construction for *other* reasons than subscription,
        // they can store bus_ptr_for_init_only in a temporary member if absolutely necessary,
        // but generally, operations requiring the bus (like subscribe/publish) should wait until after registration.
        // In this specific case, EventModelExchangeAdapter was already passing the bus to Base if provided,
        // which was part of the issue. We'll rely solely on register_entity to set this->bus_.

        _setup_callbacks(); // This is fine, doesn't use the bus for subscriptions
        // Subscriptions MOVED to setup_subscriptions()
        // LOG_INFO(...) moved to setup_subscriptions or kept here if it's just about construction.
        LOG_INFO(this->get_logger_source(), "EventModelExchangeAdapter constructed for agent " + std::to_string(this->get_id()) + " for symbol: " + symbol_);
    }

    virtual ~EventModelExchangeAdapter() override = default;

    // Add this new method
    void setup_subscriptions() {
        if (!this->bus_) {
            LOG_ERROR(this->get_logger_source(), "EventModelExchangeAdapter cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
            return;
        }
        LOG_INFO(this->get_logger_source(), "EventModelExchangeAdapter agent " + std::to_string(this->get_id()) + " setting up subscriptions for symbol: " + symbol_);
        this->subscribe(std::string("LimitOrderEvent.") + symbol_);
        this->subscribe(std::string("MarketOrderEvent.") + symbol_);
        this->subscribe(std::string("FullCancelLimitOrderEvent.") + symbol_);
        this->subscribe(std::string("FullCancelMarketOrderEvent.") + symbol_);
        this->subscribe(std::string("PartialCancelLimitOrderEvent.") + symbol_);
        this->subscribe(std::string("PartialCancelMarketOrderEvent.") + symbol_);
        this->subscribe("Bang");
        this->subscribe(std::string("TriggerExpiredLimitOrderEvent.") + symbol_);
    }

    // Typically, adapters managing resources or having complex state might be non-copyable/movable
    EventModelExchangeAdapter(const EventModelExchangeAdapter&) = delete;
    EventModelExchangeAdapter& operator=(const EventModelExchangeAdapter&) = delete;
    EventModelExchangeAdapter(EventModelExchangeAdapter&&) = delete;
    EventModelExchangeAdapter& operator=(EventModelExchangeAdapter&&) = delete;

private:
    ExchangeServer exchange_;
    SymbolType symbol_;
    bool auto_publish_orderbook_;

    // ID Mappings
    std::unordered_map<std::pair<AgentId, ClientOrderIdType>, ExchangeOrderIdType, EventBusSystem::PairHasher> trader_client_to_exchange_map_;
    std::unordered_map<ExchangeOrderIdType, std::pair<AgentId, ClientOrderIdType>> exchange_to_trader_client_map_;
    std::unordered_map<ExchangeOrderIdType, std::string> order_type_map_; // "limit" or "market"

    // These were in Python but might not be strictly necessary if lookups are efficient
    // std::unordered_map<AgentId, std::set<ExchangeOrderIdType>> trader_to_orders_map_;
    // std::unordered_map<ClientOrderIdType, std::set<ExchangeOrderIdType>> client_to_exchange_orders_map_;
    // std::unordered_map<ClientOrderIdType, std::vector<std::pair<AgentId, ExchangeOrderIdType>>> client_id_to_trader_exchanges_;


    // L2 state for diffing
    std::optional<ModelEvents::OrderBookLevel> last_published_bids_l2_;
    std::optional<ModelEvents::OrderBookLevel> last_published_asks_l2_;

    // --- Helper: Publish Wrapper ---
    template <typename E>
    void publish_wrapper(const std::string& topic_str, const std::string& stream_id_str, const std::shared_ptr<const E>& event_ptr) {
        if (!this->bus_) {
            LOG_ERROR(this->get_logger_source(), "EventBus not set, cannot publish event for topic: " + topic_str);
            return;
        }
        if (!event_ptr) {
             LOG_WARNING(this->get_logger_source(), "Attempted to publish a null event_ptr. Topic: " + topic_str);
             return;
        }
        LOG_DEBUG(this->get_logger_source(), "Publishing to topic '" + topic_str + "' on stream '" + stream_id_str + "': " + event_ptr->to_string());
        this->publish(topic_str, event_ptr, stream_id_str);
    }

    // Overload for events without a specific stream (stream_id will be auto-generated or empty by base)
    template <typename E>
    void publish_wrapper(const std::string& topic_str, const std::shared_ptr<const E>& event_ptr) {
         if (!this->bus_) {
            LOG_ERROR(this->get_logger_source(), "EventBus not set, cannot publish event for topic: " + topic_str);
            return;
        }
        if (!event_ptr) {
             LOG_WARNING(this->get_logger_source(), "Attempted to publish a null event_ptr. Topic: " + topic_str);
             return;
        }
        LOG_DEBUG(this->get_logger_source(), "Publishing to topic '" + topic_str + "': " + event_ptr->to_string());
        this->publish(topic_str, event_ptr);
    }

    // --- ID Management Methods ---
    void _register_order_mapping(AgentId trader_id, ClientOrderIdType client_order_id,
                                 ExchangeOrderIdType exchange_order_id, const std::string& order_type) {
        std::pair<AgentId, ClientOrderIdType> trader_client_key = {trader_id, client_order_id};
        trader_client_to_exchange_map_[trader_client_key] = exchange_order_id;
        exchange_to_trader_client_map_[exchange_order_id] = trader_client_key;
        order_type_map_[exchange_order_id] = order_type;
        LOG_DEBUG(this->get_logger_source(), "Registered mapping: Trader " + std::to_string(trader_id) +
            ", CID " + std::to_string(client_order_id) + " -> XID " + std::to_string(exchange_order_id) +
            " (Type: " + order_type + ")");
    }

    void _remove_order_mapping(ExchangeOrderIdType exchange_order_id) {
        auto it_xid_map = exchange_to_trader_client_map_.find(exchange_order_id);
        if (it_xid_map != exchange_to_trader_client_map_.end()) {
            std::pair<AgentId, ClientOrderIdType> trader_client_key = it_xid_map->second;

            trader_client_to_exchange_map_.erase(trader_client_key);
            exchange_to_trader_client_map_.erase(it_xid_map);
            order_type_map_.erase(exchange_order_id);
            LOG_DEBUG(this->get_logger_source(), "Removed mapping for XID " + std::to_string(exchange_order_id));
        } else {
            LOG_WARNING(this->get_logger_source(), "Attempted to remove mapping for non-existent XID " + std::to_string(exchange_order_id));
        }
    }

    std::optional<ExchangeOrderIdType> _get_exchange_order_id(AgentId trader_id, ClientOrderIdType client_order_id) const {
        auto it = trader_client_to_exchange_map_.find({trader_id, client_order_id});
        if (it != trader_client_to_exchange_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<std::pair<AgentId, ClientOrderIdType>> _get_trader_and_client_ids(ExchangeOrderIdType exchange_order_id) const {
        auto it = exchange_to_trader_client_map_.find(exchange_order_id);
        if (it != exchange_to_trader_client_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::string _format_topic_for_trader(const std::string& base_event_name, AgentId trader_id) {
        return base_event_name + "." + std::to_string(trader_id);
    }

    std::string _format_stream_id(AgentId trader_id, ClientOrderIdType client_order_id) {
        std::ostringstream oss;
        oss << "order_" << trader_id << "_" << client_order_id;
        return oss.str();
    }

    void _publish_orderbook_snapshot_if_changed() {
        if (!auto_publish_orderbook_ || !this->bus_) {
            return;
        }
        // This call will trigger _on_order_book_snapshot where diffing and publishing happen
        exchange_.get_order_book_snapshot();
    }

    // --- Callback Setup ---
    void _setup_callbacks();

public: // Public handle_event overloads for CRTP dispatch by EventProcessor base

    void handle_event(const ModelEvents::LimitOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return; // Process only for this adapter's symbol
        _process_limit_order(event, sender_id);
    }
    void handle_event(const ModelEvents::MarketOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
        _process_market_order(event, sender_id);
    }
    void handle_event(const ModelEvents::FullCancelLimitOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
        _process_full_cancel_limit_order(event, sender_id);
    }
    void handle_event(const ModelEvents::FullCancelMarketOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
        _process_full_cancel_market_order(event, sender_id);
    }
    void handle_event(const ModelEvents::PartialCancelLimitOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
        _process_partial_cancel_limit_order(event, sender_id);
    }
    void handle_event(const ModelEvents::PartialCancelMarketOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
        _process_partial_cancel_market_order(event, sender_id);
    }
    void handle_event(const ModelEvents::Bang& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        _process_bang(event);
    }
    void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
        _process_trigger_expired_limit_order_event(event);
    }

    // Dummy handlers for events published by this adapter or not directly processed if subscribed.
    void handle_event(const ModelEvents::LTwoOrderBookEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelLimitOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelMarketOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelLimitAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelMarketAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelLimitOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelLimitOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelMarketOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelMarketOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderExpiredEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderExpiredEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialFillLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullFillLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::TradeEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::RejectTriggerExpiredLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::AckTriggerExpiredLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::CheckLimitOrderExpirationEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}

private:
    // --- Private _process_... methods (called by public handle_event overloads) ---
    void _process_limit_order(const ModelEvents::LimitOrderEvent& event, AgentId trader_id);
    void _process_market_order(const ModelEvents::MarketOrderEvent& event, AgentId trader_id);
    void _process_full_cancel_limit_order(const ModelEvents::FullCancelLimitOrderEvent& event, AgentId trader_id);
    void _process_full_cancel_market_order(const ModelEvents::FullCancelMarketOrderEvent& event, AgentId trader_id);
    void _process_partial_cancel_limit_order(const ModelEvents::PartialCancelLimitOrderEvent& event, AgentId trader_id);
    void _process_partial_cancel_market_order(const ModelEvents::PartialCancelMarketOrderEvent& event, AgentId trader_id);
    void _process_bang(const ModelEvents::Bang& event);
    void _process_trigger_expired_limit_order_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event);

    // --- ExchangeServer Callback Handlers ---
    void _on_limit_order_acknowledged(ExchangeIDType xid, ExchangeSide side, ExchangePriceType price, ExchangeQuantityType quantity, ExchangeQuantityType remaining_qty, int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep);
    void _on_market_order_acknowledged(ExchangeSide side, ExchangeQuantityType req_qty, ExchangeQuantityType exec_qty, ExchangeQuantityType unfill_qty, int trader_id_int, int client_order_id_int);
    void _on_partial_cancel_limit(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType cancelled_qty, int trader_id_int_req, int client_order_id_int_req);
    void _on_partial_cancel_limit_reject(ExchangeIDType xid, int trader_id_int_req, int client_order_id_int_req);
    void _on_full_cancel_limit(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int_req, int client_order_id_int_req);
    void _on_full_cancel_limit_reject(ExchangeIDType xid, int trader_id_int_req, int client_order_id_int_req);

    void _on_trade(ExchangeIDType maker_xid, ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, bool maker_exhausted, int maker_trader_id_int, int maker_client_id_int, int taker_trader_id_int, int taker_client_id_int);

    void _on_maker_partial_fill_limit(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);
    void _on_taker_partial_fill_limit(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeQuantityType leaves_qty, int trader_id_int, int client_order_id_int);
    void _on_maker_full_fill_limit(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);
    void _on_taker_full_fill_limit(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);

    void _on_maker_partial_fill_market(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);
    void _on_taker_partial_fill_market(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeQuantityType leaves_qty, int trader_id_int, int client_order_id_int);
    void _on_maker_full_fill_market(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);
    void _on_taker_full_fill_market(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);

    void _on_order_book_snapshot(const std::vector<L2_DATA_TYPE>& bids, const std::vector<L2_DATA_TYPE>& asks);

    void _on_acknowledge_trigger_expiration(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep);
    void _on_reject_trigger_expiration(ExchangeIDType xid, int trader_id_int, int client_order_id_int);

    // Helper for side conversion
    ModelEvents::Side _to_model_side(ExchangeSide side) {
        return (side == ExchangeSide::BID) ? ModelEvents::Side::BUY : ModelEvents::Side::SELL;
    }
    ExchangeSide _to_exchange_side(ModelEvents::Side side) {
        return (side == ModelEvents::Side::BUY) ? ExchangeSide::BID : ExchangeSide::ASK;
    }
};


// --- Implementation of _setup_callbacks ---
void EventModelExchangeAdapter::_setup_callbacks() {
    exchange_.on_limit_order_acknowledged = [this](ExchangeIDType xid, ExchangeSide s, ExchangePriceType p, ExchangeQuantityType q, ExchangeQuantityType rq, int tid, int cid, ExchangeTimeType tus) {
        this->_on_limit_order_acknowledged(xid, s, p, q, rq, tid, cid, tus);
    };
    exchange_.on_market_order_acknowledged = [this](ExchangeSide s, ExchangeQuantityType rq, ExchangeQuantityType eq, ExchangeQuantityType uq, int tid, int cid) {
        this->_on_market_order_acknowledged(s, rq, eq, uq, tid, cid);
    };
    exchange_.on_partial_cancel_limit = [this](ExchangeIDType xid, ExchangePriceType p, ExchangeQuantityType cq, int tid_req, int cid_req) {
        this->_on_partial_cancel_limit(xid, p, cq, tid_req, cid_req);
    };
    exchange_.on_partial_cancel_limit_reject = [this](ExchangeIDType xid, int tid_req, int cid_req) {
        this->_on_partial_cancel_limit_reject(xid, tid_req, cid_req);
    };
    exchange_.on_full_cancel_limit = [this](ExchangeIDType xid, ExchangePriceType p, ExchangeQuantityType q, int tid_req, int cid_req) {
        this->_on_full_cancel_limit(xid, p, q, tid_req, cid_req);
    };
    exchange_.on_full_cancel_limit_reject = [this](ExchangeIDType xid, int tid_req, int cid_req) {
        this->_on_full_cancel_limit_reject(xid, tid_req, cid_req);
    };
    exchange_.on_trade = [this](ExchangeIDType mxid, ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, bool mex, int mtid, int mcid, int ttid, int tcid) {
        this->_on_trade(mxid, txid, p, q, mex, mtid, mcid, ttid, tcid);
    };
    exchange_.on_maker_partial_fill_limit = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid) {
        this->_on_maker_partial_fill_limit(mxid, p, q, tid, cid);
    };
    exchange_.on_taker_partial_fill_limit = [this](ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, ExchangeQuantityType lq, int tid, int cid) {
        this->_on_taker_partial_fill_limit(txid, p, q, lq, tid, cid);
    };
    exchange_.on_maker_full_fill_limit = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid) {
        this->_on_maker_full_fill_limit(mxid, p, q, tid, cid);
    };
    exchange_.on_taker_full_fill_limit = [this](ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid) {
        this->_on_taker_full_fill_limit(txid, p, q, tid, cid);
    };
    exchange_.on_maker_partial_fill_market = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid) {
        this->_on_maker_partial_fill_market(mxid, p, q, tid, cid);
    };
    exchange_.on_taker_partial_fill_market = [this](ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, ExchangeQuantityType lq, int tid, int cid) {
        this->_on_taker_partial_fill_market(txid, p, q, lq, tid, cid);
    };
    exchange_.on_maker_full_fill_market = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid) {
        this->_on_maker_full_fill_market(mxid, p, q, tid, cid);
    };
    exchange_.on_taker_full_fill_market = [this](ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid) {
        this->_on_taker_full_fill_market(txid, p, q, tid, cid);
    };
    exchange_.on_order_book_snapshot = [this](const std::vector<L2_DATA_TYPE>& b, const std::vector<L2_DATA_TYPE>& a) {
        this->_on_order_book_snapshot(b, a);
    };
    exchange_.on_acknowledge_trigger_expiration = [this](ExchangeIDType xid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid, ExchangeTimeType tus) {
        this->_on_acknowledge_trigger_expiration(xid, p, q, tid, cid, tus);
    };
    exchange_.on_reject_trigger_expiration = [this](ExchangeIDType xid, int tid, int cid) {
        this->_on_reject_trigger_expiration(xid, tid, cid);
    };
    // NOTE: Callbacks for modify_order_... are not handled in Python and thus omitted here for brevity,
    // but they would follow the same pattern if needed.
}

// --- Implementation of _process_... methods ---

void EventModelExchangeAdapter::_process_limit_order(const ModelEvents::LimitOrderEvent& event, AgentId trader_id) {
    ExchangeSide ex_side = _to_exchange_side(event.side);
    ExchangeTimeType timeout_us_rep = std::chrono::duration_cast<std::chrono::microseconds>(event.timeout).count();

    // ExchangeServer::place_limit_order returns the XID if it rests, or ID_DEFAULT if fully filled as taker.
    ExchangeIDType xid = exchange_.place_limit_order(
        ex_side, event.price, event.quantity, timeout_us_rep,
        static_cast<int>(trader_id), static_cast<int>(event.client_order_id)
    );

    // If xid is not ID_DEFAULT, it means the order (or part of it) rested on the book.
    // The on_limit_order_acknowledged callback will provide the definitive XID for resting orders.
    // If the order was fully filled as a taker, xid here will be ID_DEFAULT.
    // The mapping should ideally be done in the ack if it rests.
    // Python version registers if xid != -1 (-1 is Python's ID_DEFAULT equivalent).
    // C++ ID_DEFAULT is 0. ExchangeServer returns ID_DEFAULT if fully filled without resting.
    if (xid != ID_DEFAULT) {
        _register_order_mapping(trader_id, event.client_order_id, xid, "limit");
    }
    _publish_orderbook_snapshot_if_changed();
}

void EventModelExchangeAdapter::_process_market_order(const ModelEvents::MarketOrderEvent& event, AgentId trader_id) {
    ExchangeSide ex_side = _to_exchange_side(event.side);

    // Market orders are ephemeral but get a temporary XID from ExchangeServer for ACKs/Fills.
    ExchangeIDType temp_xid = exchange_.place_market_order(
        ex_side, event.quantity,
        static_cast<int>(trader_id), static_cast<int>(event.client_order_id)
    );

    // The temp_xid is the one that will appear in MarketOrderAckEvent and Fill events for this taker.
    // It's important to register this mapping so callbacks can correctly identify the original client order.
    // This mapping will be removed once the market order is fully resolved (e.g., in _on_taker_full_fill_market).
    _register_order_mapping(trader_id, event.client_order_id, temp_xid, "market");
    _publish_orderbook_snapshot_if_changed();
}

void EventModelExchangeAdapter::_process_full_cancel_limit_order(const ModelEvents::FullCancelLimitOrderEvent& event, AgentId trader_id) {
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, event.target_order_id);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    if (!xid_opt) {
        LOG_WARNING(this->get_logger_source(), "FullCancelLimitOrder: XID not found for Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));
        auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
            current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }
    ExchangeOrderIdType xid = *xid_opt;

    auto order_type_it = order_type_map_.find(xid);
    if (order_type_it == order_type_map_.end() || order_type_it->second != "limit") {
        LOG_WARNING(this->get_logger_source(), "FullCancelLimitOrder: Target XID " + std::to_string(xid) + " is not a limit order.");
        auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
            current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    // The cancel request itself also needs an ID mapping if its ACKs/REJs are to be tracked by client_order_id
    // Python's exchange server cancel_order doesn't return a new ID for the cancel *request*.
    // The client_order_id in the callback refers to the cancel request's CID.
    // So, we don't need to _register_order_mapping for the cancel event itself.
    // The callback _on_full_cancel_limit will use event.client_order_id for the ack.

    bool success = exchange_.cancel_order(xid, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
    if (success) {
        _publish_orderbook_snapshot_if_changed();
    }
    // ACKs/REJs handled by callbacks.
}

void EventModelExchangeAdapter::_process_full_cancel_market_order(const ModelEvents::FullCancelMarketOrderEvent& event, AgentId trader_id) {
    // Market orders are typically filled or expire too quickly to be cancelled.
    // We can try to find it, but expect rejection.
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, event.target_order_id);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    if (!xid_opt) {
         LOG_WARNING(this->get_logger_source(), "FullCancelMarketOrder: XID not found for Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));
    } else {
        ExchangeOrderIdType xid = *xid_opt;
        auto order_type_it = order_type_map_.find(xid);
        if (order_type_it == order_type_map_.end() || order_type_it->second != "market") {
            LOG_WARNING(this->get_logger_source(), "FullCancelMarketOrder: Target XID " + std::to_string(xid) + " is not a market order.");
        } else {
            // Attempt cancel, though likely to fail for market orders
            exchange_.cancel_order(xid, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
            // We don't expect success, so no snapshot publish here. Rejection callback will handle response.
            return; // Exit early, expect rejection via callback
        }
    }

    // If XID not found or not a market order, directly reject.
    auto reject_event = std::make_shared<const ModelEvents::FullCancelMarketOrderRejectEvent>(
        current_time, event.client_order_id, symbol_
    );
    publish_wrapper(_format_topic_for_trader("FullCancelMarketOrderRejectEvent", trader_id),
                    _format_stream_id(trader_id, event.client_order_id), reject_event);
}

void EventModelExchangeAdapter::_process_partial_cancel_limit_order(const ModelEvents::PartialCancelLimitOrderEvent& event, AgentId trader_id) {
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, event.target_order_id);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    if (!xid_opt) {
        LOG_WARNING(this->get_logger_source(), "PartialCancelLimitOrder: XID not found for Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
            current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }
    ExchangeOrderIdType xid = *xid_opt;

    auto order_type_it = order_type_map_.find(xid);
    if (order_type_it == order_type_map_.end() || order_type_it->second != "limit") {
        LOG_WARNING(this->get_logger_source(), "PartialCancelLimitOrder: Target XID " + std::to_string(xid) + " is not a limit order.");
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
            current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    std::optional<std::tuple<ExchangePriceType, ExchangeQuantityType, ExchangeSide>> details_opt = exchange_.get_order_details(xid);
    if (!details_opt) {
        LOG_WARNING(this->get_logger_source(), "PartialCancelLimitOrder: Could not get details for XID " + std::to_string(xid));
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
            current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    ExchangeQuantityType current_qty = std::get<1>(*details_opt);
    ExchangeQuantityType new_qty = current_qty - event.cancel_qty;

    bool success;
    if (new_qty <= 0) { // Treat as full cancel
        success = exchange_.cancel_order(xid, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
    } else {
        // ExchangeServer::modify_order_quantity uses TRIPLEOPTION::INPLACE which might change XID or remove.
        // The callback _on_partial_cancel_limit will be triggered if successful.
        // The python version directly calls modify_order_quantity from exchange.
        success = exchange_.modify_order_quantity(xid, new_qty, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
    }

    if (success) {
        _publish_orderbook_snapshot_if_changed();
    }
    // ACKs/REJs handled by callbacks. If modify_order_quantity rejects, its callback will fire.
    // If it leads to _on_partial_cancel_limit, that's the ack.
}

void EventModelExchangeAdapter::_process_partial_cancel_market_order(const ModelEvents::PartialCancelMarketOrderEvent& event, AgentId trader_id) {
    // Market orders are generally not partially cancellable.
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    LOG_WARNING(this->get_logger_source(), "PartialCancelMarketOrder: Market orders cannot typically be partially cancelled. Rejecting. Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));

    auto reject_event = std::make_shared<const ModelEvents::PartialCancelMarketOrderRejectEvent>(
        current_time, event.client_order_id, symbol_
    );
    publish_wrapper(_format_topic_for_trader("PartialCancelMarketOrderRejectEvent", trader_id),
                    _format_stream_id(trader_id, event.client_order_id), reject_event);
}

void EventModelExchangeAdapter::_process_bang(const ModelEvents::Bang& event) {
    LOG_INFO(this->get_logger_source(), "Processing Bang event. Flushing exchange and mappings.");
    trader_client_to_exchange_map_.clear();
    exchange_to_trader_client_map_.clear();
    order_type_map_.clear();
    // trader_to_orders_map_.clear();
    // client_to_exchange_orders_map_.clear();
    // client_id_to_trader_exchanges_.clear();

    last_published_bids_l2_ = std::nullopt;
    last_published_asks_l2_ = std::nullopt;

    exchange_.flush();

    // Publish Bang event to everyone (global topic)
    publish_wrapper("Bang", std::make_shared<const ModelEvents::Bang>(event.created_ts));
    _publish_orderbook_snapshot_if_changed(); // Publish empty book state
}

void EventModelExchangeAdapter::_process_trigger_expired_limit_order_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event) {
    // This event comes from CancelFairy (or similar) to the adapter.
    // The adapter then instructs its ExchangeServer instance.
    LOG_DEBUG(this->get_logger_source(), "Processing TriggerExpiredLimitOrderEvent for XID: " + std::to_string(event.target_exchange_order_id));

    auto xid_to_cancel = event.target_exchange_order_id;
    ExchangeTimeType timeout_us_rep = std::chrono::duration_cast<std::chrono::microseconds>(event.timeout_value).count();

    // ExchangeServer::cancel_expired_order will call appropriate callbacks (_on_acknowledge_trigger_expiration or _on_reject_trigger_expiration)
    bool call_succeeded = exchange_.cancel_expired_order(xid_to_cancel, timeout_us_rep);

    if (call_succeeded) { // This means the order was found and cancel attempt was made; outcome via callback.
        _publish_orderbook_snapshot_if_changed();
    } else {
        // If cancel_expired_order itself returns false, it means order wasn't found by ExchangeServer *prior* to cancel logic.
        // This case should also trigger a reject back to CancelFairy.
        // The _on_reject_trigger_expiration callback is for when cancel logic *inside* ExchangeServer fails.
        // This is a subtle difference. If ExchangeServer::cancel_expired_order returns false directly,
        // it means the order wasn't even on its books to begin with.
        LOG_WARNING(this->get_logger_source(), "TriggerExpired: Order XID " + std::to_string(xid_to_cancel) + " not found by ExchangeServer.cancel_expired_order. Manually sending reject.");

        Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
        // We don't have trader_id/client_id if order not found in exchange_to_trader_client_map_
        // The callback on_reject_trigger_expiration in ExchangeServer *does* get trader/client IDs.
        // This path implies the ExchangeServer itself couldn't find it.
        // The ModelEvent needs these to be properly formed.
        // For consistency, let the ExchangeServer's internal rejection logic (which calls the _on_reject_trigger_expiration callback) handle this.
        // So if cancel_expired_order returns false, it means it has already called on_reject_trigger_expiration.
    }
}


// --- Implementation of _on_... ExchangeServer Callback Handlers ---
// These are called by ExchangeServer, they create and publish ModelEvents

void EventModelExchangeAdapter::_on_limit_order_acknowledged(
    ExchangeIDType xid, ExchangeSide ex_side, ExchangePriceType price,
    ExchangeQuantityType quantity, ExchangeQuantityType remaining_qty,
    int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    ModelEvents::Side model_side = _to_model_side(ex_side);
    Duration timeout_duration = std::chrono::microseconds(timeout_us_rep);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::LimitOrderAckEvent>(
        current_time, xid, client_order_id, model_side, price, quantity, symbol_, timeout_duration
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("LimitOrderAckEvent", trader_id), stream_id_str, ack_event);
    publish_wrapper("LimitOrderAckEvent", stream_id_str, ack_event); // For CancelFairy

    // If remaining_quantity is 0 and xid is ID_DEFAULT, it means the limit order was fully filled as a taker and never rested.
    // If remaining_quantity is 0 and xid is NOT ID_DEFAULT, it means it rested, got some fills, and then this ack is for the resting part which got filled.
    // If the order is fully filled as a taker (xid == ID_DEFAULT), its mapping (if any was made for a temporary ID) would be cleaned up by fill handlers.
    // If it rested (xid != ID_DEFAULT) and is now reported as fully gone (remaining_qty == 0), remove its mapping.
    if (xid != ID_DEFAULT && remaining_qty == 0) {
        _remove_order_mapping(xid);
    }
    // If xid != ID_DEFAULT and it's still on book (remaining_qty > 0), ensure mapping exists.
    // This mapping should have been done in _process_limit_order if xid was returned.
    // Or, if place_limit_order in ExchangeServer generated a *new* xid for a resting part after some fills,
    // this ack might be the first time we see this definitive resting xid.
    else if (xid != ID_DEFAULT && remaining_qty > 0) {
        if (exchange_to_trader_client_map_.find(xid) == exchange_to_trader_client_map_.end()) {
             // This can happen if the original xid from place_limit_order was ID_DEFAULT (full taker fill initially)
             // but the ExchangeServer's internal logic for limit_match_book creates a *new* xid for any resting portion.
             // The ack is for this new resting xid.
            _register_order_mapping(trader_id, client_order_id, xid, "limit");
        }
    }
}

void EventModelExchangeAdapter::_on_market_order_acknowledged(
    ExchangeSide ex_side, ExchangeQuantityType req_qty, ExchangeQuantityType exec_qty,
    ExchangeQuantityType unfill_qty, int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    ModelEvents::Side model_side = _to_model_side(ex_side);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    // The XID for a market order ack should be the temporary XID registered in _process_market_order
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, client_order_id);
    ExchangeOrderIdType xid_for_ack = xid_opt.value_or(ID_DEFAULT); // Use ID_DEFAULT if somehow not found

    auto ack_event = std::make_shared<const ModelEvents::MarketOrderAckEvent>(
        current_time, xid_for_ack, client_order_id, model_side, req_qty, symbol_
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("MarketOrderAckEvent", trader_id), stream_id_str, ack_event);

    // If unfilled_qty == 0 and executed_qty == req_qty, the market order is fully done.
    // Its mapping (using xid_for_ack) will be removed by the taker_full_fill_market callback.
}

void EventModelExchangeAdapter::_on_partial_cancel_limit(
    ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType cancelled_qty,
    int trader_id_int_req, int client_order_id_int_req) {

    AgentId req_trader_id = static_cast<AgentId>(trader_id_int_req); // ID of who made the cancel request
    ClientOrderIdType req_client_order_id = static_cast<ClientOrderIdType>(client_order_id_int_req); // CID of the cancel request

    auto original_ids_opt = _get_trader_and_client_ids(xid); // Get original IDs of the order being cancelled
    if (!original_ids_opt) {
        LOG_ERROR(this->get_logger_source(), "PartialCancelLimit ACK for unknown XID: " + std::to_string(xid));
        // Potentially send a reject for the cancel request CID if it's known? Or rely on ExchangeServer to have rejected.
        return;
    }
    AgentId original_trader_id = original_ids_opt->first;
    ClientOrderIdType original_client_order_id = original_ids_opt->second;

    std::optional<std::tuple<ExchangePriceType, ExchangeQuantityType, ExchangeSide>> details_opt = exchange_.get_order_details(xid);
    ExchangeQuantityType remaining_qty = 0;
    ExchangeSide ex_side = ExchangeSide::NONE;
    ExchangeQuantityType original_total_qty_before_cancel = 0;

    if (details_opt) {
        remaining_qty = std::get<1>(*details_opt);
        ex_side = std::get<2>(*details_opt);
        original_total_qty_before_cancel = remaining_qty + cancelled_qty; // Reconstruct
    } else {
        LOG_WARNING(this->get_logger_source(), "PartialCancelLimit ACK for XID " + std::to_string(xid) + " but details not found. Estimating side/qty.");
        // This case is tricky. If details are gone, it might have been fully cancelled/filled by another means.
        // For now, proceed with best effort.
        original_total_qty_before_cancel = cancelled_qty; // Best guess
    }
    ModelEvents::Side model_side = _to_model_side(ex_side);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::PartialCancelLimitAckEvent>(
        current_time,
        ID_DEFAULT, // exchange_order_id for the ack event itself (often 0 or original XID)
        req_client_order_id, // CID of the cancel request
        model_side, // Side of the original order
        original_client_order_id, // Target CID (original order that was partially cancelled)
        original_total_qty_before_cancel, // Original quantity of the order *before this cancel*
        symbol_,
        cancelled_qty, // Quantity confirmed cancelled by this operation
        remaining_qty  // Quantity remaining on the order *after this cancel*
    );

    std::string stream_id_str = _format_stream_id(original_trader_id, original_client_order_id); // Stream of the original order
    publish_wrapper(_format_topic_for_trader("PartialCancelLimitAckEvent", req_trader_id), stream_id_str, ack_event);

    if (remaining_qty == 0 && xid != ID_DEFAULT) { // If order is now gone
        _remove_order_mapping(xid);
    }
}

void EventModelExchangeAdapter::_on_partial_cancel_limit_reject(
    ExchangeIDType xid, int trader_id_int_req, int client_order_id_int_req) {

    AgentId req_trader_id = static_cast<AgentId>(trader_id_int_req);
    ClientOrderIdType req_client_order_id = static_cast<ClientOrderIdType>(client_order_id_int_req);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
        current_time, req_client_order_id, symbol_
    );

    // Try to find original order's trader/client ID for stream consistency if XID is valid
    std::string stream_id_str;
    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if(original_ids_opt) {
        stream_id_str = _format_stream_id(original_ids_opt->first, original_ids_opt->second);
    } else { // Fallback if original order for XID is gone or XID was invalid
        stream_id_str = _format_stream_id(req_trader_id, req_client_order_id);
    }

    publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", req_trader_id), stream_id_str, reject_event);
}

void EventModelExchangeAdapter::_on_full_cancel_limit(
    ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int_req, int client_order_id_int_req) {

    AgentId req_trader_id = static_cast<AgentId>(trader_id_int_req);
    ClientOrderIdType req_client_order_id = static_cast<ClientOrderIdType>(client_order_id_int_req);

    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if (!original_ids_opt) {
        LOG_ERROR(this->get_logger_source(), "FullCancelLimit ACK for unknown XID: " + std::to_string(xid));
        return;
    }
    AgentId original_trader_id = original_ids_opt->first;
    ClientOrderIdType original_client_order_id = original_ids_opt->second;

    // Side needs to be inferred or stored; ExchangeServer::delete_limit_order doesn't return side with result.
    // Get it from OrderBookWrapper if possible, or from stored order_type_map if reliable.
    // For now, assuming we might not have the side easily. Let's use a default or try to get it.
    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default, attempt to improve
    auto details_opt = exchange_.get_order_details(xid); // This might fail if order is already removed by delete_limit_order
    if(details_opt) model_side = _to_model_side(std::get<2>(*details_opt));
    // If details_opt is null, then the order is truly gone. qty is the amount that *was* cancelled.

    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::FullCancelLimitOrderAckEvent>(
        current_time, xid, req_client_order_id, model_side, original_client_order_id, qty, symbol_
    );

    std::string stream_id_str = _format_stream_id(original_trader_id, original_client_order_id);
    publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderAckEvent", req_trader_id), stream_id_str, ack_event);
    publish_wrapper("FullCancelLimitOrderAckEvent", stream_id_str, ack_event); // For CancelFairy

    _remove_order_mapping(xid);
}

void EventModelExchangeAdapter::_on_full_cancel_limit_reject(
    ExchangeIDType xid, int trader_id_int_req, int client_order_id_int_req) {

    AgentId req_trader_id = static_cast<AgentId>(trader_id_int_req);
    ClientOrderIdType req_client_order_id = static_cast<ClientOrderIdType>(client_order_id_int_req);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
        current_time, req_client_order_id, symbol_
    );

    std::string stream_id_str;
    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if(original_ids_opt) {
        stream_id_str = _format_stream_id(original_ids_opt->first, original_ids_opt->second);
    } else {
        stream_id_str = _format_stream_id(req_trader_id, req_client_order_id);
    }

    publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", req_trader_id), stream_id_str, reject_event);
}

void EventModelExchangeAdapter::_on_trade(
    ExchangeIDType maker_xid, ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty,
    bool maker_exhausted, int maker_trader_id_int, int maker_client_id_int,
    int taker_trader_id_int, int taker_client_id_int) {

    AgentId maker_trader_id = static_cast<AgentId>(maker_trader_id_int);
    ClientOrderIdType maker_client_id = static_cast<ClientOrderIdType>(maker_client_id_int);
    AgentId taker_trader_id = static_cast<AgentId>(taker_trader_id_int);
    ClientOrderIdType taker_client_id = static_cast<ClientOrderIdType>(taker_client_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side maker_model_side = ModelEvents::Side::BUY; // Default
    auto maker_details_opt = exchange_.get_order_details(maker_xid); // This might be slow if called per trade
    if (maker_details_opt) {
        maker_model_side = _to_model_side(std::get<2>(*maker_details_opt));
    } else {
        LOG_WARNING(this->get_logger_source(), "Trade: Could not get details for maker XID " + std::to_string(maker_xid) + " to determine side.");
    }

    auto trade_event = std::make_shared<const ModelEvents::TradeEvent>(
        current_time, symbol_, maker_client_id, taker_client_id, maker_xid, taker_xid,
        price, qty, maker_model_side, maker_exhausted
    );

    // Publish to a general trade topic for the symbol
    // Stream ID could be complex (e.g., price_time based) or just related to one of the orders.
    // Python uses maker's stream.
    std::string maker_stream_id_str = _format_stream_id(maker_trader_id, maker_client_id);
    std::string taker_stream_id_str = _format_stream_id(taker_trader_id, taker_client_id);

    std::string trade_topic = std::string("TradeEvent.") + symbol_;
    publish_wrapper(trade_topic, maker_stream_id_str, trade_event);
    if (maker_stream_id_str != taker_stream_id_str) { // Avoid double-publish if self-trade on same stream context (unlikely here)
         publish_wrapper(trade_topic, taker_stream_id_str, trade_event);
    }
}

void EventModelExchangeAdapter::_on_maker_partial_fill_limit(
    ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ExchangeQuantityType leaves_qty = 0;
    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default
    ExchangeQuantityType cumulative_qty_filled_so_far = qty; // Approximation for this single event
    PriceType avg_price_so_far = price; // Approximation

    auto details_opt = exchange_.get_order_details(maker_xid);
    if (details_opt) {
        leaves_qty = std::get<1>(*details_opt);
        model_side = _to_model_side(std::get<2>(*details_opt));
        // More accurate cumulative and average price would require tracking total filled for this XID.
    } else {
        LOG_WARNING(this->get_logger_source(), "MakerPartialFillLimit: Could not get details for XID " + std::to_string(maker_xid));
    }

    auto fill_event = std::make_shared<const ModelEvents::PartialFillLimitOrderEvent>(
        current_time, maker_xid, client_order_id, model_side, price, qty, current_time, symbol_, true, // is_maker=true
        leaves_qty, cumulative_qty_filled_so_far, static_cast<double>(avg_price_so_far)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
}

void EventModelExchangeAdapter::_on_taker_partial_fill_limit(
    ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeQuantityType leaves_qty,
    int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default
    ExchangeQuantityType cumulative_qty_filled_so_far = qty;
    PriceType avg_price_so_far = price;

    // Taker XID might be ID_DEFAULT if it was never placed.
    // Its side comes from ExchangeServer's active_taker_side_
    if (exchange_.active_taker_side_.has_value()) { // C++ ExchangeServer has active_taker_side_ (optional<SIDE>)
         model_side = _to_model_side(exchange_.active_taker_side_.value());
    } else if (taker_xid != ID_DEFAULT) {
        auto details_opt = exchange_.get_order_details(taker_xid);
        if (details_opt) model_side = _to_model_side(std::get<2>(*details_opt));
    } else {
        LOG_WARNING(this->get_logger_source(), "TakerPartialFillLimit: Could not determine side for taker XID " + std::to_string(taker_xid));
    }

    auto fill_event = std::make_shared<const ModelEvents::PartialFillLimitOrderEvent>(
        current_time, taker_xid, client_order_id, model_side, price, qty, current_time, symbol_, false, // is_maker=false
        leaves_qty, cumulative_qty_filled_so_far, static_cast<double>(avg_price_so_far)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
}

void EventModelExchangeAdapter::_on_maker_full_fill_limit(
    ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default
    // Original details might be gone after full fill. Side might need to be from mapping or last known state.
    // For simplicity, if get_order_details fails (order already removed by core logic), we use default.
    // ExchangeServer's fill logic should ideally provide side.
    auto details_opt = _get_trader_and_client_ids(maker_xid); // Use our map that persists until explicitly removed
    if(details_opt) { // This is to get original trader_id/client_order_id for consistency
        // To get side, one would need to query order_book.get_order_side(maker_xid) *before* it's removed by fill.
        // This is a common race. Best if ExchangeServer callback provided side of filled order.
        // Assuming it was a limit order, its side was fixed.
        // The current exchange_.get_order_details(maker_xid) might fail.
        // Let's try to get it from order_book if it has a method, or assume based on price relation to market if desperate.
        // For now, a placeholder:
        // ModelEvents::Side model_side = determine_side_somehow(maker_xid);
    }
    // A more robust way: query order_book before it's removed, or have callback provide side.
    // Here, we'll rely on client_order_id being correct from map.
    // If ExchangeServer::get_order_details(maker_xid) is called *after* book removal, it fails.
    // Let's assume for now the callback `trader_id_int, client_order_id_int` are correct for the *maker*.

    auto fill_event = std::make_shared<const ModelEvents::FullFillLimitOrderEvent>(
        current_time, maker_xid, client_order_id, model_side, price, qty, current_time, symbol_, true, // is_maker=true
        static_cast<double>(price) // Simplistic avg_price for full fill of one chunk
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
    publish_wrapper("FullFillLimitOrderEvent", stream_id_str, fill_event); // For CancelFairy

    _remove_order_mapping(maker_xid);
}

void EventModelExchangeAdapter::_on_taker_full_fill_limit(
    ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default
    if (exchange_.active_taker_side_.has_value()) {
         model_side = _to_model_side(exchange_.active_taker_side_.value());
    } else if (taker_xid != ID_DEFAULT) { // If it was a resting limit order acting as taker
        auto details_opt = exchange_.get_order_details(taker_xid);
        if (details_opt) model_side = _to_model_side(std::get<2>(*details_opt));
    }

    auto fill_event = std::make_shared<const ModelEvents::FullFillLimitOrderEvent>(
        current_time, taker_xid, client_order_id, model_side, price, qty, current_time, symbol_, false, // is_maker=false
        static_cast<double>(price)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
    if (taker_xid != ID_DEFAULT) { // Only publish general event if it was a placed order
         publish_wrapper("FullFillLimitOrderEvent", stream_id_str, fill_event);
        _remove_order_mapping(taker_xid);
    }
}

void EventModelExchangeAdapter::_on_maker_partial_fill_market(
    ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int, int client_order_id_int) {
    // This is essentially same as _on_maker_partial_fill_limit because maker is always limit
    _on_maker_partial_fill_limit(maker_xid, price, qty, trader_id_int, client_order_id_int);
}

void EventModelExchangeAdapter::_on_taker_partial_fill_market(
    ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeQuantityType leaves_qty,
    int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default
    if (exchange_.active_taker_side_.has_value()) {
         model_side = _to_model_side(exchange_.active_taker_side_.value());
    }
    // Taker XID for market order is the temporary one assigned by ExchangeServer.

    auto fill_event = std::make_shared<const ModelEvents::PartialFillMarketOrderEvent>(
        current_time, taker_xid, client_order_id, model_side, price, qty, current_time, symbol_, false, // is_maker=false
        leaves_qty, qty, static_cast<double>(price) // cumulative & avg_price are approximations here
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillMarketOrderEvent", trader_id), stream_id_str, fill_event);
}

void EventModelExchangeAdapter::_on_maker_full_fill_market(
    ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int, int client_order_id_int) {
    // This is essentially same as _on_maker_full_fill_limit
    _on_maker_full_fill_limit(maker_xid, price, qty, trader_id_int, client_order_id_int);
}

void EventModelExchangeAdapter::_on_taker_full_fill_market(
    ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default
    if (exchange_.active_taker_side_.has_value()) {
         model_side = _to_model_side(exchange_.active_taker_side_.value());
    }

    auto fill_event = std::make_shared<const ModelEvents::FullFillMarketOrderEvent>(
        current_time, taker_xid, client_order_id, model_side, price, qty, current_time, symbol_, false, // is_maker=false
        static_cast<double>(price) // Simplistic avg_price
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillMarketOrderEvent", trader_id), stream_id_str, fill_event);

    // Market order fully resolved, remove its mapping (taker_xid is the temporary ID)
    _remove_order_mapping(taker_xid);
}

void EventModelExchangeAdapter::_on_order_book_snapshot(const std::vector<L2_DATA_TYPE>& bids_flat, const std::vector<L2_DATA_TYPE>& asks_flat) {
    if (!auto_publish_orderbook_ || !this->bus_) return;

    ModelEvents::OrderBookLevel current_bids_level;
    current_bids_level.reserve(bids_flat.size() / 2);
    for (size_t i = 0; i < bids_flat.size(); i += 2) {
        current_bids_level.emplace_back(static_cast<PriceType>(bids_flat[i]), static_cast<QuantityType>(bids_flat[i+1]));
    }

    ModelEvents::OrderBookLevel current_asks_level;
    current_asks_level.reserve(asks_flat.size() / 2);
    for (size_t i = 0; i < asks_flat.size(); i += 2) {
        current_asks_level.emplace_back(static_cast<PriceType>(asks_flat[i]), static_cast<QuantityType>(asks_flat[i+1]));
    }

    bool bids_changed = !last_published_bids_l2_ || (*last_published_bids_l2_ != current_bids_level);
    bool asks_changed = !last_published_asks_l2_ || (*last_published_asks_l2_ != current_asks_level);
    Timestamp current_time = this->bus_->get_current_time();

    if (bids_changed || asks_changed) {
        last_published_bids_l2_ = current_bids_level;
        last_published_asks_l2_ = current_asks_level;

        auto ob_event = std::make_shared<const ModelEvents::LTwoOrderBookEvent>(
            current_time, symbol_, current_time, current_time, // created_ts, exchange_ts, ingress_ts
            std::move(current_bids_level), std::move(current_asks_level)
        );

        std::string stream_id_str = "l2_stream_" + symbol_;
        publish_wrapper(std::string("LTwoOrderBookEvent.") + symbol_, stream_id_str, ob_event);
        LOG_DEBUG(this->get_logger_source(), "Published updated L2 snapshot for " + symbol_);
    } else {
        LOG_DEBUG(this->get_logger_source(), "L2 snapshot unchanged for " + symbol_ + ", not publishing.");
    }
}

void EventModelExchangeAdapter::_on_acknowledge_trigger_expiration(
    ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty,
    int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int); // This is the original order's CID
    Duration timeout_duration = std::chrono::microseconds(timeout_us_rep);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::AckTriggerExpiredLimitOrderEvent>(
        current_time, symbol_, xid, client_order_id, price, qty, timeout_duration
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id); // Stream of the original order

    // Publish to specific trader who owned the order AND to CancelFairy (convention ID 999)
    publish_wrapper(_format_topic_for_trader("AckTriggerExpiredLimitOrderEvent", trader_id), stream_id_str, ack_event);
    publish_wrapper(_format_topic_for_trader("AckTriggerExpiredLimitOrderEvent", 999), stream_id_str, ack_event); // To CancelFairy

    _remove_order_mapping(xid);
}

void EventModelExchangeAdapter::_on_reject_trigger_expiration(
    ExchangeIDType xid, int trader_id_int, int client_order_id_int) {
    // ExchangeServer::on_reject_trigger_expiration does not provide the original timeout value.
    // ModelEvents::RejectTriggerExpiredLimitOrderEvent expects a Duration timeout_value.
    // We'll use Duration::zero() as a placeholder.
    AgentId trader_id = static_cast<AgentId>(trader_id_int); // Original trader of the order
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int); // Original CID
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto reject_event = std::make_shared<const ModelEvents::RejectTriggerExpiredLimitOrderEvent>(
        current_time, symbol_, xid, Duration::zero() // Placeholder for timeout_value
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);

    // Publish to CancelFairy (convention ID 999)
    publish_wrapper(_format_topic_for_trader("RejectTriggerExpiredLimitOrderEvent", 999), stream_id_str, reject_event);
    // Also inform the original trader if they are subscribed to their own rejections
    publish_wrapper(_format_topic_for_trader("RejectTriggerExpiredLimitOrderEvent", trader_id), stream_id_str, reject_event);
}