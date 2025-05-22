//================
// Model.h
//================
#pragma once

#include "EventBus.h" // Include the EventBus header for base types like Timestamp, AgentId etc.

#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <iomanip> // For formatting output
#include <utility> // For std::pair, std::move
#include <atomic>  // For static event ID counter
#include <limits>  // For numeric_limits
#include <cmath>   // For std::abs in comparisons
#include <type_traits> // For type checks


namespace ModelEvents {

    // --- Type Aliases (Matching Python types where possible/sensible) ---
    using EventBusSystem::Timestamp;
    using EventBusSystem::Duration;
    using EventBusSystem::AgentId;
    using EventBusSystem::TopicId;
    using EventBusSystem::StreamId;
    using EventBusSystem::SequenceNumber;

    using SymbolType = std::string; // Consider InternedStringId if symbols are very numerous and reused
    using PriceType = int64_t; // Represents scaled price (e.g., cents * 100)
    using QuantityType = int64_t; // Represents scaled quantity (e.g., shares * 10000)
    using ClientOrderIdType = uint64_t; // Assuming numeric client order IDs for performance
    using ExchangeOrderIdType = uint64_t; // Assuming numeric exchange order IDs for performance
    using AveragePriceType = double; // Use double for average price calculations
    using EventIdType = uint64_t; // Simple incrementing ID for performance

    // Enum for Side (clearer and safer than bool)
    enum class Side {
        BUY,
        SELL
    };

    // Helper to convert Side enum to string
    inline std::string side_to_string(Side s) {
        return (s == Side::BUY) ? "BUY" : "SELL";
    }

    // Helper to format timestamp (microseconds since epoch)
    inline std::string format_timestamp(Timestamp ts) {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(ts.time_since_epoch()).count();
        return std::to_string(us) + "us";
    }

    // Helper to format optional timestamp
    inline std::string format_optional_timestamp(const std::optional<Timestamp>& opt_ts) {
        if (opt_ts) {
            return format_timestamp(*opt_ts);
        } else {
            return "None";
        }
    }

    // Helper to format duration (in microseconds)
    inline std::string format_duration(Duration d) {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(d).count();
        return std::to_string(us) + "us";
    }

    // ================ ADD THIS BLOCK INSIDE namespace ModelEvents in Model.h ================

    // --- Constants and Conversion Helpers ---
    constexpr double PRICE_SCALE_FACTOR = 10000.0;   // Example: Price is stored as float * 10000
    constexpr double QUANTITY_SCALE_FACTOR = 10000.0; // Example: Qty is stored as float * 10000
    constexpr double BPS_DIVISOR = 10000.0;
    constexpr int64_t MICROSECONDS_PER_SECOND_I64 = 1'000'000LL; // For integer conversions


    inline PriceType float_to_price(double float_price) {
        // Add 0.5 for rounding before truncating
        return static_cast<PriceType>(float_price * PRICE_SCALE_FACTOR + 0.5);
    }

    inline double price_to_float(PriceType int_price) {
        return static_cast<double>(int_price) / PRICE_SCALE_FACTOR;
    }

    inline QuantityType float_to_quantity(double float_quantity) {
        // Add 0.5 for rounding before truncating
        return static_cast<QuantityType>(float_quantity * QUANTITY_SCALE_FACTOR + 0.5);
    }

    inline double quantity_to_float(QuantityType int_quantity) {
        return static_cast<double>(int_quantity) / QUANTITY_SCALE_FACTOR;
    }

    // Convert Duration to float seconds for logging/comparison
    inline double duration_to_float_seconds(Duration d) {
        // Use floating point division for precision
        return std::chrono::duration<double>(d).count();
    }

    // Convert float seconds to Duration
    inline Duration float_seconds_to_duration(double seconds) {
        // Handle potential precision issues by converting via microseconds
        // Clamp negative values to zero duration
        if (seconds <= 0.0) {
            return Duration::zero();
        }
        auto microseconds = static_cast<int64_t>(seconds * MICROSECONDS_PER_SECOND_I64 + 0.5); // Round
        return std::chrono::microseconds(microseconds);
    }

    // Represent L2 Order Book Levels efficiently
    using PriceQuantityPair = std::pair<PriceType, QuantityType>;
    using OrderBookLevel = std::vector<PriceQuantityPair>; // Vector of price/qty pairs

    // ------------------------------------------------------------------
    // Base Event
    // ------------------------------------------------------------------
    struct BaseEvent {
        EventIdType event_id;
        Timestamp created_ts;

        static inline std::atomic<EventIdType> next_event_id{1};

        explicit BaseEvent(Timestamp ts) : event_id(next_event_id.fetch_add(1, std::memory_order_relaxed)), created_ts(ts) {}
        virtual ~BaseEvent() = default;
        BaseEvent(const BaseEvent&) = delete;
        BaseEvent& operator=(const BaseEvent&) = delete;
        BaseEvent(BaseEvent&&) = default;
        BaseEvent& operator=(BaseEvent&&) = default;

