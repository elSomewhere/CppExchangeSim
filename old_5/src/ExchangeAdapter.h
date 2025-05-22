// file: src/ExchangeAdapter.h
#pragma once

#include "Model.h"
#include "ExchangeServer.h"
#include "Globals.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <algorithm>
#include <sstream>
#include <iomanip> // For std::fixed and std::setprecision in logging average price


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
    using AveragePriceType = ModelEvents::AveragePriceType;


    using ExchangeIDType = ::ID_TYPE; // From Globals.h, typically uint64_t
    using ExchangePriceType = ::PRICE_TYPE;
    using ExchangeQuantityType = ::SIZE_TYPE;
    using ExchangeTimeType = ::TIME_TYPE;
    using ExchangeSide = ::SIDE;

    // Enum for internal order type tracking
    enum class MappedOrderType {
        UNKNOWN,
        LIMIT,
        MARKET
    };

    // Structure to track cumulative fill state for partial fills
    struct PartialFillState {
        QuantityType cumulative_qty_filled = 0;
        double cumulative_value_filled = 0.0; // Sum of (price * quantity) for each fill segment
                                              // Using double for value to maintain precision for average price calculation
    };

    EventModelExchangeAdapter(SymbolType symbol)
            : Base(),
              exchange_(), // ExchangeServer will be default constructed
              symbol_(std::move(symbol)),
              auto_publish_orderbook_(true) {
        _setup_callbacks();
        LogMessage(LogLevel::INFO, this->get_logger_source(), "EventModelExchangeAdapter constructed for symbol: " + symbol_ + ". Agent ID will be set upon registration.");
    }

    virtual ~EventModelExchangeAdapter() override = default;

    void setup_subscriptions() {
        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "EventModelExchangeAdapter cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
            return;
        }
        LogMessage(LogLevel::INFO, this->get_logger_source(), "EventModelExchangeAdapter agent " + std::to_string(this->get_id()) + " setting up subscriptions for symbol: " + symbol_);
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
    std::unordered_map<ExchangeOrderIdType, MappedOrderType> order_type_map_;
    std::unordered_map<ExchangeOrderIdType, AgentId> expiration_trigger_sender_map_;
    std::unordered_map<ExchangeOrderIdType, PartialFillState> partial_fill_tracker_;


    std::optional<ModelEvents::OrderBookLevel> last_published_bids_l2_;
    std::optional<ModelEvents::OrderBookLevel> last_published_asks_l2_;

    std::string _mapped_order_type_to_string(MappedOrderType type) const {
        switch (type) {
            case MappedOrderType::LIMIT: return "limit";
            case MappedOrderType::MARKET: return "market";
            case MappedOrderType::UNKNOWN: return "unknown";
            default: return "undefined_mapped_type";
        }
    }

    template <typename E>
    void publish_wrapper(const std::string& topic_str, const std::string& stream_id_str, const std::shared_ptr<const E>& event_ptr) {
        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "EventBus not set, cannot publish event for topic: " + topic_str);
            return;
        }
        if (!event_ptr) {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted to publish a null event_ptr. Topic: " + topic_str);
            return;
        }
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Publishing to topic '" + topic_str + "' on stream '" + stream_id_str + "': " + event_ptr->to_string());
        this->publish(topic_str, event_ptr, stream_id_str);
    }

    template <typename E>
    void publish_wrapper(const std::string& topic_str, const std::shared_ptr<const E>& event_ptr) {
        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "EventBus not set, cannot publish event for topic: " + topic_str);
            return;
        }
        if (!event_ptr) {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted to publish a null event_ptr. Topic: " + topic_str);
            return;
        }
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Publishing to topic '" + topic_str + "': " + event_ptr->to_string());
        this->publish(topic_str, event_ptr);
    }

    void _register_order_mapping(AgentId trader_id, ClientOrderIdType client_order_id,
                                 ExchangeOrderIdType exchange_order_id, MappedOrderType order_type) {
        std::pair<AgentId, ClientOrderIdType> trader_client_key = {trader_id, client_order_id};
        trader_client_to_exchange_map_[trader_client_key] = exchange_order_id;
        exchange_to_trader_client_map_[exchange_order_id] = trader_client_key;
        order_type_map_[exchange_order_id] = order_type;
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Registered mapping: Trader " + std::to_string(trader_id) +
                                             ", CID " + std::to_string(client_order_id) + " -> XID " + std::to_string(exchange_order_id) +
                                             " (Type: " + _mapped_order_type_to_string(order_type) + ")");
    }

    void _remove_order_mapping(ExchangeOrderIdType exchange_order_id) {
        auto it_xid_map = exchange_to_trader_client_map_.find(exchange_order_id);
        if (it_xid_map != exchange_to_trader_client_map_.end()) {
            std::pair<AgentId, ClientOrderIdType> trader_client_key = it_xid_map->second;

            trader_client_to_exchange_map_.erase(trader_client_key);
            exchange_to_trader_client_map_.erase(it_xid_map);
            order_type_map_.erase(exchange_order_id);
            partial_fill_tracker_.erase(exchange_order_id); // Also remove from partial fill tracker
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Removed mapping and partial fill state for XID " + std::to_string(exchange_order_id));
        } else {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "Attempted to remove mapping for non-existent XID " + std::to_string(exchange_order_id) + ". Partial fill state also not removed if it existed under this XID.");
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
        // This call will trigger the _on_order_book_snapshot callback if the book has changed
        // (or even if not, the callback will decide based on its internal state)
        exchange_.get_order_book_snapshot();
    }

    void _setup_callbacks();

