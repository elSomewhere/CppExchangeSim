// main_realtime.cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip> // For std::setw, std::fixed, std::setprecision
#include <numeric>
#include <optional>
#include <memory>
#include <string>
#include <variant> // For std::visit
#include <thread>  // For std::this_thread::sleep_for

// Custom Project Headers
#include "src/Model.h"
#include "src/EventBus.h"
#include "src/Globals.h"
#include "src/TradingSimulation.h" // This now defines EventPrinterHook
#include "src/ZeroIntelligenceMarketMaker.h"
#include "src/RealTimeBus.h" // Include RealTimeBus


// ZIMMParams struct and sample_agent_params_cpp can remain the same
struct ZIMMParams {
    int min_spread_bps;
    int max_spread_bps;
    double min_order_size_float;
    double max_order_size_float;
    size_t imbalance_levels;
    int max_imbalance_adj_bps;
};

struct ZIMMSpreadProfileConfig {
    std::pair<int, int> min_spread_bps_sampling_range;
    std::pair<int, int> max_spread_bps_sampling_range;
    double weight;
};

ZIMMParams sample_agent_params_cpp(
        std::default_random_engine& rng,
        const std::vector<ZIMMSpreadProfileConfig>& spread_profiles,
        const std::pair<double, double>& min_order_size_range,
        const std::pair<double, double>& max_order_size_range,
        const std::pair<int, int>& imbalance_levels_range,
        const std::pair<int, int>& max_imbalance_adj_bps_range
) {
    const ZIMMSpreadProfileConfig* selected_profile_ptr = nullptr;
    if (spread_profiles.empty()) {
        std::cerr << "Error: No spread profiles provided to sample_agent_params_cpp. Using a default." << std::endl;
        static const ZIMMSpreadProfileConfig default_fallback_profile = {{1, 5}, {6, 10}, 1.0};
        selected_profile_ptr = &default_fallback_profile;
    } else {
        std::vector<double> weights;
        for (const auto& profile : spread_profiles) {
            weights.push_back(profile.weight);
        }
        std::discrete_distribution<int> profile_dist(weights.begin(), weights.end());
        int selected_profile_idx = profile_dist(rng);
        selected_profile_ptr = &spread_profiles[selected_profile_idx];
    }
    const ZIMMSpreadProfileConfig& selected_profile = *selected_profile_ptr;

    std::uniform_int_distribution<int> min_spread_dist(selected_profile.min_spread_bps_sampling_range.first, selected_profile.min_spread_bps_sampling_range.second);
    int min_spread = min_spread_dist(rng);

    int sample_range_for_max_spread_A = std::max(min_spread + 1, selected_profile.max_spread_bps_sampling_range.first);
    int sample_range_for_max_spread_B = selected_profile.max_spread_bps_sampling_range.second;

    if (sample_range_for_max_spread_A > sample_range_for_max_spread_B) {
        sample_range_for_max_spread_B = sample_range_for_max_spread_A;
    }

    std::uniform_int_distribution<int> max_spread_dist(sample_range_for_max_spread_A, sample_range_for_max_spread_B);
    int max_spread = max_spread_dist(rng);

    std::uniform_real_distribution<double> min_size_dist(min_order_size_range.first, min_order_size_range.second);
    double min_size = min_size_dist(rng);

    std::uniform_real_distribution<double> max_size_dist(
            std::max(min_size, max_order_size_range.first),
            max_order_size_range.second
    );
    double max_size = max_size_dist(rng);
    if (max_size < min_size) max_size = min_size;

    std::uniform_int_distribution<int> imbalance_levels_dist(imbalance_levels_range.first, imbalance_levels_range.second);
    size_t imbalance_levels = static_cast<size_t>(imbalance_levels_dist(rng));

    std::uniform_int_distribution<int> max_imbalance_adj_dist(max_imbalance_adj_bps_range.first, max_imbalance_adj_bps_range.second);
    int max_imbalance_adj_bps = max_imbalance_adj_dist(rng);

    return {min_spread, max_spread, min_size, max_size, imbalance_levels, max_imbalance_adj_bps};
}


