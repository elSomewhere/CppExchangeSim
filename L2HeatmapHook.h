//
// Created by Esteban Lanter on 22.05.2025.
//

#ifndef PYCPPEXCHANGESIM_L2HEATMAPHOOK_H
#define PYCPPEXCHANGESIM_L2HEATMAPHOOK_H

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

// ─── Project headers ─────────────────────────────────────────────────────────
#include "src/Model.h"
#include "src/EventBus.h"
#include "src/TradingSimulation.h"
#include "ZeroIntelligenceMarketMaker.h"
#include "src/PrePublishHookBase.h"
#include "HeatmapBuffer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/val.h>
#include <emscripten/bind.h>
#endif

// Concrete L2HeatmapHook, inheriting from TradingPrePublishHook.
// Combines the functionality of L2PrinterHook, L2WasmHook, and adds heatmap buffering
class L2HeatmapHook : public TradingPrePublishHook {
private:
    std::unique_ptr<HeatmapBuffer> heatmap_buffer_;
    
#ifdef __EMSCRIPTEN__
    emscripten::val js_l2_callback;
    emscripten::val js_heatmap_callback;
    bool has_l2_callback;
    bool has_heatmap_callback;
#endif
    
    // Configuration
    bool enable_console_output_;
    bool enable_l2_updates_;
    bool enable_heatmap_updates_;
    size_t heatmap_update_frequency_;  // Send heatmap data every N L2 updates
    size_t update_counter_;
    
public:
    L2HeatmapHook(size_t buffer_size = 300, int num_price_levels = 200, double tick_size = 1.0,
                  bool enable_console = false, bool enable_l2 = true, bool enable_heatmap = true, 
                  size_t heatmap_freq = 1)
        : heatmap_buffer_(std::make_unique<HeatmapBuffer>(buffer_size, num_price_levels, tick_size)),
          enable_console_output_(enable_console), enable_l2_updates_(enable_l2), 
          enable_heatmap_updates_(enable_heatmap), heatmap_update_frequency_(heatmap_freq),
          update_counter_(0) {
#ifdef __EMSCRIPTEN__
        js_l2_callback = emscripten::val::undefined();
        js_heatmap_callback = emscripten::val::undefined();
        has_l2_callback = false;
        has_heatmap_callback = false;
#endif
    }

    std::string hook_name() const override {
        return "L2HeatmapHook";
    }
    
#ifdef __EMSCRIPTEN__
    // Set JavaScript callbacks
    void set_l2_callback(emscripten::val callback) {
        js_l2_callback = callback;
        has_l2_callback = !callback.isUndefined();
        std::cout << "[L2HeatmapHook] L2 callback " << (has_l2_callback ? "set" : "cleared") << std::endl;
    }
    
    void set_heatmap_callback(emscripten::val callback) {
        js_heatmap_callback = callback;
        has_heatmap_callback = !callback.isUndefined();
        std::cout << "[L2HeatmapHook] Heatmap callback " << (has_heatmap_callback ? "set" : "cleared") << std::endl;
    }
#endif
    
    // Configuration methods
    void set_console_output(bool enable) { enable_console_output_ = enable; }
    void set_l2_updates(bool enable) { enable_l2_updates_ = enable; }
    void set_heatmap_updates(bool enable) { enable_heatmap_updates_ = enable; }
    void set_heatmap_frequency(size_t frequency) { heatmap_update_frequency_ = frequency; }
    
    // Heatmap buffer configuration
    void set_buffer_size(size_t size) { heatmap_buffer_->set_buffer_size(size); }
    size_t get_buffer_size() const { return heatmap_buffer_->get_buffer_size(); }
    size_t get_current_buffer_usage() const { return heatmap_buffer_->get_current_size(); }
    
