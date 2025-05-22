#pragma once

#include "Model.h"
#include "ExchangeServer.h"
#include "Globals.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <optional>
#include <memory>
#include <algorithm>
#include <sstream>
#include <stdexcept>

#ifndef LOG_DEBUG
#define LOG_DEBUG(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::DEBUG, (logger_source), (message))
#define LOG_INFO(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::INFO, (logger_source), (message))
#define LOG_WARNING(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::WARNING, (logger_source), (message))
#define LOG_ERROR(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::ERROR, (logger_source), (message))
#endif

class EventModelExchangeAdapter : public ModelEventProcessor<EventModelExchangeAdapter> {
public:
    using Base = ModelEventProcessor<EventModelExchangeAdapter>;
    using AgentId = EventBusSystem::AgentId;
    using TopicId = EventBusSystem::TopicId;
    using StreamId = EventBusSystem::StreamId;
    using SequenceNumber = EventBusSystem::SequenceNumber;
    using Timestamp = EventBusSystem::Timestamp;

    using SymbolType = ModelEvents::SymbolType;
    using PriceType = ModelEvents::PriceType;
    using QuantityType = ModelEvents::QuantityType;
    using ClientOrderIdType = ModelEvents::ClientOrderIdType;
    using ExchangeOrderIdType = ModelEvents::ExchangeOrderIdType;
    using Duration = ModelEvents::Duration;

    using ExchangeIDType = ::ID_TYPE;
    using ExchangePriceType = ::PRICE_TYPE;
    using ExchangeQuantityType = ::SIZE_TYPE;
    using ExchangeTimeType = ::TIME_TYPE;
    using ExchangeSide = ::SIDE;


    EventModelExchangeAdapter(SymbolType symbol, AgentId agent_id, ModelEventBus<>* bus_ptr_for_init_only = nullptr)
            : Base(agent_id),
              exchange_(),
              symbol_(std::move(symbol)),
              auto_publish_orderbook_(true) {
        _setup_callbacks();
        LOG_INFO(this->get_logger_source(), "EventModelExchangeAdapter constructed for agent " + std::to_string(this->get_id()) + " for symbol: " + symbol_);
    }

    virtual ~EventModelExchangeAdapter() override = default;

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

    EventModelExchangeAdapter(const EventModelExchangeAdapter&) = delete;
    EventModelExchangeAdapter& operator=(const EventModelExchangeAdapter&) = delete;
    EventModelExchangeAdapter(EventModelExchangeAdapter&&) = delete;
    EventModelExchangeAdapter& operator=(EventModelExchangeAdapter&&) = delete;

private:
    ExchangeServer exchange_;
    SymbolType symbol_;
    bool auto_publish_orderbook_;

    std::unordered_map<std::pair<AgentId, ClientOrderIdType>, ExchangeOrderIdType, EventBusSystem::PairHasher> trader_client_to_exchange_map_;
    std::unordered_map<ExchangeOrderIdType, std::pair<AgentId, ClientOrderIdType>> exchange_to_trader_client_map_;
    std::unordered_map<ExchangeOrderIdType, std::string> order_type_map_;

    std::optional<ModelEvents::OrderBookLevel> last_published_bids_l2_;
    std::optional<ModelEvents::OrderBookLevel> last_published_asks_l2_;

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
        exchange_.get_order_book_snapshot();
    }

    void _setup_callbacks();

