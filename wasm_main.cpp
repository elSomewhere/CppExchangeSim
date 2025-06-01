// wasm_main.cpp – WebAssembly version of the exchange simulation
// -----------------------------------------------------------------------------
// This file provides JavaScript bindings for the exchange simulation using
// Emscripten. All parameters from main.cpp are configurable via JavaScript.
// -----------------------------------------------------------------------------

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <string>
#include <variant>
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
#include "RealTimeBusWasm.h"
#include "L2WasmHook.h"

// ─────────────────────────────────────────────────────────────────────────────
// 1. Beta‑distribution helpers (same as main.cpp)
// -----------------------------------------------------------------------------
inline double sampleBeta(std::default_random_engine &rng,
                         double alpha, double beta)
{
    std::gamma_distribution<double> g1(alpha, 1.0);
    std::gamma_distribution<double> g2(beta , 1.0);
    double x = g1(rng);
    double y = g2(rng);
    return (x + y == 0.0) ? 0.5 : x / (x + y);
}

inline std::pair<int,int> draw_spread_range_beta(std::default_random_engine &rng,
                                                 int global_low, int global_high,
                                                 double alpha_loc, double beta_loc,
                                                 double alpha_wid, double beta_wid,
                                                 double w_min_rel = 0.0)
{
    const int span = global_high - global_low;
    if (span <= 0) return {global_low, global_high};

    double c = sampleBeta(rng, alpha_loc, beta_loc);
    double w = std::max(sampleBeta(rng, alpha_wid, beta_wid), w_min_rel);
    double w_max = std::min(c, 1.0 - c);
    double half = w * w_max;
    double lo_rel = c - half;
    double hi_rel = c + half;

    int lo = global_low + static_cast<int>(std::round(lo_rel * span));
    int hi = global_low + static_cast<int>(std::round(hi_rel * span));
    if (lo == hi) hi = std::min(hi + 1, global_high);
    return {lo, hi};
}

// ─── Profile struct ──────────────────────────────────────────────────────────
struct ZIMMBetaSpreadProfile {
    int global_low;
    int global_high;
    double alpha_loc;
    double beta_loc;
    double alpha_wid;
    double beta_wid;
    double weight;
};

// ─── Simulation parameters struct ────────────────────────────────────────────
struct SimulationParams {
    int agents = 100;
    std::string symbol = "BTC/USD";
    int seed = 47;
    double speed_factor = 100.0;
    
    // Order lifetime parameters
    std::string timeout_dist = "lognormal";
    double median_timeout_seconds = 5.0;
    double sigma_timeout = 0.8;
    double pareto_alpha = 1.5;
    double pareto_scale = 5.0;
    double tail_mix = 0.1;
    double min_timeout_s = 1.0;
    double max_timeout_s = 60.0;
    
    // Warmup and seeding
    int warmup_per_agent_ms_min = 0;
    int warmup_per_agent_ms_max = 0;
    int order_book_seed_levels = 5;
    
    // Order size ranges
    double min_order_size_min = 0.01;
    double min_order_size_max = 0.1;
    double max_order_size_min = 0.1;
    double max_order_size_max = 0.5;
    
    // Imbalance parameters
    int imbalance_levels_min = 1;
    int imbalance_levels_max = 3;
    int max_imbalance_adj_bps_min = 2;
    int max_imbalance_adj_bps_max = 10;
};

struct ZIMMParams {
    int min_spread_bps;
    int max_spread_bps;
    double min_order_size_float;
    double max_order_size_float;
    size_t imbalance_levels;
    int max_imbalance_adj_bps;
};

