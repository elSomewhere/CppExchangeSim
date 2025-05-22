// file: main.cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <optional>
#include <memory>
#include <string>
#include <variant> // For std::visit

// Custom Project Headers
#include "src/Model.h"
#include "src/EventBus.h"
#include "src/Globals.h"
#include "src/Simulation.h"
#include "src/ZeroIntelligenceMarketMaker.h"

using MyScheduledEventType = EventBusSystem::IEventProcessor<
        ModelEvents::CheckLimitOrderExpirationEvent, ModelEvents::Bang, ModelEvents::LTwoOrderBookEvent,
        ModelEvents::LimitOrderEvent, ModelEvents::MarketOrderEvent, ModelEvents::PartialCancelLimitOrderEvent,
        ModelEvents::PartialCancelMarketOrderEvent, ModelEvents::FullCancelLimitOrderEvent, ModelEvents::FullCancelMarketOrderEvent,
        ModelEvents::LimitOrderAckEvent, ModelEvents::MarketOrderAckEvent, ModelEvents::FullCancelLimitOrderAckEvent,
        ModelEvents::FullCancelMarketOrderAckEvent, ModelEvents::PartialCancelLimitAckEvent, ModelEvents::PartialCancelMarketAckEvent,
        ModelEvents::PartialCancelLimitOrderRejectEvent, ModelEvents::FullCancelLimitOrderRejectEvent,
        ModelEvents::PartialCancelMarketOrderRejectEvent, ModelEvents::FullCancelMarketOrderRejectEvent,
        ModelEvents::LimitOrderRejectEvent, ModelEvents::MarketOrderRejectEvent, ModelEvents::MarketOrderExpiredEvent,
        ModelEvents::LimitOrderExpiredEvent, ModelEvents::PartialFillLimitOrderEvent, ModelEvents::PartialFillMarketOrderEvent,
        ModelEvents::FullFillLimitOrderEvent, ModelEvents::FullFillMarketOrderEvent, ModelEvents::TradeEvent,
        ModelEvents::TriggerExpiredLimitOrderEvent, ModelEvents::RejectTriggerExpiredLimitOrderEvent,
        ModelEvents::AckTriggerExpiredLimitOrderEvent
>::ScheduledEvent;

// --- L2 Snapshot Callback (prints to console) ---
void l2_console_printer_main(const ModelEvents::LTwoOrderBookEvent& event) {
//    if (EventBusSystem::LoggerConfig::G_CURRENT_LOG_LEVEL > EventBusSystem::LogLevel::INFO) return;
//
//    std::cout << "\n--- L2 Snapshot (" << event.symbol << ") at " << ModelEvents::format_timestamp(event.ingress_ts) << " ---\n";
//    std::cout << "  BIDS (" << event.bids.size() << " levels):\n";
//    int bid_levels_to_print = std::min(static_cast<size_t>(5), event.bids.size());
//    for (int i = 0; i < bid_levels_to_print; ++i) {
//        const auto& level = event.bids[i];
//        std::cout << "    P: " << std::fixed << std::setprecision(2) << ModelEvents::price_to_float(level.first)
//                  << " | Q: " << std::fixed << std::setprecision(4) << ModelEvents::quantity_to_float(level.second) << "\n";
//    }
//    if (event.bids.size() > static_cast<size_t>(bid_levels_to_print)) std::cout << "    ...\n";
//
//    std::cout << "  ASKS (" << event.asks.size() << " levels):\n";
//    int ask_levels_to_print = std::min(static_cast<size_t>(5), event.asks.size());
//    for (int i = 0; i < ask_levels_to_print; ++i) {
//        const auto& level = event.asks[i];
//        std::cout << "    P: " << std::fixed << std::setprecision(2) << ModelEvents::price_to_float(level.first)
//                  << " | Q: " << std::fixed << std::setprecision(4) << ModelEvents::quantity_to_float(level.second) << "\n";
//    }
//    if (event.asks.size() > static_cast<size_t>(ask_levels_to_print)) std::cout << "    ...\n";
//    std::cout << "--- End L2 Snapshot ---\n" << std::endl;
}

struct ZIMMParams {
    int min_spread_bps;
    int max_spread_bps;
    double min_order_size_float;
    double max_order_size_float;
    size_t imbalance_levels;
    int max_imbalance_adj_bps;
};

