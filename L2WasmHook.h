//
// Created by Esteban Lanter on 22.05.2025.
//

#ifndef PYCPPEXCHANGESIM_L2WASMHOOK_H
#define PYCPPEXCHANGESIM_L2WASMHOOK_H

#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <string>
#include <variant>
#include <thread>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#endif

// ─── Project headers ─────────────────────────────────────────────────────────
#include "src/Model.h"
#include "src/EventBus.h"
#include "src/TradingSimulation.h"
#include "ZeroIntelligenceMarketMaker.h"
#include "src/RealTimeBus.h"
#include "src/PrePublishHookBase.h"

// Concrete L2WasmHook, inheriting from TradingPrePublishHook.
class L2WasmHook : public TradingPrePublishHook {
private:
#ifdef __EMSCRIPTEN__
    emscripten::val js_callback;
    bool has_callback;
#endif

public:
    L2WasmHook() 
#ifdef __EMSCRIPTEN__
        : js_callback(emscripten::val::undefined()), has_callback(false)
#endif
    {
    }

#ifdef __EMSCRIPTEN__
    // Set the JavaScript callback function
    void set_callback(emscripten::val callback) {
        std::cout << "[L2WasmHook] DEBUG: set_callback called" << std::endl;
        js_callback = callback;
        has_callback = !callback.isUndefined() && callback.typeOf().as<std::string>() == "function";
        std::cout << "[L2WasmHook] DEBUG: Callback set, has_callback=" << has_callback 
                  << ", type=" << callback.typeOf().as<std::string>() << std::endl;
    }
#endif

    std::string hook_name() const override {
        return "L2WasmHook";
    }

    // Generic function to send any message to JavaScript
    void send_message_to_js(const std::string& event_type, const std::string& details, AgentId publisher_id, TopicId topic_id, Timestamp publish_time) {
#ifdef __EMSCRIPTEN__
        std::cout << "[L2WasmHook] DEBUG: send_message_to_js called for event: " << event_type << std::endl;
        
        if (!has_callback) {
            std::cout << "[L2WasmHook] ERROR: No JavaScript callback set for event: " << event_type << std::endl;
            return;
        }

        try {
            std::cout << "[L2WasmHook] DEBUG: Creating JavaScript object for " << event_type << std::endl;
            
            emscripten::val js_event = emscripten::val::object();
            js_event.set("eventType", event_type);
            js_event.set("details", details);
            js_event.set("publisherId", static_cast<int>(publisher_id));
            js_event.set("topicId", static_cast<int>(topic_id));
            js_event.set("timestamp", 
                std::chrono::time_point_cast<std::chrono::milliseconds>(publish_time).time_since_epoch().count());
            
            std::cout << "[L2WasmHook] DEBUG: Calling JavaScript callback for " << event_type 
                      << " with details: " << details 
                      << ", publisher: " << publisher_id 
                      << ", topic: " << topic_id << std::endl;
            
            js_callback(js_event);
            
            std::cout << "[L2WasmHook] DEBUG: JavaScript callback completed successfully for " << event_type << std::endl;
        } catch (...) {
            std::cout << "[L2WasmHook] ERROR: JavaScript callback threw an exception for " << event_type << std::endl;
        }
#else
        std::cout << "[L2WasmHook] DEBUG: Not in Emscripten build, skipping JS callback for " << event_type << std::endl;
#endif
    }