ZIMMParams sample_agent_params_cpp(
        std::default_random_engine &rng,
        const std::vector<ZIMMBetaSpreadProfile> &beta_spread_profiles,
        const std::pair<double,double> &min_order_size_range,
        const std::pair<double,double> &max_order_size_range,
        const std::pair<int,int> &imbalance_levels_range,
        const std::pair<int,int> &max_imbalance_adj_bps_range)
{
    const ZIMMBetaSpreadProfile *sel = nullptr;
    if (beta_spread_profiles.empty()) {
        static const ZIMMBetaSpreadProfile fallback = {1, 10, 1.0, 1.0, 1.0, 1.0, 1.0};
        sel = &fallback;
    } else {
        std::vector<double> w; w.reserve(beta_spread_profiles.size());
        for (auto &p : beta_spread_profiles) w.push_back(p.weight);
        std::discrete_distribution<int> pick(w.begin(), w.end());
        sel = &beta_spread_profiles[pick(rng)];
    }

    auto [min_spread, max_spread] = draw_spread_range_beta(
            rng, sel->global_low, sel->global_high,
            sel->alpha_loc, sel->beta_loc,
            sel->alpha_wid, sel->beta_wid);

    std::uniform_real_distribution<double> min_size_dist(min_order_size_range.first,
                                                         min_order_size_range.second);
    double min_size = min_size_dist(rng);

    std::uniform_real_distribution<double> max_size_dist(
            std::max(min_size, max_order_size_range.first),
            max_order_size_range.second);
    double max_size = max_size_dist(rng);
    if (max_size < min_size) max_size = min_size;

    std::uniform_int_distribution<int> imbalance_levels_dist(imbalance_levels_range.first,
                                                             imbalance_levels_range.second);
    size_t imbalance_levels = static_cast<size_t>(imbalance_levels_dist(rng));

    std::uniform_int_distribution<int> max_imbalance_adj_dist(max_imbalance_adj_bps_range.first,
                                                              max_imbalance_adj_bps_range.second);
    int max_imbalance_adj_bps = max_imbalance_adj_dist(rng);

    return {min_spread, max_spread, min_size, max_size, imbalance_levels, max_imbalance_adj_bps};
}

// ─── Utility functions (same as main.cpp) ────────────────────────────────────
void seed_order_book_cpp(TradingSimulation& sim, int depth)
{
    double bid_mid = 50000.0;
    double ask_mid = bid_mid + 200.0;

    TradingSimulation::FloatOrderBookLevel bids_float;
    for (int i = 0; i < depth; ++i)
        bids_float.push_back({bid_mid - 20.0 * i, 1.0 + 0.2 * i});

    TradingSimulation::FloatOrderBookLevel asks_float;
    for (int i = 0; i < depth; ++i)
        asks_float.push_back({ask_mid + 20.0 * i, 1.0 + 0.2 * i});

    sim.create_order_book_snapshot(std::move(bids_float), std::move(asks_float));
}

void broadcast_small_wiggle_cpp(TradingSimulation &sim, std::default_random_engine &rng)
{
    std::uniform_real_distribution<double> mid_adj_dist(-25.0, 25.0);
    std::uniform_real_distribution<double> spread_adj_dist(2.0, 8.0);
    double mid_adj = mid_adj_dist(rng);

    TradingSimulation::FloatOrderBookLevel bids_float;
    for (int i = 0; i < 3; ++i)
        bids_float.push_back({49990.0 + mid_adj - spread_adj_dist(rng) * i,
                              0.5 + 0.05 * i});

    TradingSimulation::FloatOrderBookLevel asks_float;
    for (int i = 0; i < 3; ++i)
        asks_float.push_back({50010.0 + mid_adj + spread_adj_dist(rng) * i,
                              0.5 + 0.05 * i});

    sim.create_order_book_snapshot(std::move(bids_float), std::move(asks_float));
}

