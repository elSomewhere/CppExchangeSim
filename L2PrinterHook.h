//
// Created by Esteban Lanter on 22.05.2025.
//

#ifndef PYCPPEXCHANGESIM_L2PRINTERHOOK_H
#define PYCPPEXCHANGESIM_L2PRINTERHOOK_H


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
#include "src/RealTimeBus.h"
#include "src/PrePublishHookBase.h"



// Concrete L2PrinterHook, inheriting from TradingPrePublishHook.
class L2PrinterHook : public TradingPrePublishHook {
public:
    L2PrinterHook() = default;

    std::string hook_name() const override {
        return "L2PrinterHook";
    }
    void print_l2_top_10(const ModelEvents::LTwoOrderBookEvent &event)
    {
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
        if (printed < event.asks.size())
            std::cout << "  (... " << (event.asks.size() - printed) << " more ask levels)" << std::endl;

        std::cout << "BIDS (Price -- Quantity):" << std::endl;
        printed = 0;
        for (const auto &lvl : event.bids) {
            if (printed++ >= 10) break;
            std::cout << "  " << std::setw(12) << ModelEvents::price_to_float(lvl.first)
                      << " -- " << std::setw(12) << ModelEvents::quantity_to_float(lvl.second) << std::endl;
        }
        if (event.bids.empty()) std::cout << "  (No bids)" << std::endl;
        if (printed < event.bids.size())
            std::cout << "  (... " << (event.bids.size() - printed) << " more bid levels)" << std::endl;

        std::cout << "----------------------------------------" << std::endl << std::endl;
        std::cout << std::nounitbuf;
    }
    // Override only the LTwoOrderBookEvent handler
    void on_pre_publish_LTwoOrderBookEvent(
            const ModelEvents::LTwoOrderBookEvent& event,
            AgentId publisher_id,
            TopicId published_topic_id,
            Timestamp publish_time,
            const BusT* bus
    ) override {
        print_l2_top_10(event);
    }

    // All other on_pre_publish_SpecificEvent methods are inherited from TradingPrePublishHook
    // and will use their default (noop) implementation.
};


#endif //PYCPPEXCHANGESIM_L2PRINTERHOOK_H
