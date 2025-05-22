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

        // Static counter for unique IDs (simple, performant for single thread)
        // If multiple threads *create* events, this needs proper atomic handling
        // or a different UUID generation mechanism. For simulation bus, likely okay.
        static inline std::atomic<EventIdType> next_event_id{1};

        // Constructor taking the creation timestamp
        explicit BaseEvent(Timestamp ts) : event_id(next_event_id.fetch_add(1, std::memory_order_relaxed)), created_ts(ts) {}

        // Virtual destructor is crucial for safe deletion through base pointers
        virtual ~BaseEvent() = default;

        // --- Prevent Copying (Events often passed by shared_ptr) ---
        BaseEvent(const BaseEvent&) = delete;
        BaseEvent& operator=(const BaseEvent&) = delete;

        // --- Allow Moving ---
        BaseEvent(BaseEvent&&) = default;
        BaseEvent& operator=(BaseEvent&&) = default;


        // Virtual method for string representation (override in derived classes)
        virtual std::string to_string() const {
            std::ostringstream oss;
            // Get class name (platform-dependent, use carefully or define manually)
            // For simplicity, let derived classes prepend their name.
            oss << "event_id=" << event_id
                << ", created_ts=" << format_timestamp(created_ts);
            return oss.str();
        }
    };


    // ------------------------------------------------------------------
    // Specific Events (Translating from Python model.py)
    // ------------------------------------------------------------------

    struct CheckLimitOrderExpirationEvent : BaseEvent {
        ExchangeOrderIdType target_exchange_order_id;
        Duration original_timeout; // Changed from Timestamp expiration_time

        CheckLimitOrderExpirationEvent(
            Timestamp created_ts,
            ExchangeOrderIdType target_xid,
            Duration original_order_timeout // Changed parameter
        )
            : BaseEvent(created_ts),
              target_exchange_order_id(target_xid),
              original_timeout(original_order_timeout) {} // Assign new member

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "CheckLimitOrderExpirationEvent(" << BaseEvent::to_string()
                << ", target_exchange_order_id=" << target_exchange_order_id
                << ", original_timeout=" << format_duration(original_timeout) << ")"; // Updated field name
            return oss.str();
        }
    };


    struct Bang : BaseEvent {
        explicit Bang(Timestamp created_ts) : BaseEvent(created_ts) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "Bang(" << BaseEvent::to_string() << ")";
            return oss.str();
        }
    };

    // ------------------------------------------------------------------
    // L2 Order Book Event
    // ------------------------------------------------------------------
    struct LTwoOrderBookEvent : BaseEvent {
        SymbolType symbol;
        std::optional<Timestamp> exchange_ts; // Exchange-provided timestamp
        Timestamp ingress_ts;                // Local ingestion timestamp
        OrderBookLevel bids;                 // Vector of {price, qty} pairs
        OrderBookLevel asks;                 // Vector of {price, qty} pairs

        LTwoOrderBookEvent(
            Timestamp created_ts,
            SymbolType sym,
            std::optional<Timestamp> ex_ts,
            Timestamp ing_ts,
            OrderBookLevel b, // Use move semantics for vectors
            OrderBookLevel a
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            exchange_ts(ex_ts),
            ingress_ts(ing_ts),
            bids(std::move(b)),
            asks(std::move(a))
        {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "LTwoOrderBookEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", exchange_ts=" << format_optional_timestamp(exchange_ts)
                << ", ingress_ts=" << format_timestamp(ingress_ts)
                << ", bids_levels=" << bids.size() // Show level count for brevity
                << ", asks_levels=" << asks.size() << ")";
                // Could add top level details if needed:
                // << ", top_bid=[" << (bids.empty() ? 0 : bids[0].first) << "," << (bids.empty() ? 0 : bids[0].second) << "]"
                // << ", top_ask=[" << (asks.empty() ? 0 : asks[0].first) << "," << (asks.empty() ? 0 : asks[0].second) << "]"
            return oss.str();
        }
    };

    // ------------------------------------------------------------------------------
    // Order Request Events
    // ------------------------------------------------------------------------------
    struct LimitOrderEvent : BaseEvent {
        SymbolType symbol;
        Side side;
        PriceType price;
        QuantityType quantity;
        Duration timeout; // Validity duration
        ClientOrderIdType client_order_id;

        LimitOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            Side s,
            PriceType p,
            QuantityType q,
            Duration t,
            ClientOrderIdType cid
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            side(s),
            price(p),
            quantity(q),
            timeout(t),
            client_order_id(cid)
        {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "LimitOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", side=" << side_to_string(side)
                << ", price=" << price
                << ", quantity=" << quantity
                << ", timeout=" << format_duration(timeout)
                << ", client_order_id=" << client_order_id << ")";
            return oss.str();
        }
    };

    struct MarketOrderEvent : BaseEvent {
        SymbolType symbol;
        Side side;
        QuantityType quantity;
        Duration timeout; // Validity duration
        ClientOrderIdType client_order_id;

        MarketOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            Side s,
            QuantityType q,
            Duration t,
            ClientOrderIdType cid
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            side(s),
            quantity(q),
            timeout(t),
            client_order_id(cid)
        {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "MarketOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", side=" << side_to_string(side)
                << ", quantity=" << quantity
                << ", timeout=" << format_duration(timeout)
                << ", client_order_id=" << client_order_id << ")";
            return oss.str();
        }
    };

    // ------------------------------------------------------------------------------
    // Partial Cancel Request Base
    // ------------------------------------------------------------------------------
    struct PartialCancelOrderEvent : BaseEvent {
        SymbolType symbol;
        ClientOrderIdType target_order_id; // CID of the order to cancel
        QuantityType cancel_qty;
        ClientOrderIdType client_order_id; // CID of this cancel request itself

        // Protected constructor for base class
        PartialCancelOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ClientOrderIdType target_cid,
            QuantityType cnl_qty,
            ClientOrderIdType req_cid
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            target_order_id(target_cid),
            cancel_qty(cnl_qty),
            client_order_id(req_cid)
        {}

         // Make destructor virtual as it's a base class
        virtual ~PartialCancelOrderEvent() = default;

        std::string to_string() const override {
             // Base implementation for common fields
            std::ostringstream oss;
            oss << BaseEvent::to_string() // Don't prepend class name here, let derived do it
                << ", symbol=" << symbol
                << ", target_order_id=" << target_order_id
                << ", cancel_qty=" << cancel_qty
                << ", client_order_id=" << client_order_id;
            return oss.str();
        }
    };

    struct PartialCancelLimitOrderEvent : PartialCancelOrderEvent {
         PartialCancelLimitOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ClientOrderIdType target_cid,
            QuantityType cnl_qty,
            ClientOrderIdType req_cid
        ) : PartialCancelOrderEvent(created_ts, std::move(sym), target_cid, cnl_qty, req_cid) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "PartialCancelLimitOrderEvent(" << PartialCancelOrderEvent::to_string() << ")";
            return oss.str();
        }
    };

    struct PartialCancelMarketOrderEvent : PartialCancelOrderEvent {
        PartialCancelMarketOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ClientOrderIdType target_cid,
            QuantityType cnl_qty,
            ClientOrderIdType req_cid
        ) : PartialCancelOrderEvent(created_ts, std::move(sym), target_cid, cnl_qty, req_cid) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "PartialCancelMarketOrderEvent(" << PartialCancelOrderEvent::to_string() << ")";
            return oss.str();
        }
    };


    // ------------------------------------------------------------------------------
    // Full Cancel Request Base
    // ------------------------------------------------------------------------------
    struct FullCancelOrderEvent : BaseEvent {
        SymbolType symbol;
        ClientOrderIdType target_order_id; // CID of the order to cancel
        ClientOrderIdType client_order_id; // CID of this cancel request itself

        // Protected constructor for base class
        FullCancelOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ClientOrderIdType target_cid,
            ClientOrderIdType req_cid
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            target_order_id(target_cid),
            client_order_id(req_cid)
        {}

        // Make destructor virtual as it's a base class
        virtual ~FullCancelOrderEvent() = default;

        std::string to_string() const override {
            // Base implementation for common fields
            std::ostringstream oss;
            oss << BaseEvent::to_string() // Don't prepend class name here
                << ", symbol=" << symbol
                << ", target_order_id=" << target_order_id
                << ", client_order_id=" << client_order_id;
            return oss.str();
        }
    };

     struct FullCancelLimitOrderEvent : FullCancelOrderEvent {
        FullCancelLimitOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ClientOrderIdType target_cid,
            ClientOrderIdType req_cid
        ) : FullCancelOrderEvent(created_ts, std::move(sym), target_cid, req_cid) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "FullCancelLimitOrderEvent(" << FullCancelOrderEvent::to_string() << ")";
            return oss.str();
        }
    };

    struct FullCancelMarketOrderEvent : FullCancelOrderEvent {
        FullCancelMarketOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ClientOrderIdType target_cid,
            ClientOrderIdType req_cid
        ) : FullCancelOrderEvent(created_ts, std::move(sym), target_cid, req_cid) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "FullCancelMarketOrderEvent(" << FullCancelOrderEvent::to_string() << ")";
            return oss.str();
        }
    };

    // ------------------------------------------------------------------------------
    // Acknowledgement Events Base
    // ------------------------------------------------------------------------------
    struct BaseAckEvent : BaseEvent {
        ExchangeOrderIdType order_id;        // XID assigned by exchange to the *original* order or *this request*
        ClientOrderIdType client_order_id; // CID echoed back (of original order or this request)
        Side side;
        QuantityType quantity;             // Meaning depends on derived type (e.g., acked qty, cancelled qty, original qty)
        SymbolType symbol;

        // Protected constructor
        BaseAckEvent(
            Timestamp created_ts,
            ExchangeOrderIdType xid,
            ClientOrderIdType cid,
            Side s,
            QuantityType qty, // Meaning varies
            SymbolType sym
        ) : BaseEvent(created_ts),
            order_id(xid),
            client_order_id(cid),
            side(s),
            quantity(qty),
            symbol(std::move(sym))
        {}

        virtual ~BaseAckEvent() = default; // Virtual destructor

         std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() // Don't prepend class name here
                << ", order_id=" << order_id
                << ", client_order_id=" << client_order_id
                << ", side=" << side_to_string(side)
                << ", quantity=" << quantity // Note: Quantity meaning varies!
                << ", symbol=" << symbol;
            return oss.str();
         }
    };

    // --- Specific ACK Events ---

    struct LimitOrderAckEvent : BaseAckEvent {
        PriceType limit_price;
        Duration timeout; // Original timeout duration echoed back

        LimitOrderAckEvent(
            Timestamp created_ts,
            ExchangeOrderIdType xid, // XID of the newly accepted limit order
            ClientOrderIdType cid, // CID of the limit order request
            Side s,
            PriceType p,
            QuantityType qty, // Quantity acknowledged
            SymbolType sym,
            Duration t
        ) : BaseAckEvent(created_ts, xid, cid, s, qty, std::move(sym)),
            limit_price(p),
            timeout(t)
        {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "LimitOrderAckEvent(" << BaseAckEvent::to_string()
                << ", limit_price=" << limit_price
                << ", timeout=" << format_duration(timeout) << ")";
            return oss.str();
        }
    };

    struct MarketOrderAckEvent : BaseAckEvent {
         MarketOrderAckEvent(
            Timestamp created_ts,
            ExchangeOrderIdType xid, // XID of the newly accepted market order
            ClientOrderIdType cid, // CID of the market order request
            Side s,
            QuantityType qty, // Quantity acknowledged
            SymbolType sym
        ) : BaseAckEvent(created_ts, xid, cid, s, qty, std::move(sym)) {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "MarketOrderAckEvent(" << BaseAckEvent::to_string() << ")";
            return oss.str();
        }
    };

    // --- Cancel Ack Base ---
    struct BaseCancelAckEvent : BaseAckEvent {
         ClientOrderIdType target_order_id; // CID of the order that was targeted for cancellation

         // Protected constructor
         BaseCancelAckEvent(
            Timestamp created_ts,
            ExchangeOrderIdType xid,       // XID related to the cancel ack (might be 0 or original XID)
            ClientOrderIdType cancel_req_cid, // CID of the cancel request being acked
            Side s,                      // Side of the original order
            ClientOrderIdType target_cid,    // CID of the original order
            QuantityType qty,            // Quantity affected (e.g., cancelled amount)
            SymbolType sym
         ) : BaseAckEvent(created_ts, xid, cancel_req_cid, s, qty, std::move(sym)),
             target_order_id(target_cid)
        {}

        virtual ~BaseCancelAckEvent() = default;

         std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseAckEvent::to_string() // Don't prepend class name here
                << ", target_order_id=" << target_order_id;
            return oss.str();
         }
    };


    struct FullCancelOrderAckEvent : BaseCancelAckEvent {
        // Inherits target_order_id
        // BaseAckEvent::quantity represents the quantity fully cancelled.
        // BaseAckEvent::client_order_id is the CID of the full cancel request.
        // BaseAckEvent::order_id is often the XID of the original order being cancelled.

         FullCancelOrderAckEvent(
            Timestamp created_ts,
            ExchangeOrderIdType original_xid, // XID of the cancelled order
            ClientOrderIdType cancel_req_cid, // CID of the cancel request
            Side original_side,
            ClientOrderIdType target_cid,    // CID of the cancelled order
            QuantityType cancelled_qty,    // Quantity confirmed cancelled
            SymbolType sym
        ) : BaseCancelAckEvent(created_ts, original_xid, cancel_req_cid, original_side, target_cid, cancelled_qty, std::move(sym))
        {}

        virtual ~FullCancelOrderAckEvent() = default; // Virtual destructor for inheritance

        std::string to_string() const override {
             std::ostringstream oss;
            // Clarify quantity meaning in string if needed
            oss << BaseCancelAckEvent::to_string(); // Don't prepend class name
            // oss << ", cancelled_qty=" << quantity; // Redundant if Base class prints 'quantity'
            return oss.str();
        }
    };

    struct FullCancelLimitOrderAckEvent : FullCancelOrderAckEvent {
         FullCancelLimitOrderAckEvent(
            Timestamp created_ts, ExchangeOrderIdType original_xid, ClientOrderIdType cancel_req_cid,
            Side original_side, ClientOrderIdType target_cid, QuantityType cancelled_qty, SymbolType sym
        ) : FullCancelOrderAckEvent(created_ts, original_xid, cancel_req_cid, original_side, target_cid, cancelled_qty, std::move(sym))
        {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "FullCancelLimitOrderAckEvent(" << FullCancelOrderAckEvent::to_string() << ")";
            return oss.str();
        }
    };

    struct FullCancelMarketOrderAckEvent : FullCancelOrderAckEvent {
        FullCancelMarketOrderAckEvent(
            Timestamp created_ts, ExchangeOrderIdType original_xid, ClientOrderIdType cancel_req_cid,
            Side original_side, ClientOrderIdType target_cid, QuantityType cancelled_qty, SymbolType sym
        ) : FullCancelOrderAckEvent(created_ts, original_xid, cancel_req_cid, original_side, target_cid, cancelled_qty, std::move(sym))
        {}

         std::string to_string() const override {
            std::ostringstream oss;
            oss << "FullCancelMarketOrderAckEvent(" << FullCancelOrderAckEvent::to_string() << ")";
            return oss.str();
        }
    };

    // --- Partial Cancel Ack ---
    struct PartialCancelAckEvent : BaseCancelAckEvent {
        QuantityType cancelled_qty;
        QuantityType remaining_qty;
        // Inherits target_order_id (CID of original order) from BaseCancelAckEvent
        // BaseAckEvent::quantity field holds the *original* quantity (before cancel) in this context
        // BaseAckEvent::client_order_id is the CID of the partial cancel request.
        // BaseAckEvent::order_id is often 0 or the XID of the original order.

         PartialCancelAckEvent(
            Timestamp created_ts,
            ExchangeOrderIdType xid,           // XID related to ack (often 0 or original XID)
            ClientOrderIdType cancel_req_cid, // CID of the partial cancel request
            Side original_side,
            ClientOrderIdType target_cid,    // CID of the original order
            QuantityType original_qty,     // Original quantity of the order
            SymbolType sym,
            QuantityType cnl_qty,          // Quantity confirmed cancelled
            QuantityType rem_qty           // Quantity remaining on the order
        ) : BaseCancelAckEvent(created_ts, xid, cancel_req_cid, original_side, target_cid, original_qty, std::move(sym)),
            cancelled_qty(cnl_qty),
            remaining_qty(rem_qty)
        {}

        virtual ~PartialCancelAckEvent() = default; // Virtual destructor

        std::string to_string() const override {
            std::ostringstream oss;
            // Call base, but clarify the 'quantity' field meaning from base
            oss << BaseEvent::to_string() // Call BaseEvent directly to control output order
                << ", order_id=" << order_id
                << ", client_order_id=" << client_order_id // Cancel request CID
                << ", side=" << side_to_string(side)
                << ", original_quantity=" << quantity // Quantity from BaseAck means original here
                << ", symbol=" << symbol
                << ", target_order_id=" << target_order_id // Original order CID
                << ", cancelled_qty=" << cancelled_qty
                << ", remaining_qty=" << remaining_qty;
            return oss.str();
        }
    };


    struct PartialCancelLimitAckEvent : PartialCancelAckEvent {
         PartialCancelLimitAckEvent(
            Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cancel_req_cid, Side original_side,
            ClientOrderIdType target_cid, QuantityType original_qty, SymbolType sym, QuantityType cnl_qty, QuantityType rem_qty
        ) : PartialCancelAckEvent(created_ts, xid, cancel_req_cid, original_side, target_cid, original_qty, std::move(sym), cnl_qty, rem_qty)
        {}

         std::string to_string() const override {
            std::ostringstream oss;
            oss << "PartialCancelLimitAckEvent(" << PartialCancelAckEvent::to_string() << ")";
            return oss.str();
         }
    };

    struct PartialCancelMarketAckEvent : PartialCancelAckEvent {
         PartialCancelMarketAckEvent(
            Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cancel_req_cid, Side original_side,
            ClientOrderIdType target_cid, QuantityType original_qty, SymbolType sym, QuantityType cnl_qty, QuantityType rem_qty
        ) : PartialCancelAckEvent(created_ts, xid, cancel_req_cid, original_side, target_cid, original_qty, std::move(sym), cnl_qty, rem_qty)
        {}

         std::string to_string() const override {
            std::ostringstream oss;
            oss << "PartialCancelMarketAckEvent(" << PartialCancelAckEvent::to_string() << ")";
            return oss.str();
         }
    };


    // ------------------------------------------------------------------------------
    // Reject Events Base
    // ------------------------------------------------------------------------------
    struct BaseRejectEvent : BaseEvent {
        ClientOrderIdType client_order_id; // Client ID of the request that was rejected
        SymbolType symbol;
        // Consider adding a ReasonCode enum or string field

        // Protected constructor
        BaseRejectEvent(
            Timestamp created_ts,
            ClientOrderIdType rejected_cid,
            SymbolType sym
        ) : BaseEvent(created_ts),
            client_order_id(rejected_cid),
            symbol(std::move(sym))
        {}

        virtual ~BaseRejectEvent() = default;

        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() // Don't prepend class name here
                << ", client_order_id=" << client_order_id // Rejected CID
                << ", symbol=" << symbol;
            return oss.str();
        }
    };

    // --- Specific Reject Events (Mostly just inherit and change name) ---

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

    // Add rejects for initial LimitOrder, MarketOrder if needed
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


    // ------------------------------------------------------------------
    // Expired Events Base
    // ------------------------------------------------------------------
    struct BaseExpiredEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType order_id;       // XID of the expired order
        ClientOrderIdType client_order_id; // CID of the expired order
        Side side;
        QuantityType quantity;             // Remaining quantity when expired

        // Protected constructor
        BaseExpiredEvent(
            Timestamp created_ts,
            SymbolType sym,
            ExchangeOrderIdType xid,
            ClientOrderIdType cid,
            Side s,
            QuantityType rem_qty
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            order_id(xid),
            client_order_id(cid),
            side(s),
            quantity(rem_qty)
        {}

        virtual ~BaseExpiredEvent() = default;

        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() // Don't prepend class name
                << ", symbol=" << symbol
                << ", order_id=" << order_id
                << ", client_order_id=" << client_order_id
                << ", side=" << side_to_string(side)
                << ", quantity=" << quantity; // Remaining quantity
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
        PriceType limit_price; // Original limit price

        LimitOrderExpiredEvent(
            Timestamp created_ts,
            SymbolType sym,
            ExchangeOrderIdType xid,
            ClientOrderIdType cid,
            Side s,
            QuantityType rem_qty,
            PriceType p
        ) : BaseExpiredEvent(created_ts, std::move(sym), xid, cid, s, rem_qty),
            limit_price(p)
        {}

        std::string to_string() const override {
             std::ostringstream oss;
             oss << "LimitOrderExpiredEvent(" << BaseExpiredEvent::to_string()
                 << ", limit_price=" << limit_price << ")";
             return oss.str();
        }
    };


    // ------------------------------------------------------------------
    // Fill Events Base
    // ------------------------------------------------------------------
    struct BaseFillEvent : BaseEvent {
        ExchangeOrderIdType order_id;       // XID of the filled order
        ClientOrderIdType client_order_id; // CID of the filled order
        Side side;
        PriceType fill_price;              // Price of this specific fill
        QuantityType fill_qty;             // Quantity of this specific fill
        Timestamp fill_timestamp;          // Timestamp of this fill
        SymbolType symbol;
        bool is_maker;                     // Was this fill a maker fill?

         // Protected constructor
         BaseFillEvent(
            Timestamp created_ts, // Timestamp event was created locally
            ExchangeOrderIdType xid,
            ClientOrderIdType cid,
            Side s,
            PriceType f_price,
            QuantityType f_qty,
            Timestamp f_ts,     // Actual fill timestamp from exchange/matching engine
            SymbolType sym,
            bool maker
        ) : BaseEvent(created_ts),
            order_id(xid),
            client_order_id(cid),
            side(s),
            fill_price(f_price),
            fill_qty(f_qty),
            fill_timestamp(f_ts),
            symbol(std::move(sym)),
            is_maker(maker)
        {}

        virtual ~BaseFillEvent() = default;

        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseEvent::to_string() // Don't prepend class name
                << ", order_id=" << order_id
                << ", client_order_id=" << client_order_id
                << ", side=" << side_to_string(side)
                << ", fill_price=" << fill_price
                << ", fill_qty=" << fill_qty
                << ", fill_timestamp=" << format_timestamp(fill_timestamp)
                << ", symbol=" << symbol
                << ", is_maker=" << (is_maker ? "true" : "false");
            return oss.str();
        }
    };


    // --- Partial Fill ---
    struct PartialFillEvent : BaseFillEvent {
        QuantityType leaves_qty;          // Quantity remaining after this fill
        QuantityType cumulative_qty;      // Total quantity filled so far for this order
        AveragePriceType average_price;   // Average fill price so far for this order

        // Protected constructor
        PartialFillEvent(
            Timestamp created_ts,
            ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
            PriceType f_price, QuantityType f_qty, Timestamp f_ts,
            SymbolType sym, bool maker,
            QuantityType leaves, QuantityType cum_qty, AveragePriceType avg_p
        ) : BaseFillEvent(created_ts, xid, cid, s, f_price, f_qty, f_ts, std::move(sym), maker),
            leaves_qty(leaves),
            cumulative_qty(cum_qty),
            average_price(avg_p)
        {}

        virtual ~PartialFillEvent() = default;

        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseFillEvent::to_string() // Don't prepend class name
                << ", leaves_qty=" << leaves_qty
                << ", cumulative_qty=" << cumulative_qty
                << ", average_price=" << average_price; // Consider precision formatting
            return oss.str();
        }
    };

    struct PartialFillLimitOrderEvent : PartialFillEvent {
         PartialFillLimitOrderEvent(
            Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
            PriceType f_price, QuantityType f_qty, Timestamp f_ts, SymbolType sym, bool maker,
            QuantityType leaves, QuantityType cum_qty, AveragePriceType avg_p
        ) : PartialFillEvent(created_ts, xid, cid, s, f_price, f_qty, f_ts, std::move(sym), maker, leaves, cum_qty, avg_p)
        {}

        std::string to_string() const override {
            return "PartialFillLimitOrderEvent(" + PartialFillEvent::to_string() + ")";
        }
    };

    struct PartialFillMarketOrderEvent : PartialFillEvent {
         PartialFillMarketOrderEvent(
            Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
            PriceType f_price, QuantityType f_qty, Timestamp f_ts, SymbolType sym, bool maker,
            QuantityType leaves, QuantityType cum_qty, AveragePriceType avg_p
        ) : PartialFillEvent(created_ts, xid, cid, s, f_price, f_qty, f_ts, std::move(sym), maker, leaves, cum_qty, avg_p)
        {}

        std::string to_string() const override {
            return "PartialFillMarketOrderEvent(" + PartialFillEvent::to_string() + ")";
        }
    };


    // --- Full Fill ---
    struct FullFillEvent : BaseFillEvent {
        AveragePriceType average_price; // Final average price for the whole order
        // BaseFillEvent::fill_price/fill_qty represent the *last* fill component

        // Protected constructor
        FullFillEvent(
            Timestamp created_ts,
            ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
            PriceType last_f_price, QuantityType last_f_qty, Timestamp f_ts,
            SymbolType sym, bool maker,
            AveragePriceType avg_p
        ) : BaseFillEvent(created_ts, xid, cid, s, last_f_price, last_f_qty, f_ts, std::move(sym), maker),
            average_price(avg_p)
        {}

        virtual ~FullFillEvent() = default;

        std::string to_string() const override {
            std::ostringstream oss;
            oss << BaseFillEvent::to_string() // Don't prepend class name
                << ", average_price=" << average_price;
            return oss.str();
        }
    };

    struct FullFillLimitOrderEvent : FullFillEvent {
         FullFillLimitOrderEvent(
            Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
            PriceType last_f_price, QuantityType last_f_qty, Timestamp f_ts, SymbolType sym, bool maker,
            AveragePriceType avg_p
        ) : FullFillEvent(created_ts, xid, cid, s, last_f_price, last_f_qty, f_ts, std::move(sym), maker, avg_p)
        {}

        std::string to_string() const override {
            return "FullFillLimitOrderEvent(" + FullFillEvent::to_string() + ")";
        }
    };

     struct FullFillMarketOrderEvent : FullFillEvent {
         FullFillMarketOrderEvent(
            Timestamp created_ts, ExchangeOrderIdType xid, ClientOrderIdType cid, Side s,
            PriceType last_f_price, QuantityType last_f_qty, Timestamp f_ts, SymbolType sym, bool maker,
            AveragePriceType avg_p
        ) : FullFillEvent(created_ts, xid, cid, s, last_f_price, last_f_qty, f_ts, std::move(sym), maker, avg_p)
        {}

        std::string to_string() const override {
            return "FullFillMarketOrderEvent(" + FullFillEvent::to_string() + ")";
        }
    };

    // ------------------------------------------------------------------
    // Trade Event (From Matching Engine Perspective)
    // ------------------------------------------------------------------
    struct TradeEvent : BaseEvent {
        SymbolType symbol;
        ClientOrderIdType maker_cid;
        ClientOrderIdType taker_cid;
        ExchangeOrderIdType maker_xid;
        ExchangeOrderIdType taker_xid;
        PriceType price;
        QuantityType quantity;
        Side maker_side; // Side of the maker order
        bool maker_exhausted; // Was the maker order fully filled by this trade?

        TradeEvent(
            Timestamp created_ts,
            SymbolType sym,
            ClientOrderIdType m_cid, ClientOrderIdType t_cid,
            ExchangeOrderIdType m_xid, ExchangeOrderIdType t_xid,
            PriceType p, QuantityType q,
            Side m_side, bool m_exhausted
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            maker_cid(m_cid), taker_cid(t_cid),
            maker_xid(m_xid), taker_xid(t_xid),
            price(p), quantity(q),
            maker_side(m_side), maker_exhausted(m_exhausted)
        {}

        std::string to_string() const override {
             std::ostringstream oss;
             oss << "TradeEvent(" << BaseEvent::to_string()
                 << ", symbol=" << symbol
                 << ", maker_cid=" << maker_cid << ", taker_cid=" << taker_cid
                 << ", maker_xid=" << maker_xid << ", taker_xid=" << taker_xid
                 << ", price=" << price << ", quantity=" << quantity
                 << ", maker_side=" << side_to_string(maker_side)
                 << ", maker_exhausted=" << (maker_exhausted ? "true" : "false") << ")";
             return oss.str();
        }
    };

    // ------------------------------------------------------------------
    // Order Expiration Trigger / Ack / Reject (Workflow between CancelFairy and Exchange Adapter)
    // ------------------------------------------------------------------

    // Event sent FROM CancelFairy TO Exchange Adapter to trigger cancellation due to timeout
    struct TriggerExpiredLimitOrderEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType target_exchange_order_id; // XID of the order to cancel
        Duration timeout_value; // The original timeout duration that triggered this

        TriggerExpiredLimitOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ExchangeOrderIdType target_xid,
            Duration original_timeout
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            target_exchange_order_id(target_xid),
            timeout_value(original_timeout)
        {}

         std::string to_string() const override {
            std::ostringstream oss;
            oss << "TriggerExpiredLimitOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", target_exchange_order_id=" << target_exchange_order_id
                << ", timeout_value=" << format_duration(timeout_value) << ")";
            return oss.str();
        }
    };

    // Event sent FROM Exchange Adapter TO CancelFairy if expiration trigger is REJECTED
    // (e.g., order already filled/cancelled)
    struct RejectTriggerExpiredLimitOrderEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType target_exchange_order_id; // XID of the order that failed to expire
        Duration timeout_value; // Original timeout value echoed back
        // Could add reject reason code/string

        RejectTriggerExpiredLimitOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ExchangeOrderIdType target_xid,
            Duration original_timeout
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            target_exchange_order_id(target_xid),
            timeout_value(original_timeout)
        {}

        std::string to_string() const override {
            std::ostringstream oss;
            oss << "RejectTriggerExpiredLimitOrderEvent(" << BaseEvent::to_string()
                << ", symbol=" << symbol
                << ", target_exchange_order_id=" << target_exchange_order_id
                << ", timeout_value=" << format_duration(timeout_value) << ")";
            return oss.str();
        }
    };


    // Event sent FROM Exchange Adapter TO Trader/CancelFairy if expiration trigger is ACKNOWLEDGED
    // This implies the order *was* successfully cancelled due to the timeout trigger.
    // It's similar to LimitOrderExpiredEvent but part of the trigger workflow.
    struct AckTriggerExpiredLimitOrderEvent : BaseEvent {
        SymbolType symbol;
        ExchangeOrderIdType target_exchange_order_id; // XID of the expired order
        ClientOrderIdType client_order_id;            // CID of the expired order
        PriceType price;                              // Original price of the limit order
        QuantityType quantity;                        // Remaining quantity when expired
        Duration timeout_value;                       // Original timeout value echoed back

        AckTriggerExpiredLimitOrderEvent(
            Timestamp created_ts,
            SymbolType sym,
            ExchangeOrderIdType target_xid,
            ClientOrderIdType original_cid,
            PriceType original_price,
            QuantityType rem_qty,
            Duration original_timeout
        ) : BaseEvent(created_ts),
            symbol(std::move(sym)),
            target_exchange_order_id(target_xid),
            client_order_id(original_cid),
            price(original_price),
            quantity(rem_qty),
            timeout_value(original_timeout)
        {}

         std::string to_string() const override {
             std::ostringstream oss;
             oss << "AckTriggerExpiredLimitOrderEvent(" << BaseEvent::to_string()
                 << ", symbol=" << symbol
                 << ", target_exchange_order_id=" << target_exchange_order_id
                 << ", client_order_id=" << client_order_id
                 << ", price=" << price
                 << ", quantity=" << quantity // Remaining quantity
                 << ", timeout_value=" << format_duration(timeout_value) << ")";
             return oss.str();
         }
    };


} // namespace ModelEvents