    // Convert L2 event to JavaScript object (existing specialized function)
    void send_l2_to_js(const ModelEvents::LTwoOrderBookEvent &event, AgentId publisher_id = 0, TopicId topic_id = 0, Timestamp publish_time = Timestamp{}) {
#ifdef __EMSCRIPTEN__
        std::cout << "[L2WasmHook] DEBUG: send_l2_to_js called for symbol: " << event.symbol 
                  << ", bids: " << event.bids.size() << ", asks: " << event.asks.size() << std::endl;
        
        if (!has_callback) {
            std::cout << "[L2WasmHook] ERROR: No JavaScript callback set!" << std::endl;
            return;
        }

        std::cout << "[L2WasmHook] DEBUG: Creating JavaScript object..." << std::endl;

        // Create JavaScript object with the L2 data
        emscripten::val js_event = emscripten::val::object();
        
        // Add eventType field for proper identification
        js_event.set("eventType", "LTwoOrderBookEvent");
        js_event.set("symbol", event.symbol);
        js_event.set("exchange_ts", event.exchange_ts ? 
            std::chrono::time_point_cast<std::chrono::milliseconds>(*event.exchange_ts).time_since_epoch().count() : -1);
        js_event.set("ingress_ts", 
            std::chrono::time_point_cast<std::chrono::milliseconds>(event.ingress_ts).time_since_epoch().count());

        // Add generic message fields for consistency
        js_event.set("publisherId", static_cast<int>(publisher_id));
        js_event.set("topicId", static_cast<int>(topic_id));
        if (publish_time != Timestamp{}) {
            js_event.set("timestamp", 
                std::chrono::time_point_cast<std::chrono::milliseconds>(publish_time).time_since_epoch().count());
        } else {
            js_event.set("timestamp", 
                std::chrono::time_point_cast<std::chrono::milliseconds>(event.ingress_ts).time_since_epoch().count());
        }

        // Convert bids to JavaScript array
        emscripten::val js_bids = emscripten::val::array();
        std::cout << "[L2WasmHook] DEBUG: Converting " << event.bids.size() << " bid levels..." << std::endl;
        for (size_t i = 0; i < event.bids.size() && i < 10; ++i) {
            emscripten::val level = emscripten::val::object();
            double price = ModelEvents::price_to_float(event.bids[i].first);
            double quantity = ModelEvents::quantity_to_float(event.bids[i].second);
            level.set("price", price);
            level.set("quantity", quantity);
            js_bids.call<void>("push", level);
            if (i == 0) {  // Log first bid for debugging
                std::cout << "[L2WasmHook] DEBUG: Best bid: " << price << " @ " << quantity << std::endl;
            }
        }
        js_event.set("bids", js_bids);

        // Convert asks to JavaScript array
        emscripten::val js_asks = emscripten::val::array();
        std::cout << "[L2WasmHook] DEBUG: Converting " << event.asks.size() << " ask levels..." << std::endl;
        for (size_t i = 0; i < event.asks.size() && i < 10; ++i) {
            emscripten::val level = emscripten::val::object();
            double price = ModelEvents::price_to_float(event.asks[i].first);
            double quantity = ModelEvents::quantity_to_float(event.asks[i].second);
            level.set("price", price);
            level.set("quantity", quantity);
            js_asks.call<void>("push", level);
            if (i == 0) {  // Log first ask for debugging
                std::cout << "[L2WasmHook] DEBUG: Best ask: " << price << " @ " << quantity << std::endl;
            }
        }
        js_event.set("asks", js_asks);

        // Call the JavaScript callback
        std::cout << "[L2WasmHook] DEBUG: Calling JavaScript callback..." << std::endl;
        try {
            js_callback(js_event);
            std::cout << "[L2WasmHook] DEBUG: JavaScript callback completed successfully" << std::endl;
        } catch (...) {
            std::cout << "[L2WasmHook] ERROR: JavaScript callback threw an exception!" << std::endl;
        }
#else
        std::cout << "[L2WasmHook] DEBUG: Not in Emscripten build, skipping JS callback" << std::endl;
#endif
    }

    // Override all pre-publish event handlers

    void on_pre_publish_CheckLimitOrderExpirationEvent(const ModelEvents::CheckLimitOrderExpirationEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        send_message_to_js("CheckLimitOrderExpirationEvent", "Order expiration check", pid, tid, ts);
    }

    void on_pre_publish_Bang(const ModelEvents::Bang& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        send_message_to_js("Bang", "System bang event", pid, tid, ts);
    }

    void on_pre_publish_LTwoOrderBookEvent(const ModelEvents::LTwoOrderBookEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::cout << "[L2WasmHook] DEBUG: on_pre_publish_LTwoOrderBookEvent called! Publisher: " 
                  << pid << ", Symbol: " << event.symbol 
                  << ", Bids: " << event.bids.size() << ", Asks: " << event.asks.size() << std::endl;
        
        // Send the detailed L2 data with eventType field included
        send_l2_to_js(event, pid, tid, ts);
    }