    // Generic function to send any message to JavaScript
    void send_message_to_js(const std::string& event_type, const std::string& details, AgentId publisher_id, TopicId topic_id, Timestamp publish_time) {
#ifdef __EMSCRIPTEN__
        if (!has_l2_callback) return;

        try {
            emscripten::val js_event = emscripten::val::object();
            js_event.set("eventType", event_type);
            js_event.set("details", details);
            js_event.set("publisherId", static_cast<int>(publisher_id));
            js_event.set("topicId", static_cast<int>(topic_id));
            js_event.set("timestamp", 
                std::chrono::time_point_cast<std::chrono::milliseconds>(publish_time).time_since_epoch().count());
            
            js_l2_callback(js_event);
        } catch (...) {
            std::cout << "[L2HeatmapHook] ERROR: JavaScript callback threw an exception for " << event_type << std::endl;
        }
#endif
    }
    
    void print_l2_top_10(const ModelEvents::LTwoOrderBookEvent &event) {
        if (!enable_console_output_) return;
        
        std::cout << std::unitbuf;
        std::cout << "\n--- L2 Order Book Snapshot (Top 10) for " << event.symbol << " ---" << std::endl;
        std::cout << "Exchange TS: " << ModelEvents::format_optional_timestamp(event.exchange_ts)
                  << ", Ingress TS: " << ModelEvents::format_timestamp(event.ingress_ts) << std::endl;

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "ASKS (Price -- Quantity):" << std::endl;
        int printed = 0;
        for (const auto &lvl : event.asks) {
            if (printed++ >= 10) break;
            std::cout << "  " << std::setw(12) << ModelEvents::price_to_float(lvl.first)
                      << " -- " << std::setw(12) << ModelEvents::quantity_to_float(lvl.second) << std::endl;
        }
        if (event.asks.empty()) std::cout << "  (No asks)" << std::endl;
        if (static_cast<size_t>(printed) < event.asks.size())
            std::cout << "  (... " << (event.asks.size() - printed) << " more ask levels)" << std::endl;

        std::cout << "BIDS (Price -- Quantity):" << std::endl;
        printed = 0;
        for (const auto &lvl : event.bids) {
            if (printed++ >= 10) break;
            std::cout << "  " << std::setw(12) << ModelEvents::price_to_float(lvl.first)
                      << " -- " << std::setw(12) << ModelEvents::quantity_to_float(lvl.second) << std::endl;
        }
        if (event.bids.empty()) std::cout << "  (No bids)" << std::endl;
        if (static_cast<size_t>(printed) < event.bids.size())
            std::cout << "  (... " << (event.bids.size() - printed) << " more bid levels)" << std::endl;

        std::cout << "Heatmap Buffer: " << heatmap_buffer_->get_current_size() 
                  << "/" << heatmap_buffer_->get_buffer_size() << " snapshots" << std::endl;
        std::cout << "----------------------------------------" << std::endl << std::endl;
        std::cout << std::nounitbuf;
    }
    
#ifdef __EMSCRIPTEN__
    void send_l2_to_js(const ModelEvents::LTwoOrderBookEvent &event, AgentId publisher_id = 0, TopicId topic_id = 0, Timestamp publish_time = Timestamp{}) {
        if (!enable_l2_updates_ || !has_l2_callback) return;
        
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

        // Convert bids to JavaScript array (limit to top 10 for performance)
        emscripten::val js_bids = emscripten::val::array();
        for (size_t i = 0; i < event.bids.size() && i < 10; ++i) {
            emscripten::val level = emscripten::val::object();
            level.set("price", ModelEvents::price_to_float(event.bids[i].first));
            level.set("quantity", ModelEvents::quantity_to_float(event.bids[i].second));
            js_bids.call<void>("push", level);
        }
        js_event.set("bids", js_bids);

        // Convert asks to JavaScript array (limit to top 10 for performance)
        emscripten::val js_asks = emscripten::val::array();
        for (size_t i = 0; i < event.asks.size() && i < 10; ++i) {
            emscripten::val level = emscripten::val::object();
            level.set("price", ModelEvents::price_to_float(event.asks[i].first));
            level.set("quantity", ModelEvents::quantity_to_float(event.asks[i].second));
            js_asks.call<void>("push", level);
        }
        js_event.set("asks", js_asks);

        // Call the JavaScript callback
        try {
            js_l2_callback(js_event);
        } catch (...) {
            std::cout << "[L2HeatmapHook] ERROR: L2 JavaScript callback threw an exception!" << std::endl;
        }
    }
    