// --- Define the list of all event types for the EventBus ---
// This list needs to be maintained and match the events defined above.
// It will be used when instantiating TopicBasedEventBus and EventProcessor.
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
    std::shared_ptr<const ModelEvents::LimitOrderRejectEvent>, // Added missing rejects
    std::shared_ptr<const ModelEvents::MarketOrderRejectEvent>, // Added missing rejects
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
    // NOTE: Base classes like BaseEvent, BaseAckEvent etc. are NOT included here.
    // Only concrete, instantiable event types go into the variant.
>;


// --- Template alias for EventBus using these Model Events ---
template<typename... ExtraEventTypes> // Allow adding more types later if needed
using ModelEventBus = EventBusSystem::TopicBasedEventBus<
    ModelEvents::CheckLimitOrderExpirationEvent,
    ModelEvents::Bang,
    ModelEvents::LTwoOrderBookEvent,
    ModelEvents::LimitOrderEvent,
    ModelEvents::MarketOrderEvent,
    ModelEvents::PartialCancelLimitOrderEvent,
    ModelEvents::PartialCancelMarketOrderEvent,
    ModelEvents::FullCancelLimitOrderEvent,
    ModelEvents::FullCancelMarketOrderEvent,
    ModelEvents::LimitOrderAckEvent,
    ModelEvents::MarketOrderAckEvent,
    ModelEvents::FullCancelLimitOrderAckEvent,
    ModelEvents::FullCancelMarketOrderAckEvent,
    ModelEvents::PartialCancelLimitAckEvent,
    ModelEvents::PartialCancelMarketAckEvent,
    ModelEvents::PartialCancelLimitOrderRejectEvent,
    ModelEvents::FullCancelLimitOrderRejectEvent,
    ModelEvents::PartialCancelMarketOrderRejectEvent,
    ModelEvents::FullCancelMarketOrderRejectEvent,
    ModelEvents::LimitOrderRejectEvent,
    ModelEvents::MarketOrderRejectEvent,
    ModelEvents::MarketOrderExpiredEvent,
    ModelEvents::LimitOrderExpiredEvent,
    ModelEvents::PartialFillLimitOrderEvent,
    ModelEvents::PartialFillMarketOrderEvent,
    ModelEvents::FullFillLimitOrderEvent,
    ModelEvents::FullFillMarketOrderEvent,
    ModelEvents::TradeEvent,
    ModelEvents::TriggerExpiredLimitOrderEvent,
    ModelEvents::RejectTriggerExpiredLimitOrderEvent,
    ModelEvents::AckTriggerExpiredLimitOrderEvent,
    ExtraEventTypes... // Include any extra types passed