public:
    // Event handlers from ModelEventProcessor
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
    void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event, TopicId, AgentId sender_id, Timestamp, StreamId, SequenceNumber) {
        if (event.symbol != symbol_) return;
        _process_trigger_expired_limit_order_event(event, sender_id);
    }

    // Empty handlers for events this adapter publishes but does not consume
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
    void _process_trigger_expired_limit_order_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event, AgentId trigger_sender_id);

    // Callbacks from ExchangeServer
    void _on_limit_order_acknowledged(ExchangeIDType xid, ExchangeSide side, ExchangePriceType price, ExchangeQuantityType quantity, ExchangeQuantityType remaining_qty, AgentId trader_id, ClientOrderIdType client_order_id, ExchangeTimeType timeout_us_rep);
    void _on_market_order_acknowledged(ExchangeSide side, ExchangeQuantityType req_qty, ExchangeQuantityType exec_qty, ExchangeQuantityType unfill_qty, AgentId trader_id, ClientOrderIdType client_order_id);
    void _on_partial_cancel_limit(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType cancelled_qty, AgentId trader_id_req, ClientOrderIdType client_order_id_req);
    void _on_partial_cancel_limit_reject(ExchangeIDType xid, AgentId trader_id_req, ClientOrderIdType client_order_id_req);
    void _on_full_cancel_limit(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty, ExchangeSide ex_side, AgentId trader_id_req, ClientOrderIdType client_order_id_req);
    void _on_full_cancel_limit_reject(ExchangeIDType xid, AgentId trader_id_req, ClientOrderIdType client_order_id_req);
    void _on_trade(ExchangeIDType maker_xid, ExchangeSide m_side, ExchangeIDType taker_xid, ExchangeSide t_side, ExchangePriceType price, ExchangeQuantityType qty, bool maker_exhausted, AgentId maker_trader_id, ClientOrderIdType maker_client_id, AgentId taker_trader_id, ClientOrderIdType taker_client_id);

    // Maker Fill Callbacks (Limit Order as Maker)
    void _on_maker_partial_fill_limit(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment, ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id);
    void _on_maker_full_fill_limit(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_maker, ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id);

    // Taker Fill Callbacks (Limit Order as Taker)
    void _on_taker_partial_fill_limit(ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment, ExchangeQuantityType leaves_qty_on_taker_order, AgentId trader_id, ClientOrderIdType client_order_id);
    void _on_taker_full_fill_limit(ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_taker, AgentId trader_id, ClientOrderIdType client_order_id);

    // Maker Fill Callbacks (Market Order as Maker - typically not applicable, but for completeness if a market order somehow rests)
    void _on_maker_partial_fill_market(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment, ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id);
    void _on_maker_full_fill_market(ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_maker, ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id);

    // Taker Fill Callbacks (Market Order as Taker)
    void _on_taker_partial_fill_market(ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment, ExchangeQuantityType leaves_qty_on_taker_order, AgentId trader_id, ClientOrderIdType client_order_id);
    void _on_taker_full_fill_market(ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_taker, AgentId trader_id, ClientOrderIdType client_order_id);

    void _on_order_book_snapshot(const std::vector<L2_DATA_TYPE>& bids, const std::vector<L2_DATA_TYPE>& asks);
    void _on_acknowledge_trigger_expiration(ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty_expired, AgentId original_placer_trader_id, ClientOrderIdType original_placer_client_order_id, ExchangeTimeType timeout_us_rep);
    void _on_reject_trigger_expiration(ExchangeIDType xid, AgentId original_placer_trader_id, ClientOrderIdType original_placer_client_order_id, ExchangeTimeType timeout_us_rep);

    ModelEvents::Side _to_model_side(ExchangeSide side) {
        if (side == ExchangeSide::NONE) { // Should ideally not happen for order sides
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "Converting ExchangeSide::NONE to ModelEvents::Side::BUY (defaulting). This might indicate an issue in ExchangeServer logic.");
            return ModelEvents::Side::BUY;
        }
        return (side == ExchangeSide::BID) ? ModelEvents::Side::BUY : ModelEvents::Side::SELL;
    }
    ExchangeSide _to_exchange_side(ModelEvents::Side side) {
        return (side == ModelEvents::Side::BUY) ? ExchangeSide::BID : ExchangeSide::ASK;
    }
};


void EventModelExchangeAdapter::_setup_callbacks() {
    exchange_.on_limit_order_acknowledged = [this](ExchangeIDType xid, ExchangeSide s, ExchangePriceType p, ExchangeQuantityType q, ExchangeQuantityType rq, AgentId tid, ClientOrderIdType cid, ExchangeTimeType tus) {
        this->_on_limit_order_acknowledged(xid, s, p, q, rq, tid, cid, tus);
    };
    exchange_.on_market_order_acknowledged = [this](ExchangeSide s, ExchangeQuantityType rq, ExchangeQuantityType eq, ExchangeQuantityType uq, AgentId tid, ClientOrderIdType cid) {
        this->_on_market_order_acknowledged(s, rq, eq, uq, tid, cid);
    };
    exchange_.on_partial_cancel_limit = [this](ExchangeIDType xid, ExchangePriceType p, ExchangeQuantityType cq, AgentId tid_req, ClientOrderIdType cid_req) {
        this->_on_partial_cancel_limit(xid, p, cq, tid_req, cid_req);
    };
    exchange_.on_partial_cancel_limit_reject = [this](ExchangeIDType xid, AgentId tid_req, ClientOrderIdType cid_req) {
        this->_on_partial_cancel_limit_reject(xid, tid_req, cid_req);
    };
    exchange_.on_full_cancel_limit = [this](ExchangeIDType xid, ExchangePriceType p, ExchangeQuantityType q, ExchangeSide s, AgentId tid_req, ClientOrderIdType cid_req) {
        this->_on_full_cancel_limit(xid, p, q, s, tid_req, cid_req);
    };
    exchange_.on_full_cancel_limit_reject = [this](ExchangeIDType xid, AgentId tid_req, ClientOrderIdType cid_req) {
        this->_on_full_cancel_limit_reject(xid, tid_req, cid_req);
    };
    exchange_.on_trade = [this](ExchangeIDType mxid, ExchangeSide m_side, ExchangeIDType txid, ExchangeSide t_side, ExchangePriceType p, ExchangeQuantityType q, bool mex, AgentId mtid, ClientOrderIdType mcid, AgentId ttid, ClientOrderIdType tcid) {
        this->_on_trade(mxid, m_side, txid, t_side, p, q, mex, mtid, mcid, ttid, tcid);
    };

    // Maker fills
    exchange_.on_maker_partial_fill_limit = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q_seg, ExchangeSide maker_s, AgentId tid, ClientOrderIdType cid) {
        this->_on_maker_partial_fill_limit(mxid, p, q_seg, maker_s, tid, cid);
    };
    exchange_.on_maker_full_fill_limit = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType total_q, ExchangeSide maker_s, AgentId tid, ClientOrderIdType cid) {
        this->_on_maker_full_fill_limit(mxid, p, total_q, maker_s, tid, cid);
    };
    exchange_.on_maker_partial_fill_market = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType q_seg, ExchangeSide maker_s, AgentId tid, ClientOrderIdType cid) {
        this->_on_maker_partial_fill_market(mxid, p, q_seg, maker_s, tid, cid);
    };
    exchange_.on_maker_full_fill_market = [this](ExchangeIDType mxid, ExchangePriceType p, ExchangeQuantityType total_q, ExchangeSide maker_s, AgentId tid, ClientOrderIdType cid) {
        this->_on_maker_full_fill_market(mxid, p, total_q, maker_s, tid, cid);
    };

    // Taker fills
    exchange_.on_taker_partial_fill_limit = [this](ExchangeIDType txid, ExchangeSide taker_s, ExchangePriceType p, ExchangeQuantityType q_seg, ExchangeQuantityType lq, AgentId tid, ClientOrderIdType cid) {
        this->_on_taker_partial_fill_limit(txid, taker_s, p, q_seg, lq, tid, cid);
    };
    exchange_.on_taker_full_fill_limit = [this](ExchangeIDType txid, ExchangeSide taker_s, ExchangePriceType p, ExchangeQuantityType total_q, AgentId tid, ClientOrderIdType cid) {
        this->_on_taker_full_fill_limit(txid, taker_s, p, total_q, tid, cid);
    };
    exchange_.on_taker_partial_fill_market = [this](ExchangeIDType txid, ExchangeSide taker_s, ExchangePriceType p, ExchangeQuantityType q_seg, ExchangeQuantityType lq, AgentId tid, ClientOrderIdType cid) {
        this->_on_taker_partial_fill_market(txid, taker_s, p, q_seg, lq, tid, cid);
    };
    exchange_.on_taker_full_fill_market = [this](ExchangeIDType txid, ExchangeSide taker_s, ExchangePriceType p, ExchangeQuantityType total_q, AgentId tid, ClientOrderIdType cid) {
        this->_on_taker_full_fill_market(txid, taker_s, p, total_q, tid, cid);
    };

    exchange_.on_order_book_snapshot = [this](const std::vector<L2_DATA_TYPE>& b, const std::vector<L2_DATA_TYPE>& a) {
        this->_on_order_book_snapshot(b, a);
    };
    exchange_.on_acknowledge_trigger_expiration = [this](ExchangeIDType xid, ExchangePriceType p, ExchangeQuantityType q, AgentId tid, ClientOrderIdType cid, ExchangeTimeType tus) {
        this->_on_acknowledge_trigger_expiration(xid, p, q, tid, cid, tus);
    };
    exchange_.on_reject_trigger_expiration = [this](ExchangeIDType xid, AgentId tid, ClientOrderIdType cid, ExchangeTimeType tus) {
        this->_on_reject_trigger_expiration(xid, tid, cid, tus);
    };
}