    void on_pre_publish_LimitOrderEvent(const ModelEvents::LimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::cout << "[L2WasmHook] DEBUG: *** LimitOrderEvent captured! ***" << std::endl;
        std::string details = "Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL") + 
                            ", Price: " + std::to_string(ModelEvents::price_to_float(event.price)) +
                            ", Qty: " + std::to_string(ModelEvents::quantity_to_float(event.quantity));
        send_message_to_js("LimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_MarketOrderEvent(const ModelEvents::MarketOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::cout << "[L2WasmHook] DEBUG: *** MarketOrderEvent captured! ***" << std::endl;
        std::string details = "Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL") + 
                            ", Qty: " + std::to_string(ModelEvents::quantity_to_float(event.quantity));
        send_message_to_js("MarketOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialCancelLimitOrderEvent(const ModelEvents::PartialCancelLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Order ID: " + std::to_string(event.target_order_id) + 
                            ", Cancel Qty: " + std::to_string(ModelEvents::quantity_to_float(event.cancel_qty));
        send_message_to_js("PartialCancelLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialCancelMarketOrderEvent(const ModelEvents::PartialCancelMarketOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Order ID: " + std::to_string(event.target_order_id) + 
                            ", Cancel Qty: " + std::to_string(ModelEvents::quantity_to_float(event.cancel_qty));
        send_message_to_js("PartialCancelMarketOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullCancelLimitOrderEvent(const ModelEvents::FullCancelLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Order ID: " + std::to_string(event.target_order_id);
        send_message_to_js("FullCancelLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullCancelMarketOrderEvent(const ModelEvents::FullCancelMarketOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Order ID: " + std::to_string(event.target_order_id);
        send_message_to_js("FullCancelMarketOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_LimitOrderAckEvent(const ModelEvents::LimitOrderAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::cout << "[L2WasmHook] DEBUG: *** LimitOrderAckEvent captured! ***" << std::endl;
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL");
        send_message_to_js("LimitOrderAckEvent", details, pid, tid, ts);
    }

    void on_pre_publish_MarketOrderAckEvent(const ModelEvents::MarketOrderAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL");
        send_message_to_js("MarketOrderAckEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullCancelLimitOrderAckEvent(const ModelEvents::FullCancelLimitOrderAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id);
        send_message_to_js("FullCancelLimitOrderAckEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullCancelMarketOrderAckEvent(const ModelEvents::FullCancelMarketOrderAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id);
        send_message_to_js("FullCancelMarketOrderAckEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialCancelLimitAckEvent(const ModelEvents::PartialCancelLimitAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id);
        send_message_to_js("PartialCancelLimitAckEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialCancelMarketAckEvent(const ModelEvents::PartialCancelMarketAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id);
        send_message_to_js("PartialCancelMarketAckEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialCancelLimitOrderRejectEvent(const ModelEvents::PartialCancelLimitOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("PartialCancelLimitOrderRejectEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullCancelLimitOrderRejectEvent(const ModelEvents::FullCancelLimitOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("FullCancelLimitOrderRejectEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialCancelMarketOrderRejectEvent(const ModelEvents::PartialCancelMarketOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("PartialCancelMarketOrderRejectEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullCancelMarketOrderRejectEvent(const ModelEvents::FullCancelMarketOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("FullCancelMarketOrderRejectEvent", details, pid, tid, ts);
    }

    void on_pre_publish_LimitOrderRejectEvent(const ModelEvents::LimitOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("LimitOrderRejectEvent", details, pid, tid, ts);
    }

    void on_pre_publish_MarketOrderRejectEvent(const ModelEvents::MarketOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("MarketOrderRejectEvent", details, pid, tid, ts);
    }

    void on_pre_publish_MarketOrderExpiredEvent(const ModelEvents::MarketOrderExpiredEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id);
        send_message_to_js("MarketOrderExpiredEvent", details, pid, tid, ts);
    }

    void on_pre_publish_LimitOrderExpiredEvent(const ModelEvents::LimitOrderExpiredEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id);
        send_message_to_js("LimitOrderExpiredEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialFillLimitOrderEvent(const ModelEvents::PartialFillLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Fill Qty: " + std::to_string(ModelEvents::quantity_to_float(event.fill_qty)) +
                            ", Fill Price: " + std::to_string(ModelEvents::price_to_float(event.fill_price));
        send_message_to_js("PartialFillLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_PartialFillMarketOrderEvent(const ModelEvents::PartialFillMarketOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Fill Qty: " + std::to_string(ModelEvents::quantity_to_float(event.fill_qty)) +
                            ", Fill Price: " + std::to_string(ModelEvents::price_to_float(event.fill_price));
        send_message_to_js("PartialFillMarketOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullFillLimitOrderEvent(const ModelEvents::FullFillLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Fill Price: " + std::to_string(ModelEvents::price_to_float(event.fill_price));
        send_message_to_js("FullFillLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullFillMarketOrderEvent(const ModelEvents::FullFillMarketOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Fill Price: " + std::to_string(ModelEvents::price_to_float(event.fill_price));
        send_message_to_js("FullFillMarketOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_TradeEvent(const ModelEvents::TradeEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::cout << "[L2WasmHook] DEBUG: *** TradeEvent captured! ***" << std::endl;
        std::string details = "Trade: " + std::to_string(ModelEvents::quantity_to_float(event.quantity)) + 
                            " @ " + std::to_string(ModelEvents::price_to_float(event.price));
        send_message_to_js("TradeEvent", details, pid, tid, ts);
    }

    void on_pre_publish_TriggerExpiredLimitOrderEvent(const ModelEvents::TriggerExpiredLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Exchange Order ID: " + std::to_string(event.target_exchange_order_id);
        send_message_to_js("TriggerExpiredLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_RejectTriggerExpiredLimitOrderEvent(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Exchange Order ID: " + std::to_string(event.target_exchange_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("RejectTriggerExpiredLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_AckTriggerExpiredLimitOrderEvent(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Exchange Order ID: " + std::to_string(event.target_exchange_order_id);
        send_message_to_js("AckTriggerExpiredLimitOrderEvent", details, pid, tid, ts);
    }
};

#endif //PYCPPEXCHANGESIM_L2WASMHOOK_H 