        virtual std::string to_string() const {
            std::ostringstream oss;
            oss << "event_id=" << event_id
                << ", created_ts=" << format_timestamp(created_ts);
            return oss.str();
        }
    };

    struct CheckLimitOrderExpirationEvent : BaseEvent {
        ExchangeOrderIdType target_exchange_order_id;
        Duration original_timeout;

        CheckLimitOrderExpirationEvent(
                Timestamp created_ts,
                ExchangeOrderIdType target_xid,
                Duration original_order_timeout
        )
                : BaseEvent(created_ts),
                  target_exchange_order_id(target_xid),
                  original_timeout(original_order_timeout) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "CheckLimitOrderExpirationEvent(" << BaseEvent::to_string()
                << ", target_exchange_order_id=" << target_exchange_order_id
                << ", original_timeout=" << format_duration(original_timeout) << ")";
            return oss.str();
        }
    };

    struct Bang : BaseEvent {
        explicit Bang(Timestamp created_ts) : BaseEvent(created_ts) {}
        std::string to_string() const override {
            return "Bang(" + BaseEvent::to_string() + ")";
        }
    };

    struct LTwoOrderBookEvent : BaseEvent {
        SymbolType symbol;
        std::optional<Timestamp> exchange_ts;
        Timestamp ingress_ts;
        OrderBookLevel bids;
        OrderBookLevel asks;

        LTwoOrderBookEvent(
                Timestamp created_ts, SymbolType sym, std::optional<Timestamp> ex_ts,
                Timestamp ing_ts, OrderBookLevel b, OrderBookLevel a
        ) : BaseEvent(created_ts), symbol(std::move(sym)), exchange_ts(ex_ts),
            ingress_ts(ing_ts), bids(std::move(b)), asks(std::move(a)) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "LTwoOrderBookEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", exchange_ts=" << format_optional_timestamp(exchange_ts)
                << ", ingress_ts=" << format_timestamp(ingress_ts)
                << ", bids_levels=" << bids.size()
                << ", asks_levels=" << asks.size() << ")";
            return oss.str();
        }
    };

    struct LimitOrderEvent : BaseEvent {
        SymbolType symbol;
        Side side;
        PriceType price;
        QuantityType quantity;
        Duration timeout;
        ClientOrderIdType client_order_id;

        LimitOrderEvent(
                Timestamp created_ts, SymbolType sym, Side s, PriceType p,
                QuantityType q, Duration t, ClientOrderIdType cid
        ) : BaseEvent(created_ts), symbol(std::move(sym)), side(s), price(p),
            quantity(q), timeout(t), client_order_id(cid) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "LimitOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol << ", side=" << side_to_string(side)
                << ", price=" << price << ", quantity=" << quantity
                << ", timeout=" << format_duration(timeout)
                << ", client_order_id=" << client_order_id << ")";
            return oss.str();
        }
    };

    struct MarketOrderEvent : BaseEvent {
        SymbolType symbol;
        Side side;
        QuantityType quantity;
        Duration timeout;
        ClientOrderIdType client_order_id;

        MarketOrderEvent(
                Timestamp created_ts, SymbolType sym, Side s, QuantityType q,
                Duration t, ClientOrderIdType cid
        ) : BaseEvent(created_ts), symbol(std::move(sym)), side(s),
            quantity(q), timeout(t), client_order_id(cid) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "MarketOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol << ", side=" << side_to_string(side)
                << ", quantity=" << quantity
                << ", timeout=" << format_duration(timeout)
                << ", client_order_id=" << client_order_id << ")";
            return oss.str();
        }
    };

    struct PartialCancelOrderEvent : BaseEvent {
        SymbolType symbol;
        ClientOrderIdType target_order_id;
        QuantityType cancel_qty;
        ClientOrderIdType client_order_id;

        PartialCancelOrderEvent(
                Timestamp created_ts, SymbolType sym, ClientOrderIdType target_cid,
                QuantityType cnl_qty, ClientOrderIdType req_cid
        ) : BaseEvent(created_ts), symbol(std::move(sym)),
            target_order_id(target_cid), cancel_qty(cnl_qty),
            client_order_id(req_cid) {}
        virtual ~PartialCancelOrderEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() << ", symbol=" << symbol
                << ", target_order_id=" << target_order_id
                << ", cancel_qty=" << cancel_qty
                << ", client_order_id=" << client_order_id;
            return oss.str();
        }
    };

    struct PartialCancelLimitOrderEvent : PartialCancelOrderEvent {
        PartialCancelLimitOrderEvent(
                Timestamp created_ts, SymbolType sym, ClientOrderIdType target_cid,
                QuantityType cnl_qty, ClientOrderIdType req_cid
        ) : PartialCancelOrderEvent(created_ts, std::move(sym), target_cid, cnl_qty, req_cid) {}
        std::string to_string() const override {
            return "PartialCancelLimitOrderEvent(" + PartialCancelOrderEvent::to_string() + ")";
        }
    };

    struct PartialCancelMarketOrderEvent : PartialCancelOrderEvent {
        PartialCancelMarketOrderEvent(
                Timestamp created_ts, SymbolType sym, ClientOrderIdType target_cid,
                QuantityType cnl_qty, ClientOrderIdType req_cid
        ) : PartialCancelOrderEvent(created_ts, std::move(sym), target_cid, cnl_qty, req_cid) {}
        std::string to_string() const override {
            return "PartialCancelMarketOrderEvent(" + PartialCancelOrderEvent::to_string() + ")";
        }
    };

    struct FullCancelOrderEvent : BaseEvent {
        SymbolType symbol;
        ClientOrderIdType target_order_id;
        ClientOrderIdType client_order_id;

        FullCancelOrderEvent(
                Timestamp created_ts, SymbolType sym, ClientOrderIdType target_cid, ClientOrderIdType req_cid
        ) : BaseEvent(created_ts), symbol(std::move(sym)),
            target_order_id(target_cid), client_order_id(req_cid) {}
        virtual ~FullCancelOrderEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() << ", symbol=" << symbol
                << ", target_order_id=" << target_order_id
                << ", client_order_id=" << client_order_id;
            return oss.str();
        }
    };

    struct FullCancelLimitOrderEvent : FullCancelOrderEvent {
        FullCancelLimitOrderEvent(
                Timestamp created_ts, SymbolType sym, ClientOrderIdType target_cid, ClientOrderIdType req_cid
        ) : FullCancelOrderEvent(created_ts, std::move(sym), target_cid, req_cid) {}
        std::string to_string() const override {
            return "FullCancelLimitOrderEvent(" + FullCancelOrderEvent::to_string() + ")";
        }
    };

    struct FullCancelMarketOrderEvent : FullCancelOrderEvent {
        FullCancelMarketOrderEvent(
                Timestamp created_ts, SymbolType sym, ClientOrderIdType target_cid, ClientOrderIdType req_cid
        ) : FullCancelOrderEvent(created_ts, std::move(sym), target_cid, req_cid) {}
        std::string to_string() const override {
            return "FullCancelMarketOrderEvent(" + FullCancelOrderEvent::to_string() + ")";
        }
    };

    struct BaseAckEvent : BaseEvent {
        ExchangeOrderIdType order_id;
        ClientOrderIdType client_order_id;
        Side side;
        QuantityType quantity;
        SymbolType symbol;

        BaseAckEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid,
                Side s, QuantityType qty, SymbolType sym
        ) : BaseEvent(created_ts), order_id(xid), client_order_id(cid),
            side(s), quantity(qty), symbol(std::move(sym)) {}
        virtual ~BaseAckEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() << ", order_id=" << order_id
                << ", client_order_id=" << client_order_id
                << ", side=" << side_to_string(side)
                << ", quantity=" << quantity
                << ", symbol=" << symbol;
            return oss.str();
        }
    };

    struct LimitOrderAckEvent : BaseAckEvent {
        PriceType limit_price;
        Duration timeout;
        AgentId original_trader_id; // <<<< NEWLY ADDED >>>>

        LimitOrderAckEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid,
                Side s, PriceType p, QuantityType qty, SymbolType sym, Duration t,
                AgentId orig_trader_id // <<<< NEWLY ADDED >>>>
        ) : BaseAckEvent(created_ts, xid, cid, s, qty, std::move(sym)),
            limit_price(p), timeout(t), original_trader_id(orig_trader_id) {} // <<<< INITIALIZE >>>>

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "LimitOrderAckEvent(" << BaseAckEvent::to_string()
                << ", limit_price=" << limit_price
                << ", timeout=" << format_duration(timeout)
                << ", original_trader_id=" << original_trader_id // <<<< ADD TO STRING >>>>
                << ")";
            return oss.str();
        }
    };

    struct MarketOrderAckEvent : BaseAckEvent {
        MarketOrderAckEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid,
                Side s, QuantityType qty, SymbolType sym
        ) : BaseAckEvent(created_ts, xid, cid, s, qty, std::move(sym)) {}
        std::string to_string() const override {
            return "MarketOrderAckEvent(" + BaseAckEvent::to_string() + ")";
        }
    };

    struct BaseCancelAckEvent : BaseAckEvent {
        ClientOrderIdType target_order_id;
        BaseCancelAckEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cancel_req_cid,
                Side s, ClientOrderIdType target_cid, QuantityType qty, SymbolType sym
        ) : BaseAckEvent(created_ts, xid, cancel_req_cid, s, qty, std::move(sym)),
            target_order_id(target_cid) {}
        virtual ~BaseCancelAckEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseAckEvent::to_string()
                << ", target_order_id=" << target_order_id;
            return oss.str();
        }
    };

    struct FullCancelOrderAckEvent : BaseCancelAckEvent {
        FullCancelOrderAckEvent(
                Timestamp created_ts, ExchangeOrderIdType original_xid, ClientOrderIdType cancel_req_cid,
                Side original_side, ClientOrderIdType target_cid, QuantityType cancelled_qty, SymbolType sym
        ) : BaseCancelAckEvent(created_ts, original_xid, cancel_req_cid, original_side, target_cid, cancelled_qty, std::move(sym)) {}
        virtual ~FullCancelOrderAckEvent() = default;
        std::string to_string() const override {
            return BaseCancelAckEvent::to_string(); // Derived classes will add name
        }
    };

    struct FullCancelLimitOrderAckEvent : FullCancelOrderAckEvent {
        FullCancelLimitOrderAckEvent(
                Timestamp created_ts, ExchangeOrderIdType original_xid, ClientOrderIdType cancel_req_cid,
                Side original_side, ClientOrderIdType target_cid, QuantityType cancelled_qty, SymbolType sym
        ) : FullCancelOrderAckEvent(created_ts, original_xid, cancel_req_cid, original_side, target_cid, cancelled_qty, std::move(sym)) {}
        std::string to_string() const override {
            return "FullCancelLimitOrderAckEvent(" + FullCancelOrderAckEvent::to_string() + ")";
        }
    };

    struct FullCancelMarketOrderAckEvent : FullCancelOrderAckEvent {
        FullCancelMarketOrderAckEvent(
                Timestamp created_ts, ExchangeOrderIdType original_xid, ClientOrderIdType cancel_req_cid,
                Side original_side, ClientOrderIdType target_cid, QuantityType cancelled_qty, SymbolType sym
        ) : FullCancelOrderAckEvent(created_ts, original_xid, cancel_req_cid, original_side, target_cid, cancelled_qty, std::move(sym)) {}
        std::string to_string() const override {
            return "FullCancelMarketOrderAckEvent(" + FullCancelOrderAckEvent::to_string() + ")";
        }
    };

    struct PartialCancelAckEvent : BaseCancelAckEvent {
        QuantityType cancelled_qty;
        QuantityType remaining_qty;
        PartialCancelAckEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cancel_req_cid, Side original_side,
                ClientOrderIdType target_cid, QuantityType original_qty, SymbolType sym,
                QuantityType cnl_qty, QuantityType rem_qty
        ) : BaseCancelAckEvent(created_ts, xid, cancel_req_cid, original_side, target_cid, original_qty, std::move(sym)),
            cancelled_qty(cnl_qty), remaining_qty(rem_qty) {}
        virtual ~PartialCancelAckEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string()
                << ", order_id=" << order_id << ", client_order_id=" << client_order_id
                << ", side=" << side_to_string(side) << ", original_quantity=" << quantity
                << ", symbol=" << symbol << ", target_order_id=" << target_order_id
                << ", cancelled_qty=" << cancelled_qty << ", remaining_qty=" << remaining_qty;
            return oss.str();
        }
    };

    struct PartialCancelLimitAckEvent : PartialCancelAckEvent {
        PartialCancelLimitAckEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cancel_req_cid, Side original_side,
                ClientOrderIdType target_cid, QuantityType original_qty, SymbolType sym, QuantityType cnl_qty, QuantityType rem_qty
        ) : PartialCancelAckEvent(created_ts, xid, cancel_req_cid, original_side, target_cid, original_qty, std::move(sym), cnl_qty, rem_qty) {}
        std::string to_string() const override {
            return "PartialCancelLimitAckEvent(" + PartialCancelAckEvent::to_string() + ")";
        }
    };

    struct PartialCancelMarketAckEvent : PartialCancelAckEvent {
        PartialCancelMarketAckEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cancel_req_cid, Side original_side,
                ClientOrderIdType target_cid, QuantityType original_qty, SymbolType sym, QuantityType cnl_qty, QuantityType rem_qty
        ) : PartialCancelAckEvent(created_ts, xid, cancel_req_cid, original_side, target_cid, original_qty, std::move(sym), cnl_qty, rem_qty) {}
        std::string to_string() const override {
            return "PartialCancelMarketAckEvent(" + PartialCancelAckEvent::to_string() + ")";
        }
    };

    struct BaseRejectEvent : BaseEvent {
        ClientOrderIdType client_order_id;
        SymbolType symbol;
        BaseRejectEvent(
                Timestamp created_ts, ClientOrderIdType rejected_cid, SymbolType sym
        ) : BaseEvent(created_ts), client_order_id(rejected_cid), symbol(std::move(sym)) {}
        virtual ~BaseRejectEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string()
                << ", client_order_id=" << client_order_id
                << ", symbol=" << symbol;
            return oss.str();
        }
    };

    struct PartialCancelLimitOrderRejectEvent : BaseRejectEvent {
        PartialCancelLimitOrderRejectEvent(Timestamp ts, ClientOrderIdType rej_cid, SymbolType sym)
                : BaseRejectEvent(ts, rej_cid, std::move(sym)) {}
        std::string to_string() const override {
            return "PartialCancelLimitOrderRejectEvent(" + BaseRejectEvent::to_string() + ")";
        }
    };

    struct FullCancelLimitOrderRejectEvent : BaseRejectEvent {
        FullCancelLimitOrderRejectEvent(Timestamp ts, ClientOrderIdType rej_cid, SymbolType sym)
                : BaseRejectEvent(ts, rej_cid, std::move(sym)) {}
        std::string to_string() const override {
            return "FullCancelLimitOrderRejectEvent(" + BaseRejectEvent::to_string() + ")";
        }
    };

    struct PartialCancelMarketOrderRejectEvent : BaseRejectEvent {
        PartialCancelMarketOrderRejectEvent(Timestamp ts, ClientOrderIdType rej_cid, SymbolType sym)
                : BaseRejectEvent(ts, rej_cid, std::move(sym)) {}
        std::string to_string() const override {
            return "PartialCancelMarketOrderRejectEvent(" + BaseRejectEvent::to_string() + ")";
        }
    };

    struct FullCancelMarketOrderRejectEvent : BaseRejectEvent {
        FullCancelMarketOrderRejectEvent(Timestamp ts, ClientOrderIdType rej_cid, SymbolType sym)
                : BaseRejectEvent(ts, rej_cid, std::move(sym)) {}
        std::string to_string() const override {
            return "FullCancelMarketOrderRejectEvent(" + BaseRejectEvent::to_string() + ")";
        }
    };

    struct LimitOrderRejectEvent : BaseRejectEvent {
        LimitOrderRejectEvent(Timestamp ts, ClientOrderIdType rej_cid, SymbolType sym)
                : BaseRejectEvent(ts, rej_cid, std::move(sym)) {}
        std::string to_string() const override {
            return "LimitOrderRejectEvent(" + BaseRejectEvent::to_string() + ")";
        }
    };

    struct MarketOrderRejectEvent : BaseRejectEvent {
        MarketOrderRejectEvent(Timestamp ts, ClientOrderIdType rej_cid, SymbolType sym)
                : BaseRejectEvent(ts, rej_cid, std::move(sym)) {}
        std::string to_string() const override {
            return "MarketOrderRejectEvent(" + BaseRejectEvent::to_string() + ")";
        }
    };

    struct BaseExpiredEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType order_id;
        ClientOrderIdType client_order_id;
        Side side;
        QuantityType quantity;
        BaseExpiredEvent(
                Timestamp created_ts, SymbolType sym, ExchangeOrderIdType xid,
                ClientOrderIdType cid, Side s, QuantityType rem_qty
        ) : BaseEvent(created_ts), symbol(std::move(sym)), order_id(xid),
            client_order_id(cid), side(s), quantity(rem_qty) {}
        virtual ~BaseExpiredEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() << ", symbol=" << symbol
                << ", order_id=" << order_id << ", client_order_id=" << client_order_id
                << ", side=" << side_to_string(side) << ", quantity=" << quantity;
            return oss.str();
        }
    };

    struct MarketOrderExpiredEvent : BaseExpiredEvent {
        MarketOrderExpiredEvent(Timestamp ts, SymbolType sym, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s, QuantityType rem_qty)
                : BaseExpiredEvent(ts, std::move(sym), xid, cid, s, rem_qty) {}
        std::string to_string() const override {
            return "MarketOrderExpiredEvent(" + BaseExpiredEvent::to_string() + ")";
        }
    };

    struct LimitOrderExpiredEvent : BaseExpiredEvent {
        PriceType limit_price;
        LimitOrderExpiredEvent(
                Timestamp created_ts, SymbolType sym, ExchangeOrderIdType xid,
                ClientOrderIdType cid, Side s, QuantityType rem_qty, PriceType p
        ) : BaseExpiredEvent(created_ts, std::move(sym), xid, cid, s, rem_qty), limit_price(p) {}
        std::string to_string() const override {
            std::ostringstream oss;
            oss << "LimitOrderExpiredEvent(" << BaseExpiredEvent::to_string()
                << ", limit_price=" << limit_price << ")";
            return oss.str();
        }
    };

    struct BaseFillEvent : BaseEvent {
        ExchangeOrderIdType order_id;
        ClientOrderIdType client_order_id;
        Side side;
        PriceType fill_price;
        QuantityType fill_qty;
        Timestamp fill_timestamp;
        SymbolType symbol;
        bool is_maker;
        BaseFillEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid,
                Side s, PriceType f_price, QuantityType f_qty, Timestamp f_ts,
                SymbolType sym, bool maker
        ) : BaseEvent(created_ts), order_id(xid), client_order_id(cid),
            side(s), fill_price(f_price), fill_qty(f_qty),
            fill_timestamp(f_ts), symbol(std::move(sym)), is_maker(maker) {}
        virtual ~BaseFillEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() << ", order_id=" << order_id
                << ", client_order_id=" << client_order_id
                << ", side=" << side_to_string(side) << ", fill_price=" << fill_price
                << ", fill_qty=" << fill_qty
                << ", fill_timestamp=" << format_timestamp(fill_timestamp)
                << ", symbol=" << symbol << ", is_maker=" << (is_maker ? "true" : "false");
            return oss.str();
        }
    };

    struct PartialFillEvent : BaseFillEvent {
        QuantityType leaves_qty;
        QuantityType cumulative_qty;
        AveragePriceType average_price;
        PartialFillEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
                PriceType f_price, QuantityType f_qty, Timestamp f_ts,
                SymbolType sym, bool maker,
                QuantityType leaves, QuantityType cum_qty, AveragePriceType avg_p
        ) : BaseFillEvent(created_ts, xid, cid, s, f_price, f_qty, f_ts, std::move(sym), maker),
            leaves_qty(leaves), cumulative_qty(cum_qty), average_price(avg_p) {}
        virtual ~PartialFillEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseFillEvent::to_string()
                << ", leaves_qty=" << leaves_qty << ", cumulative_qty=" << cumulative_qty
                << ", average_price=" << average_price;
            return oss.str();
        }
    };

    struct PartialFillLimitOrderEvent : PartialFillEvent {
        PartialFillLimitOrderEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
                PriceType f_price, QuantityType f_qty, Timestamp f_ts, SymbolType sym, bool maker,
                QuantityType leaves, QuantityType cum_qty, AveragePriceType avg_p
        ) : PartialFillEvent(created_ts, xid, cid, s, f_price, f_qty, f_ts, std::move(sym), maker, leaves, cum_qty, avg_p) {}
        std::string to_string() const override {
            return "PartialFillLimitOrderEvent(" + PartialFillEvent::to_string() + ")";
        }
    };

    struct PartialFillMarketOrderEvent : PartialFillEvent {
        PartialFillMarketOrderEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
                PriceType f_price, QuantityType f_qty, Timestamp f_ts, SymbolType sym, bool maker,
                QuantityType leaves, QuantityType cum_qty, AveragePriceType avg_p
        ) : PartialFillEvent(created_ts, xid, cid, s, f_price, f_qty, f_ts, std::move(sym), maker, leaves, cum_qty, avg_p) {}
        std::string to_string() const override {
            return "PartialFillMarketOrderEvent(" + PartialFillEvent::to_string() + ")";
        }
    };

    struct FullFillEvent : BaseFillEvent {
        AveragePriceType average_price;
        FullFillEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
                PriceType last_f_price, QuantityType last_f_qty, Timestamp f_ts,
                SymbolType sym, bool maker, AveragePriceType avg_p
        ) : BaseFillEvent(created_ts, xid, cid, s, last_f_price, last_f_qty, f_ts, std::move(sym), maker),
            average_price(avg_p) {}
        virtual ~FullFillEvent() = default;
        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseFillEvent::to_string()
                << ", average_price=" << average_price;
            return oss.str();
        }
    };

    struct FullFillLimitOrderEvent : FullFillEvent {
        FullFillLimitOrderEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
                PriceType last_f_price, QuantityType last_f_qty, Timestamp f_ts, SymbolType sym, bool maker,
                AveragePriceType avg_p
        ) : FullFillEvent(created_ts, xid, cid, s, last_f_price, last_f_qty, f_ts, std::move(sym), maker, avg_p) {}
        std::string to_string() const override {
            return "FullFillLimitOrderEvent(" + FullFillEvent::to_string() + ")";
        }
    };

    struct FullFillMarketOrderEvent : FullFillEvent {
        FullFillMarketOrderEvent(
                Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
                PriceType last_f_price, QuantityType last_f_qty, Timestamp f_ts, SymbolType sym, bool maker,
                AveragePriceType avg_p
        ) : FullFillEvent(created_ts, xid, cid, s, last_f_price, last_f_qty, f_ts, std::move(sym), maker, avg_p) {}
        std::string to_string() const override {
            return "FullFillMarketOrderEvent(" + FullFillEvent::to_string() + ")";
        }
    };

    struct TradeEvent : BaseEvent {
        SymbolType symbol;
        ClientOrderIdType maker_cid;
        ClientOrderIdType taker_cid;
        ExchangeOrderIdType maker_xid;
        ExchangeOrderIdType taker_xid;
        PriceType price;
        QuantityType quantity;
        Side maker_side;
        bool maker_exhausted;

        TradeEvent(
                Timestamp created_ts, SymbolType sym, ClientOrderIdType m_cid, ClientOrderIdType t_cid,
                ExchangeOrderIdType m_xid, ExchangeOrderIdType t_xid, PriceType p, QuantityType q,
                Side m_side, bool m_exhausted
        ) : BaseEvent(created_ts), symbol(std::move(sym)), maker_cid(m_cid), taker_cid(t_cid),
            maker_xid(m_xid), taker_xid(t_xid), price(p), quantity(q),
            maker_side(m_side), maker_exhausted(m_exhausted) {}
        std::string to_string() const override {
            std::ostringstream oss;
            oss << "TradeEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol << ", maker_cid=" << maker_cid << ", taker_cid=" << taker_cid
                << ", maker_xid=" << maker_xid << ", taker_xid=" << taker_xid
                << ", price=" << price << ", quantity=" << quantity
                << ", maker_side=" << side_to_string(maker_side)
                << ", maker_exhausted=" << (maker_exhausted ? "true" : "false") << ")";
            return oss.str();
        }
    };

    struct TriggerExpiredLimitOrderEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType target_exchange_order_id;
        Duration timeout_value;
        AgentId original_trader_id; // <<<< NEWLY ADDED >>>>

        TriggerExpiredLimitOrderEvent(
                Timestamp created_ts, SymbolType sym, ExchangeOrderIdType target_xid,
                Duration original_timeout, AgentId orig_trader_id // <<<< NEWLY ADDED >>>>
        ) : BaseEvent(created_ts), symbol(std::move(sym)),
            target_exchange_order_id(target_xid), timeout_value(original_timeout),
            original_trader_id(orig_trader_id) {} // <<<< INITIALIZE >>>>

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "TriggerExpiredLimitOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", target_exchange_order_id=" << target_exchange_order_id
                << ", timeout_value=" << format_duration(timeout_value)
                << ", original_trader_id=" << original_trader_id // <<<< ADD TO STRING >>>>
                << ")";
            return oss.str();
        }
    };

    struct RejectTriggerExpiredLimitOrderEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType target_exchange_order_id;
        Duration timeout_value;
        RejectTriggerExpiredLimitOrderEvent(
                Timestamp created_ts, SymbolType sym, ExchangeOrderIdType target_xid, Duration original_timeout
        ) : BaseEvent(created_ts), symbol(std::move(sym)),
            target_exchange_order_id(target_xid), timeout_value(original_timeout) {}
        std::string to_string() const override {
            std::ostringstream oss;
            oss << "RejectTriggerExpiredLimitOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", target_exchange_order_id=" << target_exchange_order_id
                << ", timeout_value=" << format_duration(timeout_value) << ")";
            return oss.str();
        }
    };

    struct AckTriggerExpiredLimitOrderEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType target_exchange_order_id;
        ClientOrderIdType client_order_id;
        PriceType price;
        QuantityType quantity;
        Duration timeout_value;

        AckTriggerExpiredLimitOrderEvent(
                Timestamp created_ts, SymbolType sym, ExchangeOrderIdType target_xid,
                ClientOrderIdType original_cid, PriceType original_price,
                QuantityType rem_qty, Duration original_timeout
        ) : BaseEvent(created_ts), symbol(std::move(sym)),
            target_exchange_order_id(target_xid), client_order_id(original_cid),
            price(original_price), quantity(rem_qty), timeout_value(original_timeout) {}
        std::string to_string() const override {
            std::ostringstream oss;
            oss << "AckTriggerExpiredLimitOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", target_exchange_order_id=" << target_exchange_order_id
                << ", client_order_id=" << client_order_id
                << ", price=" << price << ", quantity=" << quantity
                << ", timeout_value=" << format_duration(timeout_value) << ")";
            return oss.str();
        }
    };

} // namespace ModelEvents