void seed_order_book_cpp(TradingSimulation& sim, int depth) {
    double bid_mid = 50000.0;
    double ask_mid = bid_mid + 200.0;

    TradingSimulation::FloatOrderBookLevel bids_float;
    for (int i = 0; i < depth; ++i) {
        bids_float.push_back({bid_mid - 20.0 * i, 1.0 + 0.2 * i});
    }

    TradingSimulation::FloatOrderBookLevel asks_float;
    for (int i = 0; i < depth; ++i) {
        asks_float.push_back({ask_mid + 20.0 * i, 1.0 + 0.2 * i});
    }
    sim.create_order_book_snapshot(std::move(bids_float), std::move(asks_float));
}

void broadcast_small_wiggle_cpp(TradingSimulation& sim, std::default_random_engine& rng) {
    std::uniform_real_distribution<double> mid_adj_dist(-25.0, 25.0);
    std::uniform_real_distribution<double> spread_adj_dist(2.0, 8.0);
    double mid_adj = mid_adj_dist(rng);

    TradingSimulation::FloatOrderBookLevel bids_float;
    for (int i = 0; i < 3; ++i) {
        bids_float.push_back({49990.0 + mid_adj - spread_adj_dist(rng) * i, 0.5 + 0.05 * i});
    }
    TradingSimulation::FloatOrderBookLevel asks_float;
    for (int i = 0; i < 3; ++i) {
        asks_float.push_back({50010.0 + mid_adj + spread_adj_dist(rng) * i, 0.5 + 0.05 * i});
    }
    sim.create_order_book_snapshot(std::move(bids_float), std::move(asks_float));
}

void warm_up_agent_cpp(TradingSimulation& sim, const std::pair<int, int>& warmup_range_ms, std::default_random_engine& rng) {
    if (warmup_range_ms.first == 0 && warmup_range_ms.second == 0) return;

    std::uniform_int_distribution<int> warmup_dist_ms(warmup_range_ms.first, warmup_range_ms.second);
    EventBusSystem::Duration target_duration = std::chrono::milliseconds(warmup_dist_ms(rng));
    EventBusSystem::Timestamp start_ts = sim.get_event_bus().get_current_time();

    while (sim.get_event_bus().get_current_time() - start_ts < target_duration) {
        if (sim.get_event_bus().get_event_queue_size() == 0) break;
        sim.get_event_bus().step();
    }
}

// <<<< NEW FUNCTION to print L2 book >>>>
void print_l2_top_10(const ModelEvents::LTwoOrderBookEvent& event) {
    // Ensure cout is flushed before potential concurrent logging from other threads/parts of the bus
    std::cout << std::unitbuf; // or std::cout.flush() after each block

    std::cout << "\n--- L2 Order Book Snapshot (Top 10) for " << event.symbol << " ---" << std::endl;
    std::cout << "Exchange TS: " << ModelEvents::format_optional_timestamp(event.exchange_ts)
              << ", Ingress TS: " << ModelEvents::format_timestamp(event.ingress_ts) << std::endl;

    std::cout << std::fixed << std::setprecision(4); // For float price/qty output

    // Print Asks (typically shown above bids)
    std::cout << "ASKS (Price -- Quantity):" << std::endl;
    int levels_to_print_asks = 0;
    // Asks are usually sorted ascending, print from start
    for (const auto& level : event.asks) {
        if (levels_to_print_asks >= 10) break;
        std::cout << "  " << std::setw(12) << ModelEvents::price_to_float(level.first)
                  << " -- " << std::setw(12) << ModelEvents::quantity_to_float(level.second) << std::endl;
        levels_to_print_asks++;
    }
    if (event.asks.empty()) {
        std::cout << "  (No asks)" << std::endl;
    }
    if (levels_to_print_asks < event.asks.size()) {
        std::cout << "  (... " << (event.asks.size() - levels_to_print_asks) << " more ask levels)" << std::endl;
    }


    // Print Bids
    std::cout << "BIDS (Price -- Quantity):" << std::endl;
    int levels_to_print_bids = 0;
    // Bids are usually sorted descending, print from start
    for (const auto& level : event.bids) {
        if (levels_to_print_bids >= 10) break;
        std::cout << "  " << std::setw(12) << ModelEvents::price_to_float(level.first)
                  << " -- " << std::setw(12) << ModelEvents::quantity_to_float(level.second) << std::endl;
        levels_to_print_bids++;
    }
    if (event.bids.empty()) {
        std::cout << "  (No bids)" << std::endl;
    }
    if (levels_to_print_bids < event.bids.size()) {
        std::cout << "  (... " << (event.bids.size() - levels_to_print_bids) << " more bid levels)" << std::endl;
    }
    std::cout << "----------------------------------------" << std::endl << std::endl;
    std::cout << std::nounitbuf; // Restore default buffering
}


