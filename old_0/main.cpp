// file: src/main.cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <numeric> // For std::accumulate (though not used after removing benchmark)
#include <optional>
#include <memory> // For std::make_shared
#include <thread> // For potential sleep (optional)

// Include the modified EventBus header
#include "src/EventBus.h"
#include "src/CancelFairy.h"
#include "src/ZeroIntelligenceMarketMaker.h"
#include "src/ExchangeAdapter.h"
#include "src/L2SnapshotCollector.h"
#include "src/Simulation.h"

// L2 Snapshot callback function
void l2_console_printer(const ModelEvents::LTwoOrderBookEvent& event) {
    // Commented out to focus on event bus step output
    /*
    std::cout << "\n--- L2 Snapshot (" << event.symbol << ") at " << ModelEvents::format_timestamp(event.ingress_ts) << " ---\n";
    std::cout << "  BIDS (" << event.bids.size() << " levels):\n";
    int bid_levels_to_print = std::min((size_t)5, event.bids.size());
    for (int i = 0; i < bid_levels_to_print; ++i) {
        const auto& level = event.bids[i];
        std::cout << "    P: " << std::fixed << std::setprecision(2) << ModelEvents::price_to_float(level.first)
                  << " | Q: " << std::fixed << std::setprecision(4) << ModelEvents::quantity_to_float(level.second) << "\n";
    }
    if (event.bids.size() > bid_levels_to_print) std::cout << "    ...\n";

    std::cout << "  ASKS (" << event.asks.size() << " levels):\n";
    int ask_levels_to_print = std::min((size_t)5, event.asks.size());
    for (int i = 0; i < ask_levels_to_print; ++i) {
        const auto& level = event.asks[i];
        std::cout << "    P: " << std::fixed << std::setprecision(2) << ModelEvents::price_to_float(level.first)
                  << " | Q: " << std::fixed << std::setprecision(4) << ModelEvents::quantity_to_float(level.second) << "\n";
    }
    if (event.asks.size() > ask_levels_to_print) std::cout << "    ...\n";
    std::cout << "--- End L2 Snapshot ---\n" << std::endl;
    */
}


int main() {
    // Set desired log level to suppress INFO, DEBUG, WARNING messages
    EventBusSystem::LoggerConfig::G_CURRENT_LOG_LEVEL = EventBusSystem::LogLevel::INFO;

    ModelEvents::SymbolType sim_symbol = "TEST/USD";

    std::cout << "Initializing TradingSimulation for symbol: " << sim_symbol << std::endl;
    // Initialize simulation with the L2 callback
    TradingSimulation sim(sim_symbol, l2_console_printer);
    TradingSimulation::SimulationEventBus& bus = sim.get_event_bus(); // Get a reference to the event bus

    // --- Create and Add Traders ---
    TradingSimulation::AgentId zimm1_id = 101;
    auto zimm1 = std::make_shared<trading::algo::ZeroIntelligenceMarketMaker>(
        zimm1_id,
        sim_symbol, // Exchange name for the ZIMM
        5,   // min_spread_bps
        15,  // max_spread_bps
        0.1, // min_order_size_float
        1.0, // max_order_size_float
        3,   // imbalance_levels
        5    // max_imbalance_adj_bps
    );
    std::cout << "Adding Trader ID: " << sim.add_trader(zimm1) << std::endl;

    TradingSimulation::AgentId zimm2_id = 102;
     auto zimm2 = std::make_shared<trading::algo::ZeroIntelligenceMarketMaker>(
        zimm2_id,
        sim_symbol,
        6, 12, 0.2, 0.8, 2, 4
    );
    std::cout << "Adding Trader ID: " << sim.add_trader(zimm2) << std::endl;

    std::cout << "Simulation setup complete. Starting steps.\n";

    // --- External Event Injection and Trader Commanding ---

    // 1. Inject an initial LTwoOrderBookEvent
    ModelEvents::OrderBookLevel initial_bids, initial_asks;
    initial_bids.push_back({ModelEvents::float_to_price(99.90), ModelEvents::float_to_quantity(10.0)});
    initial_bids.push_back({ModelEvents::float_to_price(99.80), ModelEvents::float_to_quantity(5.0)});
    initial_asks.push_back({ModelEvents::float_to_price(100.10), ModelEvents::float_to_quantity(8.0)});
    initial_asks.push_back({ModelEvents::float_to_price(100.20), ModelEvents::float_to_quantity(12.0)});

    EventBusSystem::Timestamp initial_event_time = bus.get_current_time() + std::chrono::microseconds(10); // Slightly in the future
    auto initial_l2_event = std::make_shared<const ModelEvents::LTwoOrderBookEvent>(
        initial_event_time, // created_ts
        sim_symbol,
        initial_event_time, // exchange_ts
        initial_event_time, // ingress_ts
        std::move(initial_bids),
        std::move(initial_asks)
    );
    // Publish directly to the bus. Publisher ID 0 can represent the "exchange feed".
    bus.publish(0, "LTwoOrderBookEvent." + sim_symbol, initial_l2_event, "market_data_feed");
    std::cout << "Published initial LTwoOrderBookEvent.\n";

    // Run a few steps to process the L2 event and let ZIMMs react
    sim.run(5);

    // 2. Command zimm1 to place a limit order
    auto trader1_interface_opt = sim.get_trader(zimm1_id);
    if (trader1_interface_opt) {
        // Cast to the concrete ZIMM type to call its order creation methods
        auto zimm1_concrete = std::dynamic_pointer_cast<trading::algo::ZeroIntelligenceMarketMaker>(*trader1_interface_opt);
        if (zimm1_concrete) {
            std::cout << "Commanding Trader " << zimm1_id << " to place a BUY limit order.\n";
            zimm1_concrete->create_limit_order(
                sim_symbol,
                ModelEvents::Side::BUY,
                ModelEvents::float_to_price(99.50),   // price
                ModelEvents::float_to_quantity(0.5),  // quantity
                std::chrono::seconds(60)              // timeout
            );
        } else {
             std::cerr << "Error: Could not cast trader " << zimm1_id << " to ZeroIntelligenceMarketMaker.\n";
        }
    }

    // Run more steps
    sim.run(100);

    // 3. Command zimm2 to place a market order
    auto trader2_interface_opt = sim.get_trader(zimm2_id);
     if (trader2_interface_opt) {
        auto zimm2_concrete = std::dynamic_pointer_cast<trading::algo::ZeroIntelligenceMarketMaker>(*trader2_interface_opt);
        if (zimm2_concrete) {
            std::cout << "Commanding Trader " << zimm2_id << " to place a SELL market order.\n";
            zimm2_concrete->create_market_order(
                sim_symbol,
                ModelEvents::Side::SELL,
                ModelEvents::float_to_quantity(0.3), // quantity
                std::chrono::seconds(30)             // timeout
            );
        }
    }

    // Run more steps
    sim.run(20);

    // 4. Send a Bang event
    std::cout << "Sending Bang event.\n";
    EventBusSystem::Timestamp bang_time = bus.get_current_time() + std::chrono::microseconds(100);
    auto bang_event = std::make_shared<const ModelEvents::Bang>(bang_time);
    bus.publish(0, "Bang", bang_event, "global_reset_stream");

    // Run a few more steps to see Bang processed
    sim.run(5);

    std::cout << "\n--- Main simulation finished ---\n";
    return 0;
}