    void send_heatmap_to_js() {
        if (!enable_heatmap_updates_ || !has_heatmap_callback || !heatmap_buffer_->is_initialized()) return;
        
        try {
            auto viz_data = heatmap_buffer_->get_visualization_data();
            
            // Create JavaScript object with heatmap data
            emscripten::val js_heatmap = emscripten::val::object();
            
            // Metadata
            js_heatmap.set("timestamp", std::chrono::time_point_cast<std::chrono::milliseconds>(viz_data.timestamp).time_since_epoch().count());
            js_heatmap.set("midPrice", viz_data.mid_price);
            js_heatmap.set("basePrice", viz_data.base_price);
            js_heatmap.set("tickSize", viz_data.tick_size);
            js_heatmap.set("numLevels", viz_data.num_levels);
            js_heatmap.set("bufferUsage", heatmap_buffer_->get_current_size());
            js_heatmap.set("bufferSize", heatmap_buffer_->get_buffer_size());
            
            // Volume statistics for normalization
            emscripten::val js_stats = emscripten::val::object();
            js_stats.set("maxBidVolume", viz_data.stats.max_bid_volume);
            js_stats.set("maxAskVolume", viz_data.stats.max_ask_volume);
            js_stats.set("p95BidVolume", viz_data.stats.p95_bid_volume);
            js_stats.set("p95AskVolume", viz_data.stats.p95_ask_volume);
            js_heatmap.set("stats", js_stats);
            
            // Convert bid volumes to JavaScript array
            emscripten::val js_bid_volumes = emscripten::val::array();
            for (size_t i = 0; i < viz_data.bid_volumes.size(); ++i) {
                js_bid_volumes.call<void>("push", viz_data.bid_volumes[i]);
            }
            js_heatmap.set("bidVolumes", js_bid_volumes);
            
            // Convert ask volumes to JavaScript array
            emscripten::val js_ask_volumes = emscripten::val::array();
            for (size_t i = 0; i < viz_data.ask_volumes.size(); ++i) {
                js_ask_volumes.call<void>("push", viz_data.ask_volumes[i]);
            }
            js_heatmap.set("askVolumes", js_ask_volumes);
            
            // Calculate price labels for the levels
            emscripten::val js_price_labels = emscripten::val::array();
            for (int level = 0; level < viz_data.num_levels; ++level) {
                double price = viz_data.base_price + ((level - viz_data.num_levels/2) * viz_data.tick_size);
                js_price_labels.call<void>("push", price);
            }
            js_heatmap.set("priceLabels", js_price_labels);
            
            // Call the JavaScript heatmap callback
            js_heatmap_callback(js_heatmap);
            
        } catch (...) {
            std::cout << "[L2HeatmapHook] ERROR: Heatmap JavaScript callback threw an exception!" << std::endl;
        }
    }
#endif
    
    // Override the LTwoOrderBookEvent handler
    void on_pre_publish_LTwoOrderBookEvent(
            const ModelEvents::LTwoOrderBookEvent& event,
            AgentId publisher_id,
            TopicId published_topic_id,
            Timestamp publish_time,
            const BusT* bus
    ) override {
        // Add to heatmap buffer first
        heatmap_buffer_->add_l2_snapshot(event);
        
        // Print to console if enabled
        print_l2_top_10(event);
        
#ifdef __EMSCRIPTEN__
        // Send L2 data to JavaScript with proper event fields
        send_l2_to_js(event, publisher_id, published_topic_id, publish_time);
        
        // Send heatmap data based on frequency
        update_counter_++;
        if (update_counter_ % heatmap_update_frequency_ == 0) {
            send_heatmap_to_js();
        }
#endif
    }

    // Override all other event handlers to capture all trading events

    void on_pre_publish_CheckLimitOrderExpirationEvent(const ModelEvents::CheckLimitOrderExpirationEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        send_message_to_js("CheckLimitOrderExpirationEvent", "Order expiration check", pid, tid, ts);
    }