using AllEventTypes = std::variant<
        std::shared_ptr<const ModelEvents::CheckLimitOrderExpirationEvent>,
        std::shared_ptr<const ModelEvents::Bang>,
        std::shared_ptr<const ModelEvents::LTwoOrderBookEvent>,
        std::shared_ptr<const ModelEvents::LimitOrderEvent>,
        std::shared_ptr<const ModelEvents::MarketOrderEvent>,
        std::shared_ptr<const ModelEvents::PartialCancelLimitOrderEvent>,
        std::shared_ptr<const ModelEvents::PartialCancelMarketOrderEvent>,
        std::shared_ptr<const ModelEvents::FullCancelLimitOrderEvent>,
        std::shared_ptr<const ModelEvents::FullCancelMarketOrderEvent>,
        std::shared_ptr<const ModelEvents::LimitOrderAckEvent>,
        std::shared_ptr<const ModelEvents::MarketOrderAckEvent>,
        std::shared_ptr<const ModelEvents::FullCancelLimitOrderAckEvent>,
        std::shared_ptr<const ModelEvents::FullCancelMarketOrderAckEvent>,
        std::shared_ptr<const ModelEvents::PartialCancelLimitAckEvent>,
        std::shared_ptr<const ModelEvents::PartialCancelMarketAckEvent>,
        std::shared_ptr<const ModelEvents::PartialCancelLimitOrderRejectEvent>,
        std::shared_ptr<const ModelEvents::FullCancelLimitOrderRejectEvent>,
        std::shared_ptr<const ModelEvents::PartialCancelMarketOrderRejectEvent>,
        std::shared_ptr<const ModelEvents::FullCancelMarketOrderRejectEvent>,
        std::shared_ptr<const ModelEvents::LimitOrderRejectEvent>,
        std::shared_ptr<const ModelEvents::MarketOrderRejectEvent>,
        std::shared_ptr<const ModelEvents::MarketOrderExpiredEvent>,
        std::shared_ptr<const ModelEvents::LimitOrderExpiredEvent>,
        std::shared_ptr<const ModelEvents::PartialFillLimitOrderEvent>,
        std::shared_ptr<const ModelEvents::PartialFillMarketOrderEvent>,
        std::shared_ptr<const ModelEvents::FullFillLimitOrderEvent>,
        std::shared_ptr<const ModelEvents::FullFillMarketOrderEvent>,
        std::shared_ptr<const ModelEvents::TradeEvent>,
        std::shared_ptr<const ModelEvents::TriggerExpiredLimitOrderEvent>,
        std::shared_ptr<const ModelEvents::RejectTriggerExpiredLimitOrderEvent>,
        std::shared_ptr<const ModelEvents::AckTriggerExpiredLimitOrderEvent>