void warm_up_agent_cpp(TradingSimulation &sim,
                       const std::pair<int,int> &warmup_range_ms,
                       std::default_random_engine &rng)
{
    if (warmup_range_ms.first == 0 && warmup_range_ms.second == 0) return;

    std::uniform_int_distribution<int> warmup_dist_ms(warmup_range_ms.first,
                                                      warmup_range_ms.second);
    EventBusSystem::Duration target = std::chrono::milliseconds(warmup_dist_ms(rng));
    EventBusSystem::Timestamp start = sim.get_event_bus().get_current_time();

    while (sim.get_event_bus().get_current_time() - start < target) {
        if (sim.get_event_bus().get_event_queue_size() == 0) break;
        sim.get_event_bus().step();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Main simulation class for WASM
// -----------------------------------------------------------------------------
class ExchangeSimulation {
private:
    std::unique_ptr<TradingSimulation> sim_;
    std::unique_ptr<RealTimeBusWasm> rtb_;
    std::unique_ptr<L2WasmHook> l2_hook_;
    std::vector<std::shared_ptr<trading::algo::ZeroIntelligenceMarketMaker>> trader_pool_;
    SimulationParams params_;
    std::vector<ZIMMBetaSpreadProfile> spread_profiles_;
    bool is_running_;
    bool is_initialized_;

public:
    ExchangeSimulation() : is_running_(false), is_initialized_(false) {
        LoggerConfig::G_CURRENT_LOG_LEVEL = LogLevel::DEBUG;
        
        // Create L2WasmHook immediately so it's available for callback setting
        std::cout << "[ExchangeSimulation] DEBUG: Creating L2WasmHook in constructor..." << std::endl;
        l2_hook_ = std::make_unique<L2WasmHook>();
        
        // Set default spread profiles
        spread_profiles_ = {
            {1, 5, 0.7, 3.0, 1.0, 6.0, 0.5},    // low & narrow
            {50, 100, 3.0, 0.7, 6.0, 1.0, 0.5}  // high & wide
        };
    }

    ~ExchangeSimulation() {
        stop();
    }

    // Configuration methods - Basic parameters
    void set_agents(int agents) { params_.agents = agents; }
    void set_symbol(const std::string& symbol) { params_.symbol = symbol; }
    void set_seed(int seed) { params_.seed = seed; }
    void set_speed_factor(double speed_factor) { params_.speed_factor = speed_factor; }
    
    // Order timeout parameters  
    void set_timeout_distribution(const std::string& dist) { params_.timeout_dist = dist; }
    void set_median_timeout_seconds(double seconds) { params_.median_timeout_seconds = seconds; }
    void set_sigma_timeout(double sigma) { params_.sigma_timeout = sigma; }
    void set_pareto_alpha(double alpha) { params_.pareto_alpha = alpha; }
    void set_pareto_scale(double scale) { params_.pareto_scale = scale; }
    void set_tail_mix(double mix) { params_.tail_mix = mix; }
    void set_min_timeout_s(double min_s) { params_.min_timeout_s = min_s; }
    void set_max_timeout_s(double max_s) { params_.max_timeout_s = max_s; }
    
    // Warmup and seeding parameters
    void set_warmup_range_ms(int min_ms, int max_ms) { 
        params_.warmup_per_agent_ms_min = min_ms; 
        params_.warmup_per_agent_ms_max = max_ms; 
    }
    void set_order_book_seed_levels(int levels) { params_.order_book_seed_levels = levels; }
    
    // Order size parameters
    void set_order_size_ranges(double min_min, double min_max, double max_min, double max_max) {
        params_.min_order_size_min = min_min;
        params_.min_order_size_max = min_max;
        params_.max_order_size_min = max_min;
        params_.max_order_size_max = max_max;
    }
    
    // Imbalance parameters
    void set_imbalance_params(int levels_min, int levels_max, int adj_min, int adj_max) {
        params_.imbalance_levels_min = levels_min;
        params_.imbalance_levels_max = levels_max;
        params_.max_imbalance_adj_bps_min = adj_min;
        params_.max_imbalance_adj_bps_max = adj_max;
    }

    // Spread profile management
    void clear_spread_profiles() { spread_profiles_.clear(); }
    void add_spread_profile(int global_low, int global_high, 
                           double alpha_loc, double beta_loc,
                           double alpha_wid, double beta_wid, double weight) {
        spread_profiles_.push_back({global_low, global_high, alpha_loc, beta_loc, 
                                   alpha_wid, beta_wid, weight});
    }

#ifdef __EMSCRIPTEN__
    // Set the JavaScript callback for L2 events
    void set_l2_callback(emscripten::val callback) {
        std::cout << "[ExchangeSimulation] DEBUG: set_l2_callback called" << std::endl;
        if (l2_hook_) {
            std::cout << "[ExchangeSimulation] DEBUG: Setting callback on L2WasmHook (hook exists)..." << std::endl;
            l2_hook_->set_callback(callback);
            std::cout << "[ExchangeSimulation] DEBUG: L2 callback set successfully" << std::endl;
        } else {
            std::cout << "[ExchangeSimulation] ERROR: L2WasmHook is null!" << std::endl;
        }
    }
#endif

    // Initialize the simulation
    bool initialize() {
        std::cout << "[ExchangeSimulation] DEBUG: Initialize called, is_initialized_=" << is_initialized_ << std::endl;
        
        if (is_initialized_) return false;

        try {
            // Create simulation components
            std::cout << "[ExchangeSimulation] DEBUG: Creating TradingSimulation..." << std::endl;
            sim_ = std::make_unique<TradingSimulation>(params_.symbol, params_.seed);
            
            // Register the L2 hook
            std::cout << "[ExchangeSimulation] DEBUG: Registering existing L2WasmHook with event bus..." << std::endl;
            sim_->get_event_bus().register_pre_publish_hook(l2_hook_.get());
            
            rtb_ = std::make_unique<RealTimeBusWasm>(sim_->get_event_bus());

            // Create agents
            std::cout << "[ExchangeSimulation] DEBUG: Creating " << params_.agents << " agents..." << std::endl;
            std::default_random_engine main_rng(params_.seed);
            trader_pool_.clear();

            for (int i = 0; i < params_.agents; ++i) {
                ZIMMParams agent_params = sample_agent_params_cpp(
                    main_rng, spread_profiles_,
                    {params_.min_order_size_min, params_.min_order_size_max},
                    {params_.max_order_size_min, params_.max_order_size_max},
                    {params_.imbalance_levels_min, params_.imbalance_levels_max},
                    {params_.max_imbalance_adj_bps_min, params_.max_imbalance_adj_bps_max});

                trader_pool_.push_back(std::make_shared<trading::algo::ZeroIntelligenceMarketMaker>(
                    params_.symbol,
                    agent_params.min_spread_bps, agent_params.max_spread_bps,
                    agent_params.min_order_size_float, agent_params.max_order_size_float,
                    agent_params.imbalance_levels, agent_params.max_imbalance_adj_bps,
                    params_.timeout_dist, params_.median_timeout_seconds, params_.sigma_timeout,
                    params_.pareto_alpha, params_.pareto_scale, params_.tail_mix,
                    params_.min_timeout_s, params_.max_timeout_s,
                    static_cast<unsigned int>(params_.seed + 1000 + i)));
            }

            std::shuffle(trader_pool_.begin(), trader_pool_.end(), main_rng);

            // Add agents to simulation
            std::cout << "[ExchangeSimulation] DEBUG: Adding agents to simulation..." << std::endl;
            std::pair<int,int> warmup_range = {params_.warmup_per_agent_ms_min, params_.warmup_per_agent_ms_max};
            for (auto &t : trader_pool_) {
                sim_->add_trader(t);
                warm_up_agent_cpp(*sim_, warmup_range, main_rng);
            }

            // Seed initial order book
            std::cout << "[ExchangeSimulation] DEBUG: Seeding initial order book with " << params_.order_book_seed_levels << " levels..." << std::endl;
            seed_order_book_cpp(*sim_, params_.order_book_seed_levels);

            // Let agents react
            std::cout << "[ExchangeSimulation] DEBUG: Letting agents react to initial book..." << std::endl;
            int steps = 0;
            while (sim_->get_event_bus().get_event_queue_size() > 0 && steps < params_.agents * 20) {
                sim_->get_event_bus().step();
                ++steps;
            }
            std::cout << "[ExchangeSimulation] DEBUG: Processed " << steps << " initial events, queue size: " << sim_->get_event_bus().get_event_queue_size() << std::endl;

            // Small wiggle
            if (sim_->get_event_bus().get_event_queue_size() == 0 || steps > 0) {
                std::cout << "[ExchangeSimulation] DEBUG: Broadcasting small wiggle..." << std::endl;
                broadcast_small_wiggle_cpp(*sim_, main_rng);
                int wiggle = 0;
                while (sim_->get_event_bus().get_event_queue_size() > 0 && wiggle < params_.agents * 10) {
                    sim_->get_event_bus().step();
                    ++wiggle;
                }
                std::cout << "[ExchangeSimulation] DEBUG: Processed " << wiggle << " wiggle events, final queue size: " << sim_->get_event_bus().get_event_queue_size() << std::endl;
            }

            is_initialized_ = true;
            std::cout << "[ExchangeSimulation] DEBUG: Initialization complete!" << std::endl;
            return true;
        } catch (...) {
            std::cout << "[ExchangeSimulation] ERROR: Exception during initialization!" << std::endl;
            return false;
        }
    }

    // Start the simulation
    bool start() {
        std::cout << "[ExchangeSimulation] DEBUG: Start called, is_initialized_=" << is_initialized_ << ", is_running_=" << is_running_ << std::endl;
        
        if (!is_initialized_ || is_running_) return false;
        
        try {
            is_running_ = true;
            std::cout << "[ExchangeSimulation] DEBUG: Starting RealTimeBusWasm with speed factor " << params_.speed_factor << std::endl;
            std::cout << "[ExchangeSimulation] DEBUG: Initial queue size: " << sim_->get_event_bus().get_event_queue_size() << std::endl;
            rtb_->run(params_.speed_factor);
            is_running_ = false;
            std::cout << "[ExchangeSimulation] DEBUG: RealTimeBusWasm finished" << std::endl;
            return true;
        } catch (...) {
            std::cout << "[ExchangeSimulation] ERROR: Exception during start!" << std::endl;
            is_running_ = false;
            return false;
        }
    }

    // Stop the simulation
    void stop() {
        if (rtb_) {
            rtb_->stop();
        }
        is_running_ = false;
    }

    // Cleanup
    void cleanup() {
        stop();
        if (sim_ && l2_hook_) {
            sim_->get_event_bus().deregister_pre_publish_hook(l2_hook_.get());
        }
        trader_pool_.clear();
        rtb_.reset();
        sim_.reset();
        l2_hook_.reset();
        is_initialized_ = false;
    }

    // Status getters
    bool is_running() const { return is_running_; }
    bool is_initialized() const { return is_initialized_; }
    int get_queue_size() const { 
        return sim_ ? static_cast<int>(sim_->get_event_bus().get_event_queue_size()) : 0; 
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 3. Emscripten bindings
// -----------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(exchange_simulation) {
    emscripten::class_<ExchangeSimulation>("ExchangeSimulation")
        .constructor<>()
        // Basic parameters
        .function("setAgents", &ExchangeSimulation::set_agents)
        .function("setSymbol", &ExchangeSimulation::set_symbol)
        .function("setSeed", &ExchangeSimulation::set_seed)
        .function("setSpeedFactor", &ExchangeSimulation::set_speed_factor)
        // Order timeout parameters
        .function("setTimeoutDistribution", &ExchangeSimulation::set_timeout_distribution)
        .function("setMedianTimeoutSeconds", &ExchangeSimulation::set_median_timeout_seconds)
        .function("setSigmaTimeout", &ExchangeSimulation::set_sigma_timeout)
        .function("setParetoAlpha", &ExchangeSimulation::set_pareto_alpha)
        .function("setParetoScale", &ExchangeSimulation::set_pareto_scale)
        .function("setTailMix", &ExchangeSimulation::set_tail_mix)
        .function("setMinTimeoutS", &ExchangeSimulation::set_min_timeout_s)
        .function("setMaxTimeoutS", &ExchangeSimulation::set_max_timeout_s)
        // Warmup and seeding parameters
        .function("setWarmupRangeMs", &ExchangeSimulation::set_warmup_range_ms)
        .function("setOrderBookSeedLevels", &ExchangeSimulation::set_order_book_seed_levels)
        // Order size parameters
        .function("setOrderSizeRanges", &ExchangeSimulation::set_order_size_ranges)
        // Imbalance parameters
        .function("setImbalanceParams", &ExchangeSimulation::set_imbalance_params)
        // Spread profile management
        .function("clearSpreadProfiles", &ExchangeSimulation::clear_spread_profiles)
        .function("addSpreadProfile", &ExchangeSimulation::add_spread_profile)
        // L2 callback and control
        .function("setL2Callback", &ExchangeSimulation::set_l2_callback)
        .function("initialize", &ExchangeSimulation::initialize)
        .function("start", &ExchangeSimulation::start)
        .function("stop", &ExchangeSimulation::stop)
        .function("cleanup", &ExchangeSimulation::cleanup)
        // Status getters
        .function("isRunning", &ExchangeSimulation::is_running)
        .function("isInitialized", &ExchangeSimulation::is_initialized)
        .function("getQueueSize", &ExchangeSimulation::get_queue_size);
}
#endif 