void EventModelExchangeAdapter::_process_limit_order(const ModelEvents::LimitOrderEvent& event, AgentId trader_id) {
    ExchangeSide ex_side = _to_exchange_side(event.side);
    ExchangeTimeType timeout_us_rep = std::chrono::duration_cast<std::chrono::microseconds>(event.timeout).count();

    ExchangeIDType xid = exchange_.place_limit_order(
            ex_side, event.price, event.quantity, timeout_us_rep,
            trader_id, event.client_order_id
    );

    if (xid != ID_DEFAULT) { // ID_DEFAULT means it was fully filled aggressively and didn't rest
        _register_order_mapping(trader_id, event.client_order_id, xid, MappedOrderType::LIMIT);
    } else {
        // If xid is ID_DEFAULT, it means the order was fully filled as a taker
        // and did not rest on the book. ExchangeServer will use a transient ID for these fills.
        // We still need a way to map responses for this client_order_id if an ack is expected
        // even for fully aggressive fills. The current _on_limit_order_acknowledged uses
        // `ack_exchange_order_id` which is `xid` here.
        // A transient ID *should* be generated by ExchangeServer and used in fills,
        // and the ack might report ID_DEFAULT or the transient ID.
        // For now, we don't register a mapping if it didn't rest. The fill events will carry
        // the client_order_id.
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Limit order for Trader " + std::to_string(trader_id) +
                                             ", CID " + std::to_string(event.client_order_id) + " did not rest (XID=ID_DEFAULT). No persistent mapping registered.");
    }
    _publish_orderbook_snapshot_if_changed();
}

void EventModelExchangeAdapter::_process_market_order(const ModelEvents::MarketOrderEvent& event, AgentId trader_id) {
    ExchangeSide ex_side = _to_exchange_side(event.side);

    ExchangeIDType transient_xid = exchange_.place_market_order(
            ex_side, event.quantity,
            trader_id, event.client_order_id
    );
    // Market orders always get a transient ID from ExchangeServer.
    // We register this mapping to correlate ACKs and Fills.
    _register_order_mapping(trader_id, event.client_order_id, transient_xid, MappedOrderType::MARKET);
    _publish_orderbook_snapshot_if_changed();
}

void EventModelExchangeAdapter::_process_full_cancel_limit_order(const ModelEvents::FullCancelLimitOrderEvent& event, AgentId trader_id) {
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, event.target_order_id);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    if (!xid_opt) {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "FullCancelLimitOrder: XID not found for Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));
        auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
                current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }
    ExchangeOrderIdType xid = *xid_opt;

    auto order_type_it = order_type_map_.find(xid);
    if (order_type_it == order_type_map_.end() || order_type_it->second != MappedOrderType::LIMIT) {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "FullCancelLimitOrder: Target XID " + std::to_string(xid) + " is not a limit order or mapping missing.");
        auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
                current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    bool success = exchange_.cancel_order(xid, trader_id, event.client_order_id);
    if (success) {
        _publish_orderbook_snapshot_if_changed();
    }
    // Rejection is handled by _on_full_cancel_limit_reject callback
}

void EventModelExchangeAdapter::_process_full_cancel_market_order(const ModelEvents::FullCancelMarketOrderEvent& event, AgentId trader_id) {
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, event.target_order_id);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    if (xid_opt) {
        ExchangeOrderIdType xid = *xid_opt;
        auto order_type_it = order_type_map_.find(xid);
        if (order_type_it != order_type_map_.end() && order_type_it->second == MappedOrderType::MARKET) {
            // Market orders are typically FOK or fill-what-you-can-immediately.
            // Cancelling a market order that has already been processed might not be possible.
            // ExchangeServer's cancel_order is generic; it might succeed if the order somehow still exists.
            // However, standard market orders don't "rest" to be cancelled later.
            // This usually implies a reject.
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "FullCancelMarketOrder: Attempting to cancel market order XID " + std::to_string(xid) + ". This is unusual and will likely be rejected or have no effect.");
            // exchange_.cancel_order(xid, trader_id, event.client_order_id); // We can call it, but expect rejection.
            // For now, let's assume market orders cannot be cancelled after submission and ack.
        } else {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "FullCancelMarketOrder: Target XID " + std::to_string(xid) + " is not a market order or mapping missing.");
        }
    } else {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "FullCancelMarketOrder: XID not found for Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));
    }

    // Generally, market orders cannot be cancelled after they've been accepted and processed.
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
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "PartialCancelLimitOrder: XID not found for Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
                current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }
    ExchangeOrderIdType xid = *xid_opt;

    auto order_type_it = order_type_map_.find(xid);
    if (order_type_it == order_type_map_.end() || order_type_it->second != MappedOrderType::LIMIT) {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "PartialCancelLimitOrder: Target XID " + std::to_string(xid) + " is not a limit order or mapping missing.");
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
                current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    std::optional<std::tuple<ExchangePriceType, ExchangeQuantityType, ExchangeSide>> details_opt = exchange_.get_order_details(xid);
    if (!details_opt) {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "PartialCancelLimitOrder: Could not get details for XID " + std::to_string(xid) + ". Order might be gone.");
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
                current_time, event.client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", trader_id),
                        _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    ExchangeQuantityType current_qty_on_book = std::get<1>(*details_opt);
    if (event.cancel_qty <= 0) {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "PartialCancelLimitOrder: Cancel quantity (" + std::to_string(event.cancel_qty) + ") must be positive. Rejecting.");
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(current_time, event.client_order_id, symbol_);
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", trader_id), _format_stream_id(trader_id, event.client_order_id), reject_event);
        return;
    }

    ExchangeQuantityType new_qty_target = (event.cancel_qty >= current_qty_on_book) ? 0 : (current_qty_on_book - event.cancel_qty);

    bool success;
    if (new_qty_target == 0) { // If cancel_qty reduces order to 0 or less, it's a full cancel
        success = exchange_.cancel_order(xid, trader_id, event.client_order_id);
    } else {
        success = exchange_.modify_order_quantity(xid, new_qty_target, trader_id, event.client_order_id);
    }

    if (success) {
        _publish_orderbook_snapshot_if_changed();
    }
    // Rejection/Ack is handled by callbacks (_on_partial_cancel_limit_reject, _on_partial_cancel_limit, or _on_full_cancel_limit if new_qty was 0)
}