>;

// --- Template alias for EventProcessor using these Model Events ---
// file: src/Model.h
// ...
// --- Template alias for EventProcessor using these Model Events ---
template<typename ActualDerivedAgent, typename... ExtraEventTypes> // Add ActualDerivedAgent for CRTP
using ModelEventProcessor = EventBusSystem::EventProcessor<ActualDerivedAgent, // Pass ActualDerivedAgent as the CRTP Derived type
    ModelEvents::CheckLimitOrderExpirationEvent,
    ModelEvents::Bang,
    ModelEvents::LTwoOrderBookEvent,
    ModelEvents::LimitOrderEvent,
    ModelEvents::MarketOrderEvent,
    ModelEvents::PartialCancelLimitOrderEvent,
    ModelEvents::PartialCancelMarketOrderEvent,
    ModelEvents::FullCancelLimitOrderEvent,
    ModelEvents::FullCancelMarketOrderEvent,
    ModelEvents::LimitOrderAckEvent,
    ModelEvents::MarketOrderAckEvent,
    ModelEvents::FullCancelLimitOrderAckEvent,
    ModelEvents::FullCancelMarketOrderAckEvent,
    ModelEvents::PartialCancelLimitAckEvent,
    ModelEvents::PartialCancelMarketAckEvent,
    ModelEvents::PartialCancelLimitOrderRejectEvent,
    ModelEvents::FullCancelLimitOrderRejectEvent,
    ModelEvents::PartialCancelMarketOrderRejectEvent,
    ModelEvents::FullCancelMarketOrderRejectEvent,
    ModelEvents::LimitOrderRejectEvent,
    ModelEvents::MarketOrderRejectEvent,
    ModelEvents::MarketOrderExpiredEvent,
    ModelEvents::LimitOrderExpiredEvent,
    ModelEvents::PartialFillLimitOrderEvent,
    ModelEvents::PartialFillMarketOrderEvent,
    ModelEvents::FullFillLimitOrderEvent,
    ModelEvents::FullFillMarketOrderEvent,
    ModelEvents::TradeEvent,
    ModelEvents::TriggerExpiredLimitOrderEvent,
    ModelEvents::RejectTriggerExpiredLimitOrderEvent,
    ModelEvents::AckTriggerExpiredLimitOrderEvent,
    ExtraEventTypes... // Include any extra types passed
>;