ZIMMParams sample_agent_params_cpp(
        std::default_random_engine& rng,
        const std::pair<int, int>& min_spread_bps_range,
        const std::pair<int, int>& max_spread_bps_range,
        const std::pair<double, double>& min_order_size_range,
        const std::pair<double, double>& max_order_size_range,
        const std::pair<int, int>& imbalance_levels_range,
        const std::pair<int, int>& max_imbalance_adj_bps_range
) {
    std::uniform_int_distribution<int> min_spread_dist(min_spread_bps_range.first, min_spread_bps_range.second);
    int min_spread = min_spread_dist(rng);

    std::uniform_int_distribution<int> max_spread_dist(
            std::max(min_spread + 1, max_spread_bps_range.first),
            max_spread_bps_range.second
    );
    int max_spread = max_spread_dist(rng);
    if (max_spread < min_spread) max_spread = min_spread + 1;

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
        bids_float.push_back({bid_mid - 10.0 * i, 1.0 + 0.1 * i});
    }

    TradingSimulation::FloatOrderBookLevel asks_float;
    for (int i = 0; i < depth; ++i) {
        asks_float.push_back({ask_mid + 10.0 * i, 1.0 + 0.1 * i});
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
    std::uniform_int_distribution<int> warmup_dist_ms(warmup_range_ms.first, warmup_range_ms.second);
    EventBusSystem::Duration target_duration = std::chrono::milliseconds(warmup_dist_ms(rng));
    EventBusSystem::Timestamp start_ts = sim.get_event_bus().get_current_time();

    while (sim.get_event_bus().get_current_time() - start_ts < target_duration) {
        auto processed_event_opt = sim.get_event_bus().step();
        if (!processed_event_opt && sim.get_event_bus().get_event_queue_size() == 0) {
            break;
        }
    }
}

bool within_limits_cpp(
        int steps_executed,
        const std::optional<int>& step_cap,
        EventBusSystem::Timestamp start_us, // Note: Python uses start_us, C++ uses start_ts_main_loop
        const std::optional<EventBusSystem::Timestamp>& time_cap_target_ts,
        const TradingSimulation& sim
) {
    bool steps_ok = !step_cap.has_value() || steps_executed < step_cap.value();
    bool time_ok = !time_cap_target_ts.has_value() || sim.get_event_bus().get_current_time() < time_cap_target_ts.value();
    return steps_ok && time_ok;
}

// MODIFIED: Pass bus by reference for get_topic_string and get_stream_string
void print_scheduled_event_details(
        const std::optional<MyScheduledEventType>& scheduled_event_opt, // CORRECTED: Qualified with EventBusSystem::
        const TradingSimulation::SimulationEventBus& bus, // Pass bus by const reference
        const std::string& context
) {
    if (EventBusSystem::LoggerConfig::G_CURRENT_LOG_LEVEL > EventBusSystem::LogLevel::INFO) return;

    if (scheduled_event_opt) {
        const auto& se = *scheduled_event_opt;
//        std::cout << "[" << context << "] Processed Event (Seq: " << se.sequence_number
//                  << ", Time: " << ModelEvents::format_timestamp(se.scheduled_time)
//                  << ", Pub: " << se.publisher_id << ", Sub: " << se.subscriber_id
//                  << ", Topic: " << bus.get_topic_string(se.topic) // Use passed bus
//                  << ", Stream: " << bus.get_stream_string(se.stream_id) // Use passed bus
//                  << "): ";
        std::visit([](const auto& ev_ptr) {
            if (ev_ptr) {
//                std::cout << ev_ptr->to_string();
            } else {
                std::cout << "Null event pointer in variant";
            }
        }, se.event);
        std::cout << std::endl;
    }
}