void EventModelExchangeAdapter::_process_partial_cancel_market_order(const ModelEvents::PartialCancelMarketOrderEvent& event, AgentId trader_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    LogMessage(LogLevel::WARNING, this->get_logger_source(), "PartialCancelMarketOrder: Market orders cannot typically be partially cancelled after submission. Rejecting. Trader " + std::to_string(trader_id) + ", TargetCID " + std::to_string(event.target_order_id));

    auto reject_event = std::make_shared<const ModelEvents::PartialCancelMarketOrderRejectEvent>(
            current_time, event.client_order_id, symbol_
    );
    publish_wrapper(_format_topic_for_trader("PartialCancelMarketOrderRejectEvent", trader_id),
                    _format_stream_id(trader_id, event.client_order_id), reject_event);
}

void EventModelExchangeAdapter::_process_bang(const ModelEvents::Bang& /*event unused*/) {
    LogMessage(LogLevel::INFO, this->get_logger_source(), "Processing Bang event. Flushing exchange and all local mappings.");
    trader_client_to_exchange_map_.clear();
    exchange_to_trader_client_map_.clear();
    order_type_map_.clear();
    expiration_trigger_sender_map_.clear();
    partial_fill_tracker_.clear(); // Clear partial fill states

    last_published_bids_l2_ = std::nullopt;
    last_published_asks_l2_ = std::nullopt;

    exchange_.flush(); // Flushes ExchangeServer's internal state

    Timestamp current_time_for_bang = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    publish_wrapper("Bang", std::make_shared<const ModelEvents::Bang>(current_time_for_bang));
    _publish_orderbook_snapshot_if_changed(); // Will publish empty book if auto_publish is on
}

void EventModelExchangeAdapter::_process_trigger_expired_limit_order_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event, AgentId trigger_sender_id) {
    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Processing TriggerExpiredLimitOrderEvent for XID: " + std::to_string(event.target_exchange_order_id) + " from sender: " + std::to_string(trigger_sender_id));

    ExchangeIDType xid_to_cancel = event.target_exchange_order_id;
    ExchangeTimeType timeout_us_rep = std::chrono::duration_cast<std::chrono::microseconds>(event.timeout_value).count();

    expiration_trigger_sender_map_[xid_to_cancel] = trigger_sender_id;

    bool call_succeeded = exchange_.cancel_expired_order(xid_to_cancel, timeout_us_rep);

    if (call_succeeded) { // This means the call to exchange was made, not necessarily that the order was found and cancelled.
        _publish_orderbook_snapshot_if_changed();
    }
    // Callbacks (_on_acknowledge_trigger_expiration / _on_reject_trigger_expiration) will handle publishing ack/reject
    // and cleaning expiration_trigger_sender_map_.
}


void EventModelExchangeAdapter::_on_limit_order_acknowledged(
        ExchangeIDType xid, ExchangeSide ex_side, ExchangePriceType price,
        ExchangeQuantityType quantity, ExchangeQuantityType remaining_qty,
        AgentId trader_id, ClientOrderIdType client_order_id, ExchangeTimeType timeout_us_rep) {

    ModelEvents::Side model_side = _to_model_side(ex_side);
    Duration timeout_duration = std::chrono::microseconds(timeout_us_rep);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    // If xid is ID_DEFAULT here, it means the order was fully aggressive and ExchangeServer might not have assigned a persistent XID.
    // The ModelEvents::LimitOrderAckEvent expects an ExchangeOrderIdType.
    // If ExchangeServer's place_limit_order returns ID_DEFAULT for fully aggressive fills,
    // we use that. Client must be aware. Alternatively, ExchangeServer could use a transient ID even for this ack.
    // For now, pass what ExchangeServer gave.
    ExchangeOrderIdType ack_xid_to_publish = xid;


    auto ack_event = std::make_shared<const ModelEvents::LimitOrderAckEvent>(
            current_time, ack_xid_to_publish, client_order_id, model_side, price, quantity, symbol_, timeout_duration,
            trader_id // original_trader_id field in LimitOrderAckEvent
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("LimitOrderAckEvent", trader_id), stream_id_str, ack_event);
    publish_wrapper("LimitOrderAckEvent", stream_id_str, ack_event); // Generic topic

    if (xid != ID_DEFAULT && remaining_qty == 0) { // If it had a persistent ID and is now fully gone
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Limit order XID " + std::to_string(xid) + " fully resolved on acknowledgement (remaining_qty=0). Removing mapping.");
        _remove_order_mapping(xid);
    }
    // If xid was ID_DEFAULT, no mapping was registered in _process_limit_order, so no removal needed here.
}

void EventModelExchangeAdapter::_on_market_order_acknowledged(
        ExchangeSide ex_side, ExchangeQuantityType req_qty, ExchangeQuantityType exec_qty,
        ExchangeQuantityType unfill_qty, AgentId trader_id, ClientOrderIdType client_order_id) {

    ModelEvents::Side model_side = _to_model_side(ex_side);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    // Market orders always get a transient XID from ExchangeServer, which we map.
    std::optional<ExchangeOrderIdType> xid_opt = _get_exchange_order_id(trader_id, client_order_id);
    ExchangeOrderIdType xid_for_ack = xid_opt.value_or(ID_DEFAULT); // Should always find it.
    if (!xid_opt) {
         LogMessage(LogLevel::ERROR, this->get_logger_source(), "MarketOrderAck: XID not found for Trader " + std::to_string(trader_id) + ", CID " + std::to_string(client_order_id) + ". This is unexpected.");
    }


    auto ack_event = std::make_shared<const ModelEvents::MarketOrderAckEvent>(
            current_time, xid_for_ack, client_order_id, model_side, req_qty, symbol_
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("MarketOrderAckEvent", trader_id), stream_id_str, ack_event);
    // No generic publish for MarketOrderAckEvent based on original code, can be added if needed.

    // If the market order is fully processed (either fully filled or remaining part is unfillable)
    if (xid_for_ack != ID_DEFAULT && (exec_qty == req_qty || unfill_qty > 0) ) {
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Market order XID " + std::to_string(xid_for_ack) + " fully resolved on acknowledgement. Removing mapping.");
        _remove_order_mapping(xid_for_ack);
    }
}