public:
    void handle_event(const ModelEvents::LimitOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
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
    void _process_limit_order(const ModelEvents::LimitOrderEvent& event, AgentId trader_id);
    void _process_market_order(const ModelEvents::MarketOrderEvent& event, AgentId trader_id);
    void _process_full_cancel_limit_order(const ModelEvents::FullCancelLimitOrderEvent& event, AgentId trader_id);
    void _process_full_cancel_market_order(const ModelEvents::FullCancelMarketOrderEvent& event, AgentId trader_id);
    void _process_partial_cancel_limit_order(const ModelEvents::PartialCancelLimitOrderEvent& event, AgentId trader_id);
    void _process_partial_cancel_market_order(const ModelEvents::PartialCancelMarketOrderEvent& event, AgentId trader_id);
    void _process_bang(const ModelEvents::Bang& event);
    void _process_trigger_expired_limit_order_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event);

    void _on_limit_order_acknowledged(ExchangeIDType xid, ExchangeSide side, ExchangePriceType price, ExchangeQuantityType quantity, ExchangeQuantityType remaining_qty, int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep);
    void _on_market_order_acknowledged(ExchangeSide side, ExchangeQuantityType req_qty, ExchangeQuantityType exec_qty, ExchangeQuantityType unfill_qty, int trader_id_int, int client_order_id_int);
    void _on_partial_cancel_limit(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType cancelled_qty, int trader_id_int_req, int client_order_id_int_req);
    void _on_partial_cancel_limit_reject(ExchangeIDType xid, int trader_id_int_req, int client_order_id_int_req);
    // MODIFIED: Added ExchangeSide ex_side
    void _on_full_cancel_limit(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeSide ex_side, int trader_id_int_req, int client_order_id_int_req);
    void _on_full_cancel_limit_reject(ExchangeIDType xid, int trader_id_int_req, int client_order_id_int_req);

    // MODIFIED: Added ExchangeSide m_side, t_side
    void _on_trade(ExchangeIDType maker_xid, ExchangeSide m_side, ExchangeIDType taker_xid, ExchangeSide t_side, ExchangePriceType price, ExchangeQuantityType qty, bool maker_exhausted, int maker_trader_id_int, int maker_client_id_int, int taker_trader_id_int, int taker_client_id_int);

    // MODIFIED: Added ExchangeSide ex_maker_side
    void _on_maker_partial_fill_limit(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int);
    void _on_taker_partial_fill_limit(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeQuantityType leaves_qty, int trader_id_int, int client_order_id_int);
    // MODIFIED: Added ExchangeSide ex_maker_side
    void _on_maker_full_fill_limit(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int);
    void _on_taker_full_fill_limit(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);

    // MODIFIED: Added ExchangeSide ex_maker_side
    void _on_maker_partial_fill_market(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int);
    void _on_taker_partial_fill_market(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeQuantityType leaves_qty, int trader_id_int, int client_order_id_int);
    // MODIFIED: Added ExchangeSide ex_maker_side
    void _on_maker_full_fill_market(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int);
    void _on_taker_full_fill_market(ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int);

    void _on_order_book_snapshot(const std::vector<L2_DATA_TYPE>& bids, const std::vector<L2_DATA_TYPE>& asks);

    void _on_acknowledge_trigger_expiration(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty, int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep);
    // MODIFIED: Added ExchangeTimeType timeout_us_rep
    void _on_reject_trigger_expiration(ExchangeIDType xid, int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep);

    ModelEvents::Side _to_model_side(ExchangeSide side) {
        if (side == ExchangeSide::NONE) {
            LOG_WARNING(this->get_logger_source(), "Converting ExchangeSide::NONE to ModelEvents::Side::BUY (defaulting). This might indicate an issue.");
            return ModelEvents::Side::BUY; // Or throw, or handle as error
        }
        return (side == ExchangeSide::BID) ? ModelEvents::Side::BUY : ModelEvents::Side::SELL;
    }
    ExchangeSide _to_exchange_side(ModelEvents::Side side) {
        return (side == ModelEvents::Side::BUY) ? ExchangeSide::BID : ExchangeSide::ASK;
    }
};


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
    // MODIFIED: Added ExchangeSide s
    exchange_.on_full_cancel_limit = [this](ExchangeIDType xid, ExchangePriceType p, ExchangeQuantityType q, ExchangeSide s, int tid_req, int cid_req) {
        this->_on_full_cancel_limit(xid, p, q, s, tid_req, cid_req);
    };
    exchange_.on_full_cancel_limit_reject = [this](ExchangeIDType xid, int tid_req, int cid_req) {
        this->_on_full_cancel_limit_reject(xid, tid_req, cid_req);
    };
    // MODIFIED: Added ExchangeSide m_side, t_side
    exchange_.on_trade = [this](ExchangeIDType mxid, ExchangeSide m_side, ExchangeIDType txid, ExchangeSide t_side, ExchangePriceType p, ExchangeQuantityType q, bool mex, int mtid, int mcid, int ttid, int tcid) {
        this->_on_trade(mxid, m_side, txid, t_side, p, q, mex, mtid, mcid, ttid, tcid);
    };
    // MODIFIED: Added ExchangeSide maker_s
    exchange_.on_maker_partial_fill_limit = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, ExchangeSide maker_s, int tid, int cid) {
        this->_on_maker_partial_fill_limit(mxid, p, q, maker_s, tid, cid);
    };
    exchange_.on_taker_partial_fill_limit = [this](ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, ExchangeQuantityType lq, int tid, int cid) {
        this->_on_taker_partial_fill_limit(txid, p, q, lq, tid, cid);
    };
    // MODIFIED: Added ExchangeSide maker_s
    exchange_.on_maker_full_fill_limit = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, ExchangeSide maker_s, int tid, int cid) {
        this->_on_maker_full_fill_limit(mxid, p, q, maker_s, tid, cid);
    };
    exchange_.on_taker_full_fill_limit = [this](ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, int tid, int cid) {
        this->_on_taker_full_fill_limit(txid, p, q, tid, cid);
    };
    // MODIFIED: Added ExchangeSide maker_s
    exchange_.on_maker_partial_fill_market = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, ExchangeSide maker_s, int tid, int cid) {
        this->_on_maker_partial_fill_market(mxid, p, q, maker_s, tid, cid);
    };
    exchange_.on_taker_partial_fill_market = [this](ExchangeIDType txid, ExchangePriceType p, ExchangeQuantityType q, ExchangeQuantityType lq, int tid, int cid) {
        this->_on_taker_partial_fill_market(txid, p, q, lq, tid, cid);
    };
    // MODIFIED: Added ExchangeSide maker_s
    exchange_.on_maker_full_fill_market = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q, ExchangeSide maker_s, int tid, int cid) {
        this->_on_maker_full_fill_market(mxid, p, q, maker_s, tid, cid);
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
    // MODIFIED: Added ExchangeTimeType tus
    exchange_.on_reject_trigger_expiration = [this](ExchangeIDType xid, int tid, int cid, ExchangeTimeType tus) {
        this->_on_reject_trigger_expiration(xid, tid, cid, tus);
    };
}