>;

template<typename... ExtraEventTypes>
using ModelEventBus = EventBusSystem::TopicBasedEventBus<
        ModelEvents::CheckLimitOrderExpirationEvent, ModelEvents::Bang, ModelEvents::LTwoOrderBookEvent,
        ModelEvents::LimitOrderEvent, ModelEvents::MarketOrderEvent,
        ModelEvents::PartialCancelLimitOrderEvent, ModelEvents::PartialCancelMarketOrderEvent,
        ModelEvents::FullCancelLimitOrderEvent, ModelEvents::FullCancelMarketOrderEvent,
        ModelEvents::LimitOrderAckEvent, ModelEvents::MarketOrderAckEvent,
        ModelEvents::FullCancelLimitOrderAckEvent, ModelEvents::FullCancelMarketOrderAckEvent,
        ModelEvents::PartialCancelLimitAckEvent, ModelEvents::PartialCancelMarketAckEvent,
        ModelEvents::PartialCancelLimitOrderRejectEvent, ModelEvents::FullCancelLimitOrderRejectEvent,
        ModelEvents::PartialCancelMarketOrderRejectEvent, ModelEvents::FullCancelMarketOrderRejectEvent,
        ModelEvents::LimitOrderRejectEvent, ModelEvents::MarketOrderRejectEvent,
        ModelEvents::MarketOrderExpiredEvent, ModelEvents::LimitOrderExpiredEvent,
        ModelEvents::PartialFillLimitOrderEvent, ModelEvents::PartialFillMarketOrderEvent,
        ModelEvents::FullFillLimitOrderEvent, ModelEvents::FullFillMarketOrderEvent,
        ModelEvents::TradeEvent, ModelEvents::TriggerExpiredLimitOrderEvent,
        ModelEvents::RejectTriggerExpiredLimitOrderEvent, ModelEvents::AckTriggerExpiredLimitOrderEvent,
        ExtraEventTypes...