void EventModelExchangeAdapter::_on_partial_cancel_limit(
        ExchangeIDType xid, ExchangePriceType price_ignored, ExchangeQuantityType cancelled_qty,
        AgentId req_trader_id, ClientOrderIdType req_client_order_id) {

    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if (!original_ids_opt) {
        LogMessage(LogLevel::ERROR, this->get_logger_source(), "PartialCancelLimit ACK for unknown XID: " + std::to_string(xid) + ". Rejecting cancel request CID: " + std::to_string(req_client_order_id));
        Timestamp current_time_reject = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
                current_time_reject, req_client_order_id, symbol_
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
    ExchangeQuantityType original_total_qty_before_this_cancel = cancelled_qty; // Initial estimate

    if (details_opt) {
        remaining_qty_after_cancel = std::get<1>(*details_opt);
        ex_side_original_order = std::get<2>(*details_opt);
        original_total_qty_before_this_cancel = remaining_qty_after_cancel + cancelled_qty;
    } else {
        // This case means the order is GONE from the book (e.g. cancelled_qty made it 0, and it was fully removed by exchange.cancel_order)
        // The 'price_ignored' from callback might be useful if exchange_.get_order_details fails,
        // but side is more critical. We need to infer what its original side was.
        // This scenario is tricky. If details_opt is null, it implies remaining_qty_after_cancel is 0.
        // The original_total_qty_before_this_cancel would just be 'cancelled_qty'.
        // The side needs to be fetched from historical data or the cancel request itself.
        // For now, assume if details are gone, remaining is 0. Side is problematic.
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "PartialCancelLimit ACK for XID " + std::to_string(xid) + " but current details not found. Order might be fully gone. Estimating original side/qty.");
        remaining_qty_after_cancel = 0;
        // Try to get side from order_type_map (it won't give side, but proves it existed)
        // This is a limitation; ideally, ExchangeServer provides all necessary info.
        // Let's assume `on_full_cancel_limit` handles cases where partial cancel results in full cancel.
        // This callback `_on_partial_cancel_limit` should only be for true partials where order still exists.
        // If ExchangeServer calls this for an order that became fully cancelled, it's a bit of a mixed signal.
        // However, `ExchangeServer::modify_order_quantity` calls `on_partial_cancel_limit` if `new_volume < old_volume && !removed`.
        // If `new_volume` becomes 0, `modify_order_quantity` sets `removed = true` and should not call this one.
        // `cancel_order` calls `on_full_cancel_limit`.
        // So, `details_opt` should ideally always be present here. If not, it's an anomaly.
        LogMessage(LogLevel::ERROR, this->get_logger_source(), "CRITICAL: _on_partial_cancel_limit called for XID " + std::to_string(xid) + " but get_order_details failed. This implies inconsistency.");
        // Fallback: publish reject for the cancel request if essential info is missing
        Timestamp current_time_reject = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
        auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(current_time_reject, req_client_order_id, symbol_);
        publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", req_trader_id), _format_stream_id(req_trader_id, req_client_order_id), reject_event);
        return;
    }

    ModelEvents::Side model_side_original_order = _to_model_side(ex_side_original_order);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::PartialCancelLimitAckEvent>(
            current_time,
            xid,
            req_client_order_id, // CID of the cancel request itself
            model_side_original_order,
            original_client_order_id, // CID of the original order being cancelled
            original_total_qty_before_this_cancel, // Original total quantity of the order
            symbol_,
            cancelled_qty, // Amount that was actually cancelled now
            remaining_qty_after_cancel // Amount left after this cancel
    );

    std::string stream_id_str = _format_stream_id(original_trader_id, original_client_order_id); // Stream of original order
    publish_wrapper(_format_topic_for_trader("PartialCancelLimitAckEvent", req_trader_id), stream_id_str, ack_event);

    // If remaining quantity is 0 due to this partial cancel, the order is effectively fully cancelled.
    // ExchangeServer's modify_order_quantity might have already removed it if new_qty was 0.
    // If it calls on_partial_cancel_limit and then the order is found to have 0 qty, remove mapping.
    // However, if modify_order_quantity reduced to 0, it sets `removed=true` and should not call this.
    // This is more of a safeguard. The primary removal should happen in _on_full_cancel_limit if it was a cancel_order call.
    if (remaining_qty_after_cancel == 0 && xid != ID_DEFAULT) {
        LogMessage(LogLevel::INFO, this->get_logger_source(), "Order XID " + std::to_string(xid) + " has 0 remaining quantity after partial cancel. Removing mapping.");
        _remove_order_mapping(xid);
    }
}

void EventModelExchangeAdapter::_on_partial_cancel_limit_reject(
        ExchangeIDType xid, AgentId req_trader_id, ClientOrderIdType req_client_order_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto reject_event = std::make_shared<const ModelEvents::PartialCancelLimitOrderRejectEvent>(
            current_time, req_client_order_id, symbol_
    );

    std::string stream_id_str;
    auto original_ids_opt = _get_trader_and_client_ids(xid); // xid is of the target order
    if(original_ids_opt) {
        stream_id_str = _format_stream_id(original_ids_opt->first, original_ids_opt->second);
    } else { // Fallback if target order mapping is already gone or never existed for this XID
        stream_id_str = _format_stream_id(req_trader_id, req_client_order_id);
    }

    publish_wrapper(_format_topic_for_trader("PartialCancelLimitOrderRejectEvent", req_trader_id), stream_id_str, reject_event);
}

void EventModelExchangeAdapter::_on_full_cancel_limit(
        ExchangeIDType xid, ExchangePriceType price_ignored, ExchangeQuantityType qty_cancelled,
        ExchangeSide ex_side, AgentId req_trader_id, ClientOrderIdType req_client_order_id) {

    auto original_ids_opt = _get_trader_and_client_ids(xid);
    if (!original_ids_opt) {
        // This can happen if the order was already removed due to full fill or other reasons
        // before the cancel confirmation arrives. The cancel request might still be acked by the exchange if it processed it.
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "FullCancelLimit ACK for XID: " + std::to_string(xid) + " but no original mapping found. This might be okay if order was filled/expired before cancel acked. Proceeding with cancel ack for request CID: " + std::to_string(req_client_order_id));
        // We cannot determine original_client_order_id. We must publish based on req_client_order_id.
        // The FullCancelLimitOrderAckEvent needs original_client_order_id. This is an issue.
        // Fallback: Use req_client_order_id for target_order_id in event if original is unknown.
        // However, the event expects the CID of the order that *was* cancelled.
        // For robustness, if we can't find original CID, we might need a different event or log heavily.

        // Let's assume for now this scenario means we should still try to publish the ack.
        // If original_client_order_id is crucial, this ACK might be unpublishable or misleading.
        // Consider if ExchangeServer should provide original_client_order_id in its callback if known. (It does not currently)
        // Let's publish a reject for the cancel request if we can't map it, as the ACK event might be ill-formed.
        Timestamp current_time_for_reject = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
        auto reject_event = std::make_shared<const ModelEvents::FullCancelLimitOrderRejectEvent>(
                current_time_for_reject, req_client_order_id, symbol_
        );
        publish_wrapper(_format_topic_for_trader("FullCancelLimitOrderRejectEvent", req_trader_id),
                        _format_stream_id(req_trader_id, req_client_order_id), reject_event);
        _remove_order_mapping(xid); // Attempt to clean up if any stray mapping exists
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
    publish_wrapper("FullCancelLimitOrderAckEvent", stream_id_str, ack_event); // Generic

    _remove_order_mapping(xid); // Order is gone
}