void EventModelExchangeAdapter::_process_limit_order(const ModelEvents::LimitOrderEvent& event, AgentId trader_id) {
    ExchangeSide ex_side = _to_exchange_side(event.side);
    ExchangeTimeType timeout_us_rep = std::chrono::duration_cast<std::chrono::microseconds>(event.timeout).count();

    ExchangeIDType xid = exchange_.place_limit_order(
            ex_side, event.price, event.quantity, timeout_us_rep,
            static_cast<int>(trader_id), static_cast<int>(event.client_order_id)
    );

    if (xid != ID_DEFAULT) { // If it rests (even partially)
        _register_order_mapping(trader_id, event.client_order_id, xid, "limit");
    }
    _publish_orderbook_snapshot_if_changed();
}

void EventModelExchangeAdapter::_process_market_order(const ModelEvents::MarketOrderEvent& event, AgentId trader_id) {
    ExchangeSide ex_side = _to_exchange_side(event.side);

    ExchangeIDType temp_xid = exchange_.place_market_order(
            ex_side, event.quantity,
            static_cast<int>(trader_id), static_cast<int>(event.client_order_id)
    );
    // temp_xid is the ID used by ExchangeServer for this market order interaction
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
        LOG_WARNING(this->get_logger_source(), "FullCancelLimitOrder: Target XID " + std::to_string(xid) + " is not a limit order or mapping missing.");
        auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
                current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    bool success = exchange_.cancel_order(xid, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
    if (success) {
        _publish_orderbook_snapshot_if_changed();
    }
    // ACKs/REJs handled by callbacks.
}

void EventModelExchangeAdapter::_process_full_cancel_market_order(const ModelEvents::FullCancelMarketOrderEvent& event, AgentId trader_id) {
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, event.target_order_id);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    if (xid_opt) {
        ExchangeOrderIdType xid = *xid_opt;
        auto order_type_it = order_type_map_.find(xid);
        if (order_type_it != order_type_map_.end() && order_type_it->second == "market") {
            // Attempt cancel, though ExchangeServer's cancel_order is primarily for limit orders.
            // Market orders are usually too fast. Expect rejection callback.
            exchange_.cancel_order(xid, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
            // Callback _on_full_cancel_limit_reject will be called by ExchangeServer if order not found or cannot be cancelled.
            // No snapshot publish here, as market order cancel attempts are typically rejected.
            return;
        } else {
            LOG_WARNING(this->get_logger_source(), "FullCancelMarketOrder: Target XID " + std::to_string(xid) + " is not a market order or mapping missing.");
        }
    } else {
        LOG_WARNING(this->get_logger_source(), "FullCancelMarketOrder: XID not found for Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));
    }

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
        LOG_WARNING(this->get_logger_source(), "PartialCancelLimitOrder: Target XID " + std::to_string(xid) + " is not a limit order or mapping missing.");
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
    if (new_qty <= 0) {
        success = exchange_.cancel_order(xid, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
    } else {
        success = exchange_.modify_order_quantity(xid, new_qty, static_cast<int>(trader_id), static_cast<int>(event.client_order_id));
    }

    if (success) {
        _publish_orderbook_snapshot_if_changed();
    }
    // ACKs/REJs handled by callbacks.
}

void EventModelExchangeAdapter::_process_partial_cancel_market_order(const ModelEvents::PartialCancelMarketOrderEvent& event, AgentId trader_id) {
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

    last_published_bids_l2_ = std::nullopt;
    last_published_asks_l2_ = std::nullopt;

    exchange_.flush();

    Timestamp current_time_for_bang = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    publish_wrapper("Bang", std::make_shared<const ModelEvents::Bang>(current_time_for_bang));
    _publish_orderbook_snapshot_if_changed();
}

void EventModelExchangeAdapter::_process_trigger_expired_limit_order_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event) {
    LOG_DEBUG(this->get_logger_source(), "Processing TriggerExpiredLimitOrderEvent for XID: " + std::to_string(event.target_exchange_order_id));

    ExchangeIDType xid_to_cancel = event.target_exchange_order_id;
    ExchangeTimeType timeout_us_rep = std::chrono::duration_cast<std::chrono::microseconds>(event.timeout_value).count();

    bool call_succeeded = exchange_.cancel_expired_order(xid_to_cancel, timeout_us_rep);

    if (call_succeeded) {
        _publish_orderbook_snapshot_if_changed();
    }
    // If call_succeeded is false, ExchangeServer::cancel_expired_order would have already called
    // the on_reject_trigger_expiration callback.
}


void EventModelExchangeAdapter::_on_limit_order_acknowledged(
        ExchangeIDType xid, ExchangeSide ex_side, ExchangePriceType price,
        ExchangeQuantityType quantity, ExchangeQuantityType remaining_qty,
        int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int); // This is the original placer's ID
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    ModelEvents::Side model_side = _to_model_side(ex_side);
    Duration timeout_duration = std::chrono::microseconds(timeout_us_rep);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::LimitOrderAckEvent>(
            current_time, xid, client_order_id, model_side, price, quantity, symbol_, timeout_duration,
            trader_id // <<<< PASS ORIGINAL TRADER ID HERE >>>>
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("LimitOrderAckEvent", trader_id), stream_id_str, ack_event);
    publish_wrapper("LimitOrderAckEvent", stream_id_str, ack_event); // For CancelFairy

    if (xid != ID_DEFAULT && remaining_qty == 0) {
        _remove_order_mapping(xid);
    }
}

void EventModelExchangeAdapter::_on_market_order_acknowledged(
        ExchangeSide ex_side, ExchangeQuantityType req_qty, ExchangeQuantityType exec_qty,
        ExchangeQuantityType unfill_qty, int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    ModelEvents::Side model_side = _to_model_side(ex_side);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, client_order_id);
    ExchangeOrderIdType xid_for_ack = xid_opt.value_or(ID_DEFAULT);

    auto ack_event = std::make_shared<const ModelEvents::MarketOrderAckEvent>(
            current_time, xid_for_ack, client_order_id, model_side, req_qty, symbol_
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("MarketOrderAckEvent", trader_id), stream_id_str, ack_event);
    // Market order mapping (using xid_for_ack) removed by fill handlers or if rejected/expired.
}