>;

template<typename ActualDerivedAgent, typename... ExtraEventTypes>
using ModelEventProcessor = EventBusSystem::EventProcessor<ActualDerivedAgent,
        ModelEvents::CheckLimitOrderExpirationEvent, ModelEvents::Bang, ModelEvents::LTwoOrderBookEvent,
        ModelEvents::LimitOrderEvent, ModelEvents::MarketOrderEvent,
        ModelEvents::PartialCancelLimitOrderEvent, ModelEvents::PartialCancelMarketOrderEvent,
        ModelEvents::FullCancelLimitOrderEvent, ModelEvents::FullCancelMarketOrderEvent,
        ModelEvents::LimitOrderAckEvent, ModelEvents::MarketOrderAckEvent,
        ModelEvents::FullCancelLimitOrderAckEvent, ModelEvents::FullCancelMarketOrderAckEvent,
        ModelEvents::PartialCancelLimitAckEvent, ModelEvents::PartialCancelMarketAckEvent,
        ModelEvents::PartialCancelLimitOrderRejectEvent, ModelEvents::FullCancelLimitOrderRejectEvent,
        ModelEvents::PartialCancelMarketOrderRejectEvent, ModelEvents::FullCancelMarketOrderRejectEvent,
        ModelEvents::LimitOrderRejectEvent, ModelEvents::MarketOrderRejectEvent,
        ModelEvents::MarketOrderExpiredEvent, ModelEvents::LimitOrderExpiredEvent,
        ModelEvents::PartialFillLimitOrderEvent, ModelEvents::PartialFillMarketOrderEvent,
        ModelEvents::FullFillLimitOrderEvent, ModelEvents::FullFillMarketOrderEvent,
        ModelEvents::TradeEvent, ModelEvents::TriggerExpiredLimitOrderEvent,
        ModelEvents::RejectTriggerExpiredLimitOrderEvent, ModelEvents::AckTriggerExpiredLimitOrderEvent,
        ExtraEventTypes...
>;