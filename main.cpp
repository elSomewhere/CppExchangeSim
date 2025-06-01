// main.cpp – beta‑skewed spread sampling version
// -----------------------------------------------------------------------------
// Build notes
//   * No other source files changed – only this unit introduces the new helper
//     functions + profile type and wires them into sample_agent_params_cpp().
//   * If you split helpers into their own header later, remove them from here
//     and just include that header instead.
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

// ─── Project headers ─────────────────────────────────────────────────────────
#include "src/Model.h"
#include "src/EventBus.h"
#include "src/TradingSimulation.h"
#include "ZeroIntelligenceMarketMaker.h"
#include "src/RealTimeBus.h"
#include "L2PrinterHook.h"

// ─────────────────────────────────────────────────────────────────────────────
// 1.  Beta‑distribution helpers (self‑contained here for simplicity)
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

    // 1. centre in [0,1]
    double c = sampleBeta(rng, alpha_loc, beta_loc);

    // 2. relative half‑width in [0,1]
    double w = std::max(sampleBeta(rng, alpha_wid, beta_wid), w_min_rel);

    // 3. shrink so interval stays inside [0,1]
    double w_max = std::min(c, 1.0 - c);
    double half = w * w_max;

    double lo_rel = c - half;
    double hi_rel = c + half;

    // 4. back to integer bps
    int lo = global_low + static_cast<int>(std::round(lo_rel * span));
    int hi = global_low + static_cast<int>(std::round(hi_rel * span));
    if (lo == hi) hi = std::min(hi + 1, global_high);
    return {lo, hi};
}

// ─── Profile describing the shapes for Beta‑based sampling ───────────────────
struct ZIMMBetaSpreadProfile {
    int global_low;     // absolute min possible spread (bps)
    int global_high;    // absolute max possible spread (bps)
    double alpha_loc;
    double beta_loc;
    double alpha_wid;
    double beta_wid;
    double weight;      // probability weight when selecting profile
};

// ─────────────────────────────────────────────────────────────────────────────
// 2.  Sampling of full ZIMM parameter set
// -----------------------------------------------------------------------------
struct ZIMMParams {
    int    min_spread_bps;
    int    max_spread_bps;
    double min_order_size_float;
    double max_order_size_float;
    size_t imbalance_levels;
    int    max_imbalance_adj_bps;
};

ZIMMParams sample_agent_params_cpp(
        std::default_random_engine                                &rng,
        const std::vector<ZIMMBetaSpreadProfile>                  &beta_spread_profiles,
        const std::pair<double,double> &min_order_size_range,
        const std::pair<double,double> &max_order_size_range,
        const std::pair<int,int>    &imbalance_levels_range,
        const std::pair<int,int>    &max_imbalance_adj_bps_range)
{
    // ── choose a profile ────────────────────────────────────────────────
    const ZIMMBetaSpreadProfile *sel = nullptr;
    if (beta_spread_profiles.empty()) {
        static const ZIMMBetaSpreadProfile fallback = {1, 10,
                                                      1.0, 1.0,
                                                      1.0, 1.0,
                                                      1.0};
        sel = &fallback;
    } else {
        std::vector<double> w; w.reserve(beta_spread_profiles.size());
        for (auto &p : beta_spread_profiles) w.push_back(p.weight);
        std::discrete_distribution<int> pick(w.begin(), w.end());
        sel = &beta_spread_profiles[pick(rng)];
    }

    // ── draw (min,max) spread via Beta shapes ───────────────────────────
    auto [min_spread, max_spread] = draw_spread_range_beta(
            rng,
            sel->global_low, sel->global_high,
            sel->alpha_loc, sel->beta_loc,
            sel->alpha_wid, sel->beta_wid);

    // ── remaining params identical to previous code ─────────────────────
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

    return {min_spread, max_spread,
            min_size,   max_size,
            imbalance_levels,
            max_imbalance_adj_bps};
}