int main() {
    int agents = 100;
    ModelEvents::SymbolType symbol = "BTC/USD";
    std::optional<int> steps_cap = std::nullopt;
    std::optional<double> sim_time_seconds = 3600.0 * 24.0 * 7.0;
    int seed = 47;
    std::pair<int, int> warmup_per_agent_ms = {0, 0};
    int order_book_seed_levels = 5;

    std::pair<int, int> min_spread_bps_range = {5, 100};
    std::pair<int, int> max_spread_bps_range = {100, 1500};
    std::pair<double, double> min_order_size_range = {0.1, 5.0};
    std::pair<double, double> max_order_size_range = {10.0, 70.0};
    std::pair<int, int> imbalance_levels_range = {2, 12};
    std::pair<int, int> max_imbalance_adj_bps_range = {3, 25};

    std::string timeout_dist = "pareto";
    double median_timeout_seconds = 60.0;
    double sigma_timeout = 1.4;
    double pareto_alpha = 1.1;
    double pareto_scale = 3600.0;
    double tail_mix = 0.1;
    double min_timeout_s = 5.0;
    double max_timeout_s = 3600.0 * 24.0;

    EventBusSystem::LoggerConfig::G_CURRENT_LOG_LEVEL = EventBusSystem::LogLevel::INFO;

    std::default_random_engine main_rng(seed);

    TradingSimulation sim(symbol, l2_console_printer_main, seed); // Pass seed to TradingSimulation for the bus
    TradingSimulation::SimulationEventBus& bus_ref = sim.get_event_bus(); // Get reference for print_scheduled_event_details

    std::vector<std::shared_ptr<trading::algo::ZeroIntelligenceMarketMaker>> trader_pool;
    EventBusSystem::AgentId next_agent_id = 100;

    std::cout << "Creating " << agents << " ZIMM agents..." << std::endl;
    for (int i = 0; i < agents; ++i) {
        ZIMMParams params = sample_agent_params_cpp(
                main_rng, min_spread_bps_range, max_spread_bps_range,
                min_order_size_range, max_order_size_range,
                imbalance_levels_range, max_imbalance_adj_bps_range
        );
        trader_pool.push_back(
                std::make_shared<trading::algo::ZeroIntelligenceMarketMaker>(
                        next_agent_id + i,
                        symbol,
                        params.min_spread_bps,
                        params.max_spread_bps,
                        params.min_order_size_float,
                        params.max_order_size_float,
                        params.imbalance_levels,
                        params.max_imbalance_adj_bps,
                        timeout_dist, median_timeout_seconds, sigma_timeout,
                        pareto_alpha, pareto_scale, tail_mix,
                        min_timeout_s, max_timeout_s,
                        static_cast<unsigned int>(seed + i) // Pass seed to ZIMM
                )
        );
    }
    std::shuffle(trader_pool.begin(), trader_pool.end(), main_rng);
    std::cout << "Trader pool created and shuffled." << std::endl;

    std::cout << "Seeding initial order book..." << std::endl;
    seed_order_book_cpp(sim, order_book_seed_levels);

    std::cout << "Processing seed order book event..." << std::endl;
    auto step_res_seed = bus_ref.step();
    print_scheduled_event_details(step_res_seed, bus_ref, "Seed OB");


    std::cout << "Adding and warming up agents..." << std::endl;
    for (const auto& trader_ptr : trader_pool) {
        broadcast_small_wiggle_cpp(sim, main_rng);
        auto step_res_wiggle = bus_ref.step();
        print_scheduled_event_details(step_res_wiggle, bus_ref, "Wiggle");

        sim.add_trader(trader_ptr);
        warm_up_agent_cpp(sim, warmup_per_agent_ms, main_rng);
    }
    std::cout << "All agents added and warmed up." << std::endl;

    std::optional<EventBusSystem::Timestamp> time_cap_target_ts = std::nullopt;
    EventBusSystem::Timestamp start_ts_main_loop = bus_ref.get_current_time();

    if (sim_time_seconds.has_value()) {
        EventBusSystem::Duration sim_duration = ModelEvents::float_seconds_to_duration(sim_time_seconds.value());
        time_cap_target_ts = start_ts_main_loop + sim_duration;
    }

    int steps_executed = 0;
    std::cout << "Starting main simulation loop..." << std::endl;

    while (within_limits_cpp(steps_executed, steps_cap, start_ts_main_loop, time_cap_target_ts, sim)) {
        if (bus_ref.get_event_queue_size() == 0) {
            std::cout << "Event queue depleted – terminating main loop early." << std::endl;
            EventBusSystem::LogMessage(EventBusSystem::LogLevel::INFO, "main", "Event queue depleted – terminating early.");
            break;
        }
        auto processed_event_opt = bus_ref.step();
        print_scheduled_event_details(processed_event_opt, bus_ref, "MainLoop");

        steps_executed++;
        if (steps_cap.has_value() && steps_executed >= steps_cap.value()) {
            break;
        }
    }

    EventBusSystem::Duration elapsed_duration = bus_ref.get_current_time() - start_ts_main_loop;
    double elapsed_seconds_sim = ModelEvents::duration_to_float_seconds(elapsed_duration);

    std::cout << "Simulation finished – processed " << steps_executed
              << " steps (" << std::fixed << std::setprecision(3) << elapsed_seconds_sim
              << "s simulated time)." << std::endl;
    EventBusSystem::LogMessage(EventBusSystem::LogLevel::INFO, "main",
                               "Simulation finished – processed " + std::to_string(steps_executed) +
                               " steps (" + std::to_string(elapsed_seconds_sim) + "s simulated time).");

    std::cout << "Final event queue size: " << bus_ref.get_event_queue_size() << std::endl;

    return 0;
}