void EventModelExchangeAdapter::_on_full_cancel_limit_reject(
        ExchangeIDType xid, AgentId req_trader_id, ClientOrderIdType req_client_order_id) {
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
        ExchangeIDType maker_xid, ExchangeSide maker_ex_side, ExchangeIDType taker_xid, ExchangeSide taker_ex_side,
        ExchangePriceType price, ExchangeQuantityType qty, bool maker_exhausted,
        AgentId maker_trader_id, ClientOrderIdType maker_client_id,
        AgentId taker_trader_id, ClientOrderIdType taker_client_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    ModelEvents::Side maker_model_side = _to_model_side(maker_ex_side);
    // Taker side is implicitly opposite of maker, or can be derived from taker_ex_side if needed by event.
    // ModelEvents::TradeEvent uses maker_side.

    auto trade_event = std::make_shared<const ModelEvents::TradeEvent>(
            current_time, symbol_, maker_client_id, taker_client_id, maker_xid, taker_xid,
            price, qty, maker_model_side, maker_exhausted
    );

    std::string maker_stream_id_str = _format_stream_id(maker_trader_id, maker_client_id);
    std::string taker_stream_id_str = _format_stream_id(taker_trader_id, taker_client_id);

    std::string trade_topic = std::string("TradeEvent.") + symbol_;
    // Publish on maker's stream
    publish_wrapper(trade_topic, maker_stream_id_str, trade_event);
    // Publish on taker's stream if different (to avoid duplicate delivery if self-trade or same stream concept)
    if (maker_trader_id != taker_trader_id || maker_client_id != taker_client_id) { // Basic check for self-trade
        publish_wrapper(trade_topic, taker_stream_id_str, trade_event);
    }
}

// Common logic for partial fills
void update_partial_fill_state(
    ID_TYPE xid,
    PRICE_TYPE price_this_segment,
    SIZE_TYPE qty_filled_this_segment,
    EventModelExchangeAdapter::PartialFillState& state,
    ModelEvents::AveragePriceType& out_avg_price,
    ModelEvents::QuantityType& out_cumulative_qty,
    const std::string& logger_source) {

    state.cumulative_qty_filled += qty_filled_this_segment;
    // PriceType is int64_t, QuantityType is int64_t.
    // Their product can exceed int64_t if not careful, but typical trading values might be okay.
    // ModelEvents::PriceType is scaled. Let's assume price_this_segment is also scaled.
    // To calculate average price accurately, sum (scaled_price * scaled_qty)
    // then divide by total scaled_qty. This keeps it in scaled domain as long as possible.
    // Or, convert to double for value accumulation.
    state.cumulative_value_filled += static_cast<double>(price_this_segment) * qty_filled_this_segment;

    out_cumulative_qty = state.cumulative_qty_filled;
    if (state.cumulative_qty_filled > 0) {
        out_avg_price = state.cumulative_value_filled / static_cast<double>(state.cumulative_qty_filled);
    } else {
        out_avg_price = 0.0; // Or some indicator of no fills yet
    }
     LogMessage(LogLevel::DEBUG, logger_source, "PartialFill Update for XID " + std::to_string(xid) +
                                             ": SegmentQty=" + std::to_string(qty_filled_this_segment) +
                                             ", SegmentPrice=" + std::to_string(price_this_segment) +
                                             ", CumulativeQty=" + std::to_string(out_cumulative_qty) +
                                             ", CumulativeValue=" + std::to_string(state.cumulative_value_filled) +
                                             ", AvgPrice=" + std::to_string(out_avg_price));
}


void EventModelExchangeAdapter::_on_maker_partial_fill_limit(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment,
        ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    ModelEvents::Side model_side = _to_model_side(ex_maker_side);

    QuantityType leaves_qty = 0;
    auto details_opt = exchange_.get_order_details(maker_xid);
    if (details_opt) {
        leaves_qty = std::get<1>(*details_opt);
    } else {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "MakerPartialFillLimit: Could not get current details for XID " + std::to_string(maker_xid) + " to find leaves_qty. Assuming 0 if not found (order might be gone).");
    }

    PartialFillState& state = partial_fill_tracker_[maker_xid]; // Creates if not exists
    AveragePriceType avg_price_so_far;
    QuantityType cumulative_qty_filled_so_far;
    update_partial_fill_state(maker_xid, price, qty_filled_this_segment, state, avg_price_so_far, cumulative_qty_filled_so_far, this->get_logger_source());

    auto fill_event = std::make_shared<const ModelEvents::PartialFillLimitOrderEvent>(
            current_time, maker_xid, client_order_id, model_side, price, qty_filled_this_segment, current_time, symbol_, true, /*is_maker*/
            leaves_qty, cumulative_qty_filled_so_far, avg_price_so_far
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
}

void EventModelExchangeAdapter::_on_taker_partial_fill_limit(
        ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment, ExchangeQuantityType leaves_qty_on_taker_order,
        AgentId trader_id, ClientOrderIdType client_order_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    ModelEvents::Side model_side = _to_model_side(taker_ex_side);

    // Taker XID can be a persistent XID (if limit order rested then became aggressive)
    // or a transient XID (if limit order was immediately aggressive or for market orders).
    PartialFillState& state = partial_fill_tracker_[taker_xid]; // Creates if not exists
    AveragePriceType avg_price_so_far;
    QuantityType cumulative_qty_filled_so_far;
    update_partial_fill_state(taker_xid, price, qty_filled_this_segment, state, avg_price_so_far, cumulative_qty_filled_so_far, this->get_logger_source());

    auto fill_event = std::make_shared<const ModelEvents::PartialFillLimitOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, qty_filled_this_segment, current_time, symbol_, false, /*is_maker=false*/
            leaves_qty_on_taker_order, cumulative_qty_filled_so_far, avg_price_so_far
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
}

void EventModelExchangeAdapter::_on_maker_full_fill_limit(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_maker,
        ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    ModelEvents::Side model_side = _to_model_side(ex_maker_side);

    AveragePriceType final_avg_price;
    QuantityType final_cumulative_qty;
    // If there were prior partial fills, their state is in partial_fill_tracker_.
    // The 'total_qty_filled_for_maker' is the *total for this order*, not this segment.
    // The 'price' is the price of the *last segment* that caused full fill.
    auto it = partial_fill_tracker_.find(maker_xid);
    if (it != partial_fill_tracker_.end()) {
        PartialFillState& state = it->second;
        // The qty_filled_this_segment that leads to full fill: total_qty_filled_for_maker - state.cumulative_qty_filled
        QuantityType last_segment_qty = total_qty_filled_for_maker - state.cumulative_qty_filled;
        if (last_segment_qty < 0) { // Should not happen if logic is correct
             LogMessage(LogLevel::ERROR, this->get_logger_source(), "MakerFullFillLimit: Negative last_segment_qty for XID " + std::to_string(maker_xid) + ". total_qty=" + std::to_string(total_qty_filled_for_maker) + ", prev_cum_qty=" + std::to_string(state.cumulative_qty_filled));
             last_segment_qty = 0; // Avoid issues, but indicates problem
        }
        update_partial_fill_state(maker_xid, price, last_segment_qty, state, final_avg_price, final_cumulative_qty, this->get_logger_source());
        if (final_cumulative_qty != total_qty_filled_for_maker) {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "MakerFullFillLimit: Mismatch cumulative qty for XID " + std::to_string(maker_xid) + ". Calculated: " + std::to_string(final_cumulative_qty) + ", Reported total: " + std::to_string(total_qty_filled_for_maker));
            final_cumulative_qty = total_qty_filled_for_maker; // Trust reported total for the event
            // Recalculate avg price if cumulative qty was overridden (though ideally they match)
            if (final_cumulative_qty > 0) final_avg_price = state.cumulative_value_filled / static_cast<double>(final_cumulative_qty); else final_avg_price = 0;
        }

    } else { // No prior partial fills, this full fill is from one go.
        final_cumulative_qty = total_qty_filled_for_maker;
        final_avg_price = static_cast<AveragePriceType>(price); // If single fill, avg price is the fill price
         LogMessage(LogLevel::DEBUG, this->get_logger_source(), "MakerFullFillLimit (no prior partials) for XID " + std::to_string(maker_xid) +
                                                             ": TotalQty=" + std::to_string(final_cumulative_qty) +
                                                             ", Price=" + std::to_string(price));
    }


    auto fill_event = std::make_shared<const ModelEvents::FullFillLimitOrderEvent>(
            current_time, maker_xid, client_order_id, model_side, price, total_qty_filled_for_maker, /* This is total qty of order */
            current_time, symbol_, true, /*is_maker*/
            final_avg_price
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillLimitOrderEvent", trader_id), stream_id_str, fill_event);
    publish_wrapper("FullFillLimitOrderEvent", stream_id_str, fill_event); // Generic

    _remove_order_mapping(maker_xid); // Clears main maps and partial_fill_tracker_
}