// ─────────────────────────────────────────────────────────────────────────────
// 3.  Utility fns unchanged (seed_order_book_cpp, broadcast_small_wiggle_cpp …)
// -----------------------------------------------------------------------------
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
// 4.  main()
// -----------------------------------------------------------------------------
int main()
{
    LoggerConfig::G_CURRENT_LOG_LEVEL = LogLevel::DEBUG;

    int agents = 100;
    ModelEvents::SymbolType symbol = "BTC/USD";
    int seed = 47;
    double speed_factor = 100.0;

    // Order lifetime parameters (unchanged)
    std::string timeout_dist = "lognormal";
    double median_timeout_seconds = 5.0;
    double sigma_timeout = 0.8;
    double pareto_alpha = 1.5;
    double pareto_scale = 5.0;
    double tail_mix = 0.1;
    double min_timeout_s = 1.0;
    double max_timeout_s = 60.0;

    std::pair<int,int> warmup_per_agent_ms = {0,0};
    int order_book_seed_levels = 5;

    // ── NEW: two Beta‑based spread profiles ---------------------------------
    std::vector<ZIMMBetaSpreadProfile> spread_profiles = {
        // low & narrow  (1–5 bps)
        { 1, 5,
          0.7, 3.0,     // centre skew → left
          1.0, 6.0,     // width  skew → narrow
          0.5 },        // 50 % weight
        // high & wide   (50–100 bps)
        { 50, 100,
          3.0, 0.7,     // centre skew → right
          6.0, 1.0,     // width  skew → wide
          0.5 }
    };

    // Other parameter ranges --------------------------------------------------
    std::pair<double,double> min_order_size_range   = {0.01, 0.1};
    std::pair<double,double> max_order_size_range   = {0.1,  0.5};
    std::pair<int,int>       imbalance_levels_range = {1, 3};
    std::pair<int,int>       max_imbalance_adj_bps_range = {2, 10};

    std::default_random_engine main_rng(seed);

    // --- MODIFIED: Create and use L2PrinterHook ---
    // The existing EventPrinterHook in TradingSimulation.h might still be useful
    // for generic event logging. If L2PrinterHook is *the only* pre-publish hook desired
    // for L2 printing, we can pass nullptr to TradingSimulation for its internal hook.
    // For this example, let's assume TradingSimulation's default hook creation is fine,
    // and we add L2PrinterHook *in addition*.
    // If you want L2PrinterHook to *replace* the one in TradingSimulation that handles L2,
    // you'd pass a non-L2-printing hook (or nullptr if TradingSimulation handles it)
    // to TradingSimulation's constructor, then register L2PrinterHook separately.

    // Let's instantiate our L2PrinterHook
    auto l2_printer_hook = std::make_unique<L2PrinterHook>();

    // TradingSimulation will still create its own EventPrinterHook.
    // We can then register our L2PrinterHook in addition if we want both.
    // Or, if TradingSimulation's printer_hook parameter is intended to be the *sole*
    // pre-publish hook, we would pass this one in.
    // The current TradingSimulation takes a `unique_ptr<EventPrinterHook>`, which is a specific type.
    // To use L2PrinterHook, we would need to:
    // 1. Modify TradingSimulation to accept IPrePublishHook
    // OR
    // 2. Register L2PrinterHook directly with the bus after TradingSimulation is created.

    // Option 2: Register L2PrinterHook directly with the bus.
    // This assumes TradingSimulation is constructed with its default EventPrinterHook or a custom one.
    TradingSimulation sim(symbol, seed); // Uses its default internal EventPrinterHook
    sim.get_event_bus().register_pre_publish_hook(l2_printer_hook.get()); // Register our L2 hook
    // --- END MODIFIED ---


    RealTimeBus rtb(sim.get_event_bus());

    std::vector<std::shared_ptr<trading::algo::ZeroIntelligenceMarketMaker>> trader_pool;

    std::cout << "Creating " << agents << " ZIMM agents with beta‑skewed spreads…" << std::endl;
    for (int i = 0; i < agents; ++i) {
        ZIMMParams params = sample_agent_params_cpp(
                main_rng, spread_profiles,
                min_order_size_range, max_order_size_range,
                imbalance_levels_range, max_imbalance_adj_bps_range);

        trader_pool.push_back(std::make_shared<trading::algo::ZeroIntelligenceMarketMaker>(
                symbol,
                params.min_spread_bps, params.max_spread_bps,
                params.min_order_size_float, params.max_order_size_float,
                params.imbalance_levels, params.max_imbalance_adj_bps,
                timeout_dist, median_timeout_seconds, sigma_timeout,
                pareto_alpha, pareto_scale, tail_mix,
                min_timeout_s, max_timeout_s,
                static_cast<unsigned int>(seed + 1000 + i)));
    }
    std::shuffle(trader_pool.begin(), trader_pool.end(), main_rng);

    std::cout << "Adding agents BEFORE initial book seed…" << std::endl;
    for (auto &t : trader_pool) {
        sim.add_trader(t);
        warm_up_agent_cpp(sim, warmup_per_agent_ms, main_rng);
    }

    std::cout << "Seeding initial order book AFTER agents are added…" << std::endl;
    seed_order_book_cpp(sim, order_book_seed_levels);

    // let agents react ---------------------------------------------------------
    int steps = 0;
    while (sim.get_event_bus().get_event_queue_size() > 0 && steps < agents*20) {
        sim.get_event_bus().step();
        ++steps;
    }

    // small wiggle -------------------------------------------------------------
    if (sim.get_event_bus().get_event_queue_size() == 0 || steps > 0) {
        broadcast_small_wiggle_cpp(sim, main_rng);
        int wiggle = 0;
        while (sim.get_event_bus().get_event_queue_size() > 0 && wiggle < agents*10) {
            sim.get_event_bus().step();
            ++wiggle;
        }
    }

    std::cout << "Starting RealTimeBus processing…" << std::endl;
    auto wall_start = std::chrono::steady_clock::now();
    EventBusSystem::Timestamp sim_start_ts = sim.get_event_bus().get_current_time();

    rtb.run(speed_factor);

    auto wall_end = std::chrono::steady_clock::now();
    EventBusSystem::Duration sim_elapsed = sim.get_event_bus().get_current_time() - sim_start_ts;
    std::chrono::duration<double> wall_elapsed = wall_end - wall_start;

    std::cout << "\n--- RealTimeBus finished ---" << std::endl;
    std::cout << "Wall‑clock elapsed: " << std::fixed << std::setprecision(3)
              << wall_elapsed.count() << " s" << std::endl;
    std::cout << "Simulated time elapsed: " << ModelEvents::duration_to_float_seconds(sim_elapsed)
              << " s" << std::endl;
    std::cout << "Final queue size: " << sim.get_event_bus().get_event_queue_size() << std::endl;

    // Deregister the hook (good practice, though unique_ptr would manage lifetime if owned by sim)
    sim.get_event_bus().deregister_pre_publish_hook(l2_printer_hook.get());

    return 0;
}