    void on_pre_publish_Bang(const ModelEvents::Bang& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        send_message_to_js("Bang", "System bang event", pid, tid, ts);
    }

    void on_pre_publish_LimitOrderEvent(const ModelEvents::LimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL") + 
                            ", Price: " + std::to_string(ModelEvents::price_to_float(event.price)) +
                            ", Qty: " + std::to_string(ModelEvents::quantity_to_float(event.quantity));
        send_message_to_js("LimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_MarketOrderEvent(const ModelEvents::MarketOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL") + 
                            ", Qty: " + std::to_string(ModelEvents::quantity_to_float(event.quantity));
        send_message_to_js("MarketOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_TradeEvent(const ModelEvents::TradeEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Trade: " + std::to_string(ModelEvents::quantity_to_float(event.quantity)) + 
                            " @ " + std::to_string(ModelEvents::price_to_float(event.price));
        send_message_to_js("TradeEvent", details, pid, tid, ts);
    }

    void on_pre_publish_LimitOrderAckEvent(const ModelEvents::LimitOrderAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL");
        send_message_to_js("LimitOrderAckEvent", details, pid, tid, ts);
    }

    void on_pre_publish_MarketOrderAckEvent(const ModelEvents::MarketOrderAckEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Order ID: " + std::to_string(event.order_id) + 
                            ", Side: " + std::string(event.side == ModelEvents::Side::BUY ? "BUY" : "SELL");
        send_message_to_js("MarketOrderAckEvent", details, pid, tid, ts);
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

    // Add other important event handlers (I'll add more if needed)
    void on_pre_publish_PartialCancelLimitOrderEvent(const ModelEvents::PartialCancelLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Order ID: " + std::to_string(event.target_order_id) + 
                            ", Cancel Qty: " + std::to_string(ModelEvents::quantity_to_float(event.cancel_qty));
        send_message_to_js("PartialCancelLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_FullCancelLimitOrderEvent(const ModelEvents::FullCancelLimitOrderEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Target Order ID: " + std::to_string(event.target_order_id);
        send_message_to_js("FullCancelLimitOrderEvent", details, pid, tid, ts);
    }

    void on_pre_publish_LimitOrderRejectEvent(const ModelEvents::LimitOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("LimitOrderRejectEvent", details, pid, tid, ts);
    }

    void on_pre_publish_MarketOrderRejectEvent(const ModelEvents::MarketOrderRejectEvent& event, AgentId pid, TopicId tid, Timestamp ts, const BusT* bus) override {
        std::string details = "Client Order ID: " + std::to_string(event.client_order_id) + ", Symbol: " + event.symbol;
        send_message_to_js("MarketOrderRejectEvent", details, pid, tid, ts);
    }

    // Now these methods handle all event types, not just L2!
};

#ifdef __EMSCRIPTEN__
// Emscripten bindings for the heatmap hook
EMSCRIPTEN_BINDINGS(l2_heatmap_hook) {
    emscripten::class_<L2HeatmapHook>("L2HeatmapHook")
        .constructor<size_t, int, double, bool, bool, bool, size_t>()
        .function("setL2Callback", &L2HeatmapHook::set_l2_callback)
        .function("setHeatmapCallback", &L2HeatmapHook::set_heatmap_callback)
        .function("setConsoleOutput", &L2HeatmapHook::set_console_output)
        .function("setL2Updates", &L2HeatmapHook::set_l2_updates)
        .function("setHeatmapUpdates", &L2HeatmapHook::set_heatmap_updates)
        .function("setHeatmapFrequency", &L2HeatmapHook::set_heatmap_frequency)
        .function("setBufferSize", &L2HeatmapHook::set_buffer_size)
        .function("getBufferSize", &L2HeatmapHook::get_buffer_size)
        .function("getCurrentBufferUsage", &L2HeatmapHook::get_current_buffer_usage);
}
#endif

#endif //PYCPPEXCHANGESIM_L2HEATMAPHOOK_H 