void EventModelExchangeAdapter::_on_taker_full_fill_limit(
        ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_taker,
        AgentId trader_id, ClientOrderIdType client_order_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    ModelEvents::Side model_side = _to_model_side(taker_ex_side);

    AveragePriceType final_avg_price;
    QuantityType final_cumulative_qty;

    auto it = partial_fill_tracker_.find(taker_xid);
    if (it != partial_fill_tracker_.end()) {
        PartialFillState& state = it->second;
        QuantityType last_segment_qty = total_qty_filled_for_taker - state.cumulative_qty_filled;
         if (last_segment_qty < 0) {
             LogMessage(LogLevel::ERROR, this->get_logger_source(), "TakerFullFillLimit: Negative last_segment_qty for XID " + std::to_string(taker_xid) + ". total_qty=" + std::to_string(total_qty_filled_for_taker) + ", prev_cum_qty=" + std::to_string(state.cumulative_qty_filled));
             last_segment_qty = 0;
        }
        update_partial_fill_state(taker_xid, price, last_segment_qty, state, final_avg_price, final_cumulative_qty, this->get_logger_source());
         if (final_cumulative_qty != total_qty_filled_for_taker) {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "TakerFullFillLimit: Mismatch cumulative qty for XID " + std::to_string(taker_xid) + ". Calculated: " + std::to_string(final_cumulative_qty) + ", Reported total: " + std::to_string(total_qty_filled_for_taker));
            final_cumulative_qty = total_qty_filled_for_taker;
            if (final_cumulative_qty > 0) final_avg_price = state.cumulative_value_filled / static_cast<double>(final_cumulative_qty); else final_avg_price = 0;
        }
    } else {
        final_cumulative_qty = total_qty_filled_for_taker;
        final_avg_price = static_cast<AveragePriceType>(price);
         LogMessage(LogLevel::DEBUG, this->get_logger_source(), "TakerFullFillLimit (no prior partials) for XID " + std::to_string(taker_xid) +
                                                             ": TotalQty=" + std::to_string(final_cumulative_qty) +
                                                             ", Price=" + std::to_string(price));
    }

    auto fill_event = std::make_shared<const ModelEvents::FullFillLimitOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, total_qty_filled_for_taker,
            current_time, symbol_, false, /*is_maker=false*/
            final_avg_price
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillLimitOrderEvent", trader_id), stream_id_str, fill_event);

    // Publish generic event only if the taker_xid is persistent (not transient from market_order range)
    // This check might be too simple; need a robust way to identify transient IDs if they come from different counters.
    // Assuming transient IDs are large, persistent (resting) IDs are smaller.
    // Or, more reliably, check if it was in `order_type_map_` as `LIMIT`.
    // For now, using the provided assert check philosophy.
    assert(taker_xid != ID_DEFAULT && "Taker XID for limit full fill should not be ID_DEFAULT");
    if (taker_xid != ID_DEFAULT) { // Also implies it was a mapped order or should have been
        auto order_type_it = order_type_map_.find(taker_xid);
        if (order_type_it != order_type_map_.end() && order_type_it->second == MappedOrderType::LIMIT) {
            publish_wrapper("FullFillLimitOrderEvent", stream_id_str, fill_event);
        }
        _remove_order_mapping(taker_xid);
    } else {
         LogMessage(LogLevel::ERROR, this->get_logger_source(),
                   "_on_taker_full_fill_limit called with taker_xid == ID_DEFAULT. "
                   "This indicates an unexpected issue in ExchangeServer or callback logic. "
                   "TraderID: " + std::to_string(trader_id) +
                   ", ClientOrderID: " + std::to_string(client_order_id) +
                   ". The fill_event was still published to the trader-specific topic.");
    }
}

void EventModelExchangeAdapter::_on_maker_partial_fill_market(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment,
        ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id) {
    // Market orders don't typically "make" the market by resting.
    // This callback implies a limit order was hit by an incoming market order.
    // So, this is essentially a partial fill of a resting limit order.
    _on_maker_partial_fill_limit(maker_xid, price, qty_filled_this_segment, ex_maker_side, trader_id, client_order_id);
}

void EventModelExchangeAdapter::_on_taker_partial_fill_market(
        ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType qty_filled_this_segment, ExchangeQuantityType leaves_qty_on_taker_order,
        AgentId trader_id, ClientOrderIdType client_order_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    ModelEvents::Side model_side = _to_model_side(taker_ex_side);

    PartialFillState& state = partial_fill_tracker_[taker_xid]; // Market order XID
    AveragePriceType avg_price_so_far;
    QuantityType cumulative_qty_filled_so_far;
    update_partial_fill_state(taker_xid, price, qty_filled_this_segment, state, avg_price_so_far, cumulative_qty_filled_so_far, this->get_logger_source());

    auto fill_event = std::make_shared<const ModelEvents::PartialFillMarketOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, qty_filled_this_segment, current_time, symbol_, false, /*is_maker=false*/
            leaves_qty_on_taker_order, cumulative_qty_filled_so_far, avg_price_so_far
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("PartialFillMarketOrderEvent", trader_id), stream_id_str, fill_event);
}

void EventModelExchangeAdapter::_on_maker_full_fill_market(
        ExchangeIDType maker_xid, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_maker,
        ExchangeSide ex_maker_side, AgentId trader_id, ClientOrderIdType client_order_id) {
    // This implies a limit order was fully filled by an incoming market order.
    _on_maker_full_fill_limit(maker_xid, price, total_qty_filled_for_maker, ex_maker_side, trader_id, client_order_id);
}