int main() {
    LoggerConfig::G_CURRENT_LOG_LEVEL = LogLevel::DEBUG;

    int agents = 10; // Reduced for easier observation of L2 prints
    ModelEvents::SymbolType symbol = "BTC/USD";
    int seed = 47;
    double speed_factor = 1.0; // Slow down for easier observation

    std::string timeout_dist = "lognormal"; // Simpler timeout for testing
    double median_timeout_seconds = 5.0;    // Shorter timeouts -> more activity
    double sigma_timeout = 0.8;
    double pareto_alpha = 1.5; // Not used if timeout_dist is "lognormal"
    double pareto_scale = 5.0; // Not used
    double tail_mix = 0.1;     // Not used
    double min_timeout_s = 1.0; // Min timeout
    double max_timeout_s = 60.0; // Max timeout

    std::pair<int, int> warmup_per_agent_ms = {0, 0};
    int order_book_seed_levels = 5;

    std::vector<ZIMMSpreadProfileConfig> spread_configurations_left_heavy = {
            {{1, 5}, {6, 10}, 0.7},
            {{10, 20}, {21, 40}, 0.2},
            {{50, 70}, {71, 100}, 0.1}
    };
    const auto& chosen_spread_configurations = spread_configurations_left_heavy;

    std::pair<double, double> min_order_size_range = {0.01, 0.1}; // Smaller orders
    std::pair<double, double> max_order_size_range = {0.1, 0.5};
    std::pair<int, int> imbalance_levels_range = {1, 3};
    std::pair<int, int> max_imbalance_adj_bps_range = {2, 10};

    std::default_random_engine main_rng(seed);

    // <<<< MODIFIED >>>> Create EventPrinterHook with the L2 printing callback
    std::unique_ptr<EventPrinterHook> my_printer_hook =
            std::make_unique<EventPrinterHook>(print_l2_top_10);

    // <<<< MODIFIED >>>> Pass the configured hook to TradingSimulation
    TradingSimulation sim(symbol, seed, std::move(my_printer_hook));
    RealTimeBus rtb(sim.get_event_bus());

    std::vector<std::shared_ptr<trading::algo::ZeroIntelligenceMarketMaker>> trader_pool;

    std::cout << "Creating " << agents << " ZIMM agents with real-time parameters..." << std::endl;
    for (int i = 0; i < agents; ++i) {
        ZIMMParams params = sample_agent_params_cpp(
                main_rng, chosen_spread_configurations,
                min_order_size_range, max_order_size_range,
                imbalance_levels_range, max_imbalance_adj_bps_range
        );
        trader_pool.push_back(
                std::make_shared<trading::algo::ZeroIntelligenceMarketMaker>(
                        symbol,
                        params.min_spread_bps, params.max_spread_bps,
                        params.min_order_size_float, params.max_order_size_float,
                        params.imbalance_levels, params.max_imbalance_adj_bps,
                        timeout_dist, median_timeout_seconds, sigma_timeout,
                        pareto_alpha, pareto_scale, tail_mix,
                        min_timeout_s, max_timeout_s,
                        static_cast<unsigned int>(seed + 1000 + i)
                )
        );
    }
    std::shuffle(trader_pool.begin(), trader_pool.end(), main_rng);
    std::cout << "Trader pool created and shuffled." << std::endl;

    std::cout << "Adding agents BEFORE initial book seed..." << std::endl;
    for (const auto& trader_ptr : trader_pool) {
        sim.add_trader(trader_ptr);
        warm_up_agent_cpp(sim, warmup_per_agent_ms, main_rng);
    }
    std::cout << "All agents added." << std::endl;

    std::cout << "Seeding initial order book AFTER agents are added..." << std::endl;
    seed_order_book_cpp(sim, order_book_seed_levels); // This will publish an L2 event

    std::cout << "Processing initial agent reactions to seed book..." << std::endl;
    int initial_steps = 0;
    // The L2 print should happen when seed_order_book_cpp's event is pre-published.
    // ZIMMs will react and place orders, which should also trigger L2 prints via ExchangeAdapter.
    while(sim.get_event_bus().get_event_queue_size() > 0 && initial_steps < agents * 20) { // More steps
        sim.get_event_bus().step();
        initial_steps++;
    }
    std::cout << "Initial reactions processed (" << initial_steps << " steps). "
              << "Queue size: " << sim.get_event_bus().get_event_queue_size() << std::endl;

    // Broadcast a wiggle to generate more L2 events before starting the main loop
    if (sim.get_event_bus().get_event_queue_size() == 0 || initial_steps > 0) { // Ensure some state exists or initial reactions happened
        std::cout << "Broadcasting a small wiggle to the order book..." << std::endl;
        broadcast_small_wiggle_cpp(sim, main_rng);
        int wiggle_steps = 0;
        while(sim.get_event_bus().get_event_queue_size() > 0 && wiggle_steps < agents * 10) {
            sim.get_event_bus().step();
            wiggle_steps++;
        }
        std::cout << "Wiggle reactions processed (" << wiggle_steps << " steps). "
                  << "Queue size: " << sim.get_event_bus().get_event_queue_size() << std::endl;
    }


    std::cout << "Starting RealTimeBus processing with speed_factor: " << speed_factor << std::endl;
    std::cout << "Simulation will run until event queue is empty for a period or an error occurs." << std::endl;
    std::cout << "Press Ctrl+C to stop manually if needed." << std::endl;

    EventBusSystem::Timestamp start_ts_real_time_loop = sim.get_event_bus().get_current_time();
    auto wall_clock_start = std::chrono::steady_clock::now();

    rtb.run(speed_factor);

    auto wall_clock_end = std::chrono::steady_clock::now();
    EventBusSystem::Duration sim_elapsed_duration = sim.get_event_bus().get_current_time() - start_ts_real_time_loop;
    std::chrono::duration<double> wall_clock_elapsed_seconds = wall_clock_end - wall_clock_start;

    std::cout << "\n--- RealTimeBus Simulation Finished ---" << std::endl;
    std::cout << "Wall clock time elapsed: " << std::fixed << std::setprecision(3) << wall_clock_elapsed_seconds.count() << "s" << std::endl;
    std::cout << "Simulated time elapsed during RTB run: " << ModelEvents::duration_to_float_seconds(sim_elapsed_duration) << "s" << std::endl;
    std::cout << "Final simulation time: " << ModelEvents::format_timestamp(sim.get_event_bus().get_current_time()) << std::endl;
    std::cout << "Final event queue size: " << sim.get_event_bus().get_event_queue_size() << std::endl;

    return 0;
}