void EventModelExchangeAdapter::_on_partial_cancel_limit(
        ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType cancelled_qty,
        int trader_id_int_req, int client_order_id_int_req) {

    AgentId req_trader_id = static_cast<AgentId>(trader_id_int_req);
    ClientOrderIdType req_client_order_id = static_cast<ClientOrderIdType>(client_order_id_int_req);

    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if (!original_ids_opt) {
        LOG_ERROR(this->get_logger_source(), "PartialCancelLimit ACK for unknown XID: " + std::to_string(xid) + ". Rejecting cancel request CID: " + std::to_string(req_client_order_id));
        // Send reject for the cancel request
        Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
                current_time, req_client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", req_trader_id),
                        _format_stream_id(req_trader_id, req_client_order_id), reject_event);
        return;
    }
    AgentId original_trader_id = original_ids_opt->first;
    ClientOrderIdType original_client_order_id = original_ids_opt->second;

    std::optional<std::tuple<ExchangePriceType, ExchangeQuantityType, ExchangeSide>> details_opt = exchange_.get_order_details(xid);
    ExchangeQuantityType remaining_qty_after_cancel = 0;
    ExchangeSide ex_side_original_order = ExchangeSide::NONE;
    ExchangeQuantityType original_total_qty_before_cancel = cancelled_qty; // Default if details gone

    if (details_opt) {
        remaining_qty_after_cancel = std::get<1>(*details_opt);
        ex_side_original_order = std::get<2>(*details_opt);
        original_total_qty_before_cancel = remaining_qty_after_cancel + cancelled_qty;
    } else {
        LOG_WARNING(this->get_logger_source(), "PartialCancelLimit ACK for XID " + std::to_string(xid) + " but current details not found. Order might be fully gone. Estimating original side/qty.");
        // If details are gone, it implies remaining_qty_after_cancel is 0.
        // Try to get side from order_type_map if it's a limit order
        auto type_it = order_type_map_.find(xid);
        if (type_it != order_type_map_.end() && type_it->second == "limit") {
            // This is tricky as we don't store original side in order_type_map.
            // We could get it from _get_trader_and_client_ids map's order object if we stored it.
            // For now, we have to rely on details_opt or ExchangeServer providing it.
            // If ExchangeServer's on_partial_cancel_limit callback included the original order's side, this would be robust.
            // Let's assume for now the side from details_opt is what we have, or we use a default.
        }
    }
    ModelEvents::Side model_side_original_order = _to_model_side(ex_side_original_order);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::PartialCancelLimitAckEvent>(
            current_time,
            xid, // XID of the original order
            req_client_order_id,
            model_side_original_order,
            original_client_order_id,
            original_total_qty_before_cancel,
            symbol_,
            cancelled_qty,
            remaining_qty_after_cancel
    );

    std::string stream_id_str = _format_stream_id(original_trader_id, original_client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialCancelLimitAckEvent", req_trader_id), stream_id_str, ack_event);

    if (remaining_qty_after_cancel == 0 && xid != ID_DEFAULT) {
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

    std::string stream_id_str;
    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if(original_ids_opt) {
        stream_id_str = _format_stream_id(original_ids_opt->first, original_ids_opt->second);
    } else {
        stream_id_str = _format_stream_id(req_trader_id, req_client_order_id);
    }

    publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", req_trader_id), stream_id_str, reject_event);
}

// MODIFIED: Added ExchangeSide ex_side parameter
void EventModelExchangeAdapter::_on_full_cancel_limit(
        ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty_cancelled,
        ExchangeSide ex_side, int trader_id_int_req, int client_order_id_int_req) {

    AgentId req_trader_id = static_cast<AgentId>(trader_id_int_req);
    ClientOrderIdType req_client_order_id = static_cast<ClientOrderIdType>(client_order_id_int_req);

    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if (!original_ids_opt) {
        LOG_ERROR(this->get_logger_source(), "FullCancelLimit ACK for unknown XID: " + std::to_string(xid) + ". Rejecting cancel request CID: " + std::to_string(req_client_order_id));
        Timestamp current_time_for_reject = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
        auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
                current_time_for_reject, req_client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", req_trader_id),
                        _format_stream_id(req_trader_id, req_client_order_id), reject_event);
        return;
    }
    AgentId original_trader_id = original_ids_opt->first;
    ClientOrderIdType original_client_order_id = original_ids_opt->second;

    ModelEvents::Side model_side = _to_model_side(ex_side);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::FullCancelLimitOrderAckEvent>(
            current_time, xid, req_client_order_id, model_side, original_client_order_id, qty_cancelled, symbol_
    );

    std::string stream_id_str = _format_stream_id(original_trader_id, original_client_order_id);
    publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderAckEvent", req_trader_id), stream_id_str, ack_event);
    publish_wrapper("FullCancelLimitOrderAckEvent", stream_id_str, ack_event);

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

// MODIFIED: Added ExchangeSide maker_ex_side, taker_ex_side parameters
void EventModelExchangeAdapter::_on_trade(
        ExchangeIDType maker_xid, ExchangeSide maker_ex_side, ExchangeIDType taker_xid, ExchangeSide taker_ex_side,
        ExchangePriceType price, ExchangeQuantityType qty, bool maker_exhausted,
        int maker_trader_id_int, int maker_client_id_int,
        int taker_trader_id_int, int taker_client_id_int) {

    AgentId maker_trader_id = static_cast<AgentId>(maker_trader_id_int);
    ClientOrderIdType maker_client_id = static_cast<ClientOrderIdType>(maker_client_id_int);
    AgentId taker_trader_id = static_cast<AgentId>(taker_trader_id_int);
    ClientOrderIdType taker_client_id = static_cast<ClientOrderIdType>(taker_client_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side maker_model_side = _to_model_side(maker_ex_side);

    auto trade_event = std::make_shared<const ModelEvents::TradeEvent>(
            current_time, symbol_, maker_client_id, taker_client_id, maker_xid, taker_xid,
            price, qty, maker_model_side, maker_exhausted
    );

    std::string maker_stream_id_str = _format_stream_id(maker_trader_id, maker_client_id);
    std::string taker_stream_id_str = _format_stream_id(taker_trader_id, taker_client_id);

    std::string trade_topic = std::string("TradeEvent.") + symbol_;
    publish_wrapper(trade_topic, maker_stream_id_str, trade_event);
    if (maker_stream_id_str != taker_stream_id_str) {
        publish_wrapper(trade_topic, taker_stream_id_str, trade_event);
    }
}

// MODIFIED: Added ExchangeSide ex_maker_side parameter
void EventModelExchangeAdapter::_on_maker_partial_fill_limit(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_time,
        ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ExchangeQuantityType leaves_qty = 0;
    ModelEvents::Side model_side = _to_model_side(ex_maker_side);
    // For cumulative_qty and average_price, a more sophisticated tracking per XID would be needed.
    // For simplicity, using this fill's details.
    ExchangeQuantityType cumulative_qty_filled_so_far = qty_filled_this_time;
    PriceType avg_price_so_far = price;

    auto details_opt = exchange_.get_order_details(maker_xid);
    if (details_opt) {
        leaves_qty = std::get<1>(*details_opt);
        // We could verify ex_maker_side against std::get<2>(*details_opt) here
    } else {
        LOG_WARNING(this->get_logger_source(), "MakerPartialFillLimit: Could not get current details for XID " + std::to_string(maker_xid) + " to find leaves_qty. Using 0.");
    }

    auto fill_event = std::make_shared<const ModelEvents::PartialFillLimitOrderEvent>(
            current_time, maker_xid, client_order_id, model_side, price, qty_filled_this_time, current_time, symbol_, true,
            leaves_qty, cumulative_qty_filled_so_far, static_cast<double>(avg_price_so_far)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
}

void EventModelExchangeAdapter::_on_taker_partial_fill_limit(
        ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_time, ExchangeQuantityType leaves_qty_on_taker_order,
        int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY; // Default
    ExchangeQuantityType cumulative_qty_filled_so_far = qty_filled_this_time;
    PriceType avg_price_so_far = price;

    if (exchange_.active_taker_side_.has_value()) {
        model_side = _to_model_side(exchange_.active_taker_side_.value());
    } else if (taker_xid != ID_DEFAULT) { // If it was a resting limit order that became a taker
        auto details_opt = exchange_.get_order_details(taker_xid);
        if (details_opt) model_side = _to_model_side(std::get<2>(*details_opt));
        else LOG_WARNING(this->get_logger_source(), "TakerPartialFillLimit: Could not determine side for resting taker XID " + std::to_string(taker_xid));
    } else {
        LOG_WARNING(this->get_logger_source(), "TakerPartialFillLimit: Could not determine side for taker (ID_DEFAULT, active_taker_side_ not set).");
    }

    auto fill_event = std::make_shared<const ModelEvents::PartialFillLimitOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, qty_filled_this_time, current_time, symbol_, false,
            leaves_qty_on_taker_order, cumulative_qty_filled_so_far, static_cast<double>(avg_price_so_far)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
}

// MODIFIED: Added ExchangeSide ex_maker_side parameter
void EventModelExchangeAdapter::_on_maker_full_fill_limit(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled,
        ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = _to_model_side(ex_maker_side);

    auto fill_event = std::make_shared<const ModelEvents::FullFillLimitOrderEvent>(
            current_time, maker_xid, client_order_id, model_side, price, total_qty_filled, current_time, symbol_, true,
            static_cast<double>(price)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
    publish_wrapper("FullFillLimitOrderEvent", stream_id_str, fill_event);

    _remove_order_mapping(maker_xid);
}

void EventModelExchangeAdapter::_on_taker_full_fill_limit(
        ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled,
        int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY;
    if (exchange_.active_taker_side_.has_value()) {
        model_side = _to_model_side(exchange_.active_taker_side_.value());
    } else if (taker_xid != ID_DEFAULT) {
        auto details_opt = exchange_.get_order_details(taker_xid);
        if (details_opt) model_side = _to_model_side(std::get<2>(*details_opt));
        else LOG_WARNING(this->get_logger_source(), "TakerFullFillLimit: Could not determine side for resting taker XID " + std::to_string(taker_xid));
    } else {
        LOG_WARNING(this->get_logger_source(), "TakerFullFillLimit: Could not determine side for taker (ID_DEFAULT, active_taker_side_ not set).");
    }

    auto fill_event = std::make_shared<const ModelEvents::FullFillLimitOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, total_qty_filled, current_time, symbol_, false,
            static_cast<double>(price)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
    if (taker_xid != ID_DEFAULT) { // Only publish general if it was a placed order that became taker
        publish_wrapper("FullFillLimitOrderEvent", stream_id_str, fill_event);
        _remove_order_mapping(taker_xid);
    }
    // If taker_xid was a temporary ID from market_order_id_counter_, it's removed by place_limit_order/place_market_order.
}

// MODIFIED: Added ExchangeSide ex_maker_side parameter
void EventModelExchangeAdapter::_on_maker_partial_fill_market(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_time,
        ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int) {
    _on_maker_partial_fill_limit(maker_xid, price, qty_filled_this_time, ex_maker_side, trader_id_int, client_order_id_int);
}

void EventModelExchangeAdapter::_on_taker_partial_fill_market(
        ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_time, ExchangeQuantityType leaves_qty_on_taker_order,
        int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY;
    if (exchange_.active_taker_side_.has_value()) {
        model_side = _to_model_side(exchange_.active_taker_side_.value());
    } else {
        LOG_WARNING(this->get_logger_source(), "TakerPartialFillMarket: Could not determine side for taker (active_taker_side_ not set). XID: " + std::to_string(taker_xid));
    }

    auto fill_event = std::make_shared<const ModelEvents::PartialFillMarketOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, qty_filled_this_time, current_time, symbol_, false,
            leaves_qty_on_taker_order, qty_filled_this_time, static_cast<double>(price)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillMarketOrderEvent", trader_id), stream_id_str, fill_event);
}

// MODIFIED: Added ExchangeSide ex_maker_side parameter
void EventModelExchangeAdapter::_on_maker_full_fill_market(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled,
        ExchangeSide ex_maker_side, int trader_id_int, int client_order_id_int) {
    _on_maker_full_fill_limit(maker_xid, price, total_qty_filled, ex_maker_side, trader_id_int, client_order_id_int);
}

void EventModelExchangeAdapter::_on_taker_full_fill_market(
        ExchangeIDType taker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled,
        int trader_id_int, int client_order_id_int) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side model_side = ModelEvents::Side::BUY;
    if (exchange_.active_taker_side_.has_value()) {
        model_side = _to_model_side(exchange_.active_taker_side_.value());
    } else {
        LOG_WARNING(this->get_logger_source(), "TakerFullFillMarket: Could not determine side for taker (active_taker_side_ not set). XID: " + std::to_string(taker_xid));
    }

    auto fill_event = std::make_shared<const ModelEvents::FullFillMarketOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, total_qty_filled, current_time, symbol_, false,
            static_cast<double>(price)
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillMarketOrderEvent", trader_id), stream_id_str, fill_event);

    // The temporary mapping for this market order (taker_xid) should be removed by ExchangeServer itself after processing,
    // or ensure _remove_order_mapping is robust if called here.
    // ExchangeServer::place_market_order now removes metadata for the market_order_id.
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
                current_time, symbol_, current_time, current_time,
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
        ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty_expired,
        int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep) {

    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Duration timeout_duration = std::chrono::microseconds(timeout_us_rep);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::AckTriggerExpiredLimitOrderEvent>(
            current_time, symbol_, xid, client_order_id, price, qty_expired, timeout_duration
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);

    // Assuming CANCEL_FAIRY_ID is defined (e.g., in TradingSimulation.h or a constants file)
    constexpr EventBusSystem::AgentId CANCEL_FAIRY_AGENT_ID = 999;
    publish_wrapper(_format_topic_for_trader("AckTriggerExpiredLimitOrderEvent", CANCEL_FAIRY_AGENT_ID), stream_id_str, ack_event);
    if (trader_id != CANCEL_FAIRY_AGENT_ID) { // Don't double send if CancelFairy was original trader (unlikely)
        publish_wrapper(_format_topic_for_trader("AckTriggerExpiredLimitOrderEvent", trader_id), stream_id_str, ack_event);
    }
    _remove_order_mapping(xid);
}

// MODIFIED: Added ExchangeTimeType timeout_us_rep parameter
void EventModelExchangeAdapter::_on_reject_trigger_expiration(
        ExchangeIDType xid, int trader_id_int, int client_order_id_int, ExchangeTimeType timeout_us_rep) {
    AgentId trader_id = static_cast<AgentId>(trader_id_int);
    ClientOrderIdType client_order_id = static_cast<ClientOrderIdType>(client_order_id_int);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    Duration original_timeout_duration = std::chrono::microseconds(timeout_us_rep);

    auto reject_event = std::make_shared<const ModelEvents::RejectTriggerExpiredLimitOrderEvent>(
            current_time, symbol_, xid, original_timeout_duration
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);

    constexpr EventBusSystem::AgentId CANCEL_FAIRY_AGENT_ID = 999;
    publish_wrapper(_format_topic_for_trader("RejectTriggerExpiredLimitOrderEvent", CANCEL_FAIRY_AGENT_ID), stream_id_str, reject_event);
    if (trader_id != CANCEL_FAIRY_AGENT_ID) {
        publish_wrapper(_format_topic_for_trader("RejectTriggerExpiredLimitOrderEvent", trader_id), stream_id_str, reject_event);
    }
    // Do not remove mapping here; the order might still be active if expiration was rejected.
}