void EventModelExchangeAdapter::_on_taker_full_fill_market(
        ExchangeIDType taker_xid, ExchangeSide taker_ex_side, ExchangePriceType price, ExchangeQuantityType total_qty_filled_for_taker,
        AgentId trader_id, ClientOrderIdType client_order_id) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    ModelEvents::Side model_side = _to_model_side(taker_ex_side);

    AveragePriceType final_avg_price;
    QuantityType final_cumulative_qty;

    auto it = partial_fill_tracker_.find(taker_xid);
    if (it != partial_fill_tracker_.end()) {
        PartialFillState& state = it->second;
        QuantityType last_segment_qty = total_qty_filled_for_taker - state.cumulative_qty_filled;
         if (last_segment_qty < 0) {
             LogMessage(LogLevel::ERROR, this->get_logger_source(), "TakerFullFillMarket: Negative last_segment_qty for XID " + std::to_string(taker_xid) + ". total_qty=" + std::to_string(total_qty_filled_for_taker) + ", prev_cum_qty=" + std::to_string(state.cumulative_qty_filled));
             last_segment_qty = 0;
        }
        update_partial_fill_state(taker_xid, price, last_segment_qty, state, final_avg_price, final_cumulative_qty, this->get_logger_source());
        if (final_cumulative_qty != total_qty_filled_for_taker) {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "TakerFullFillMarket: Mismatch cumulative qty for XID " + std::to_string(taker_xid) + ". Calculated: " + std::to_string(final_cumulative_qty) + ", Reported total: " + std::to_string(total_qty_filled_for_taker));
            final_cumulative_qty = total_qty_filled_for_taker;
             if (final_cumulative_qty > 0) final_avg_price = state.cumulative_value_filled / static_cast<double>(final_cumulative_qty); else final_avg_price = 0;
        }
    } else {
        final_cumulative_qty = total_qty_filled_for_taker;
        final_avg_price = static_cast<AveragePriceType>(price);
         LogMessage(LogLevel::DEBUG, this->get_logger_source(), "TakerFullFillMarket (no prior partials) for XID " + std::to_string(taker_xid) +
                                                             ": TotalQty=" + std::to_string(final_cumulative_qty) +
                                                             ", Price=" + std::to_string(price));
    }

    auto fill_event = std::make_shared<const ModelEvents::FullFillMarketOrderEvent>(
            current_time, taker_xid, client_order_id, model_side, price, total_qty_filled_for_taker,
            current_time, symbol_, false, /*is_maker=false*/
            final_avg_price
    );

    std::string stream_id_str = _format_stream_id(trader_id, client_order_id);
    publish_wrapper(_format_topic_for_trader("FullFillMarketOrderEvent", trader_id), stream_id_str, fill_event);

    // Market orders use transient XIDs that are mapped via (trader_id, client_order_id)
    // So, we should find the original mapped XID to remove.
    // The `taker_xid` received here *is* that mapped XID.
    if (taker_xid != ID_DEFAULT) {
        _remove_order_mapping(taker_xid);
    } else {
        // This case should ideally not happen for market orders if ExchangeServer always provides a valid transient ID.
         LogMessage(LogLevel::ERROR, this->get_logger_source(),
                   "_on_taker_full_fill_market called with taker_xid == ID_DEFAULT. This is unexpected for market orders. "
                   "TraderID: " + std::to_string(trader_id) + ", ClientOrderID: " + std::to_string(client_order_id));
    }
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
                current_time, symbol_, current_time, current_time, // ModelEvent expects created_ts, symbol, exchange_ts, ingress_ts
                current_bids_level,
                current_asks_level
        );

        std::string stream_id_str = "l2_stream_" + symbol_;
        publish_wrapper(std::string("LTwoOrderBookEvent.") + symbol_, stream_id_str, ob_event);
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Published updated L2 snapshot for " + symbol_);
    } else {
        LogMessage(LogLevel::INFO, this->get_logger_source(), "L2 snapshot unchanged for " + symbol_ + ", not publishing."); // Changed to TRACE for less noise
    }
}

void EventModelExchangeAdapter::_on_acknowledge_trigger_expiration(
        ExchangeIDType xid, ExchangePriceType price, ExchangeQuantityType qty_expired,
        AgentId original_placer_trader_id, ClientOrderIdType original_placer_client_order_id,
        ExchangeTimeType timeout_us_rep) {

    Duration timeout_duration = std::chrono::microseconds(timeout_us_rep);
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};

    auto ack_event = std::make_shared<const ModelEvents::AckTriggerExpiredLimitOrderEvent>(
            current_time, symbol_, xid, original_placer_client_order_id, price, qty_expired, timeout_duration
    );

    std::string stream_id_str = _format_stream_id(original_placer_trader_id, original_placer_client_order_id);

    AgentId expiration_trigger_sender = EventBusSystem::INVALID_AGENT_ID;
    auto it_sender = expiration_trigger_sender_map_.find(xid);
    if (it_sender != expiration_trigger_sender_map_.end()) {
        expiration_trigger_sender = it_sender->second;
        expiration_trigger_sender_map_.erase(it_sender); // Clean up map entry
    } else {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not find expiration trigger sender for XID " + std::to_string(xid) + ". Ack will not be specifically targeted to trigger sender.");
    }

    // Publish to the agent that triggered the expiration check (e.g., CancelFairy)
    if (expiration_trigger_sender != EventBusSystem::INVALID_AGENT_ID) {
         publish_wrapper(_format_topic_for_trader("AckTriggerExpiredLimitOrderEvent", expiration_trigger_sender), stream_id_str, ack_event);
    }

    // Publish to the original placer of the order, if different from trigger sender
    if (original_placer_trader_id != expiration_trigger_sender && original_placer_trader_id != EventBusSystem::INVALID_AGENT_ID) {
        publish_wrapper(_format_topic_for_trader("AckTriggerExpiredLimitOrderEvent", original_placer_trader_id), stream_id_str, ack_event);
    }

    // Publish to a generic topic as well
    publish_wrapper("AckTriggerExpiredLimitOrderEvent", stream_id_str, ack_event);


    _remove_order_mapping(xid); // Order is gone
}

void EventModelExchangeAdapter::_on_reject_trigger_expiration(
        ExchangeIDType xid, AgentId original_placer_trader_id, ClientOrderIdType original_placer_client_order_id,
        ExchangeTimeType timeout_us_rep) {
    Timestamp current_time = this->bus_ ? this->bus_->get_current_time() : Timestamp{};
    Duration original_timeout_duration = std::chrono::microseconds(timeout_us_rep);

    auto reject_event = std::make_shared<const ModelEvents::RejectTriggerExpiredLimitOrderEvent>(
            current_time, symbol_, xid, original_timeout_duration
    );

    std::string stream_id_str = _format_stream_id(original_placer_trader_id, original_placer_client_order_id);

    AgentId expiration_trigger_sender = EventBusSystem::INVALID_AGENT_ID;
    auto it_sender = expiration_trigger_sender_map_.find(xid);
    if (it_sender != expiration_trigger_sender_map_.end()) {
        expiration_trigger_sender = it_sender->second;
        expiration_trigger_sender_map_.erase(it_sender); // Clean up map entry
    } else {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "Could not find expiration trigger sender for XID " + std::to_string(xid) + ". Reject will not be specifically targeted to trigger sender.");
    }

    // Publish to the agent that triggered the expiration check
    if (expiration_trigger_sender != EventBusSystem::INVALID_AGENT_ID) {
        publish_wrapper(_format_topic_for_trader("RejectTriggerExpiredLimitOrderEvent", expiration_trigger_sender), stream_id_str, reject_event);
    }
    // No need to publish to original placer for reject of expiration trigger usually, unless specified.
    // Generic publish can also be considered.
    // Unlike ACK, a reject means the order is still on the book (or was not found/not expired).
    // So, _remove_order_mapping should NOT be called here.
}