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

    // Convert L2 event to JavaScript object
    void send_l2_to_js(const ModelEvents::LTwoOrderBookEvent &event) {
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
        
        js_event.set("symbol", event.symbol);
        js_event.set("exchange_ts", event.exchange_ts ? 
            std::chrono::time_point_cast<std::chrono::milliseconds>(*event.exchange_ts).time_since_epoch().count() : -1);
        js_event.set("ingress_ts", 
            std::chrono::time_point_cast<std::chrono::milliseconds>(event.ingress_ts).time_since_epoch().count());

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

    // Override only the LTwoOrderBookEvent handler
    void on_pre_publish_LTwoOrderBookEvent(
            const ModelEvents::LTwoOrderBookEvent& event,
            AgentId publisher_id,
            TopicId published_topic_id,
            Timestamp publish_time,
            const BusT* bus
    ) override {
        std::cout << "[L2WasmHook] DEBUG: on_pre_publish_LTwoOrderBookEvent called! Publisher: " 
                  << publisher_id << ", Symbol: " << event.symbol 
                  << ", Bids: " << event.bids.size() << ", Asks: " << event.asks.size() << std::endl;
        send_l2_to_js(event);
    }

    // All other on_pre_publish_SpecificEvent methods are inherited from TradingPrePublishHook
    // and will use their default (noop) implementation.
};

#endif //PYCPPEXCHANGESIM_L2WASMHOOK_H 