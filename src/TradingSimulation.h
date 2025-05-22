// file: src/TradingSimulation.h
#pragma once

#include "Model.h"
#include "EventBus.h" // EventBusSystem::LatencyParameters will be available via this
#include "ExchangeAdapter.h"
#include "CancelFairy.h"
#include "AlgoBase.h"
#include "EnvironmentProcessor.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <type_traits>
#include <functional>
#include <chrono>
#include <utility>
#include <random> // Added for std::default_random_engine, std::uniform_int_distribution
#include "Logging.h" // For EventBusSystem::LogMessage

// Define the EventPrinterHook
// This could be in its own file, but for simplicity, placing it here.
// Ensure ModelEventBus is defined or aliased correctly before this.
using SimulationEventBus = ModelEventBus<>; // Ensure this alias is available

class EventPrinterHook : public EventBusSystem::IPrePublishHook<
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
> {
private:
    std::function<void(const ModelEvents::LTwoOrderBookEvent&)> on_l2_event_cb_;

public:
    // <<<< MODIFIED CONSTRUCTOR >>>>
    explicit EventPrinterHook(std::function<void(const ModelEvents::LTwoOrderBookEvent&)> on_l2_cb = nullptr)
            : on_l2_event_cb_(std::move(on_l2_cb)) {}

    std::string get_hook_name() const override {
        return "EventPrinterHook";
    }

    void on_pre_publish(
            EventBusSystem::AgentId publisher_id,
            EventBusSystem::TopicId published_topic_id,
            const EventVariant& event_variant,
            EventBusSystem::Timestamp publish_time,
            const SimulationEventBus* bus // Use the concrete bus type for resolving
    ) override {
        std::string topic_str = bus->get_topic_string(published_topic_id);
        std::string event_type_name_str;

        std::visit([&](const auto& ev_ptr){ // Captures this, event_type_name_str, event_variant by reference
            if (ev_ptr) {
                event_type_name_str = typeid(*ev_ptr).name();

                // <<<< NEW LOGIC for LTwoOrderBookEvent callback >>>>
                if (on_l2_event_cb_) {
                    if (auto l2_event_sptr_ptr = std::get_if<std::shared_ptr<const ModelEvents::LTwoOrderBookEvent>>(&event_variant)) {
                        if (const auto& l2_event_sptr = *l2_event_sptr_ptr) { // Check if the shared_ptr itself is not null
                            try {
                                on_l2_event_cb_(*l2_event_sptr);
                            } catch (const std::exception& e) {
                                LogMessage(LogLevel::ERROR, get_hook_name(), "Exception in L2 Event callback: " + std::string(e.what()));
                            } catch (...) {
                                LogMessage(LogLevel::ERROR, get_hook_name(), "Unknown exception in L2 Event callback.");
                            }
                        }
                    }
                }
                // <<<< END NEW LOGIC >>>>

            } else {
                event_type_name_str = "[NullEventPtrInVariant]";
            }
        }, event_variant);

        std::ostringstream oss;
        oss << "PRE-PUBLISH: PubID=" << publisher_id
            << ", Topic='" << topic_str << "' (ID=" << published_topic_id << ")"
            << ", EventType=" << event_type_name_str
            << ", BusTime=" << bus->format_timestamp(publish_time);

        LogMessage(LogLevel::DEBUG, get_logger_source(), oss.str()); // Changed to DEBUG for less noise if L2 print is primary
    }

    std::string get_logger_source(){
        return "EventPrinterHook";
    }
};

// Define Latency Profiles based on the provided table
struct LatencyProfile {
    std::string name;
    double median_us;
    double sigma;
    double cap_us;

    EventBusSystem::LatencyParameters to_latency_parameters() const {
        return EventBusSystem::LatencyParameters::Lognormal(median_us, sigma, cap_us);
    }
};

// This could be a static member of TradingSimulation or in a namespace
// For simplicity, a global const in this header (guarded by #pragma once)
const std::vector<LatencyProfile> TRADER_LATENCY_PROFILES = {
        {"Co-located HFT",        50.0,    0.42, 200.0},
        {"Metro cross-connect",   300.0,   0.66, 2000.0},
        {"Same-city VPS",         1000.0,  0.67, 5000.0},
        {"Domestic retail ISP",   12000.0, 0.54, 60000.0},
        {"Inter-continental retail", 60000.0, 0.42, 150000.0}
};


class TradingSimulation {
public:
    using AgentId = EventBusSystem::AgentId;
    using SymbolType = ModelEvents::SymbolType;
    using PriceType = ModelEvents::PriceType;
    using QuantityType = ModelEvents::QuantityType;
    using Duration = ModelEvents::Duration;
    using Side = ModelEvents::Side;
    using ClientOrderIdType = ModelEvents::ClientOrderIdType;
    using Timestamp = EventBusSystem::Timestamp;
    using FloatPriceQuantityPair = std::pair<double, double>;
    using FloatOrderBookLevel = std::vector<FloatPriceQuantityPair>;
    using LatencyParameters = EventBusSystem::LatencyParameters;


    template<typename... Args>
    using GenericEventProcessorInterface = EventBusSystem::IEventProcessor<Args...>;

    using TraderInterfacePtr = std::shared_ptr<
            GenericEventProcessorInterface<
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
            >
    >;


    explicit TradingSimulation(
            const SymbolType& symbol,
            unsigned int bus_seed = 0,
            std::unique_ptr<EventPrinterHook> printer_hook = nullptr // <<<< MODIFIED >>>>
    )
            : event_bus_(Timestamp{}, bus_seed),
              symbol_(symbol),
              latency_rng_(bus_seed + 1) {

        // <<<< MODIFIED >>>>
        if (printer_hook) {
            event_printer_hook_ = std::move(printer_hook);
        } else {
            // Create a default hook if none is provided (e.g., one that doesn't print L2)
            event_printer_hook_ = std::make_unique<EventPrinterHook>();
        }
        event_bus_.register_pre_publish_hook(event_printer_hook_.get());
        // <<<< END MODIFIED >>>>

        // 1. Create and register EnvironmentProcessor
        environment_processor_ = std::make_shared<EnvironmentProcessor>();
        environment_processor_id_ = event_bus_.register_entity(environment_processor_.get());

        // 2. Create and register CancelFairy
        cancel_fairy_ = std::make_shared<CancelFairyApp>();
        cancel_fairy_id_ = event_bus_.register_entity(cancel_fairy_.get());

        // 3. Create and register ExchangeAdapter
        exchange_adapter_ = std::make_shared<EventModelExchangeAdapter>(symbol_);
        exchange_adapter_id_ = event_bus_.register_entity(exchange_adapter_.get());

        // 4. L2SnapshotCollector REMOVED

        // 5. Setup subscriptions for all main components
        environment_processor_->setup_subscriptions();
        cancel_fairy_->setup_subscriptions();
        exchange_adapter_->setup_subscriptions();

        LogMessage(LogLevel::INFO, get_logger_source(), "TradingSimulation initialized for symbol: " + symbol_);
        LogMessage(LogLevel::INFO, get_logger_source(), "Assigned IDs: Environment=" + std::to_string(environment_processor_id_) + ", CancelFairy=" + std::to_string(cancel_fairy_id_) + ", ExchangeAdapter=" + std::to_string(exchange_adapter_id_));

        // 6. Configure inter-agent latencies for core components
        configure_core_component_latencies();
    }

    ~TradingSimulation() {
        LogMessage(LogLevel::INFO, get_logger_source(), "TradingSimulation shutting down.");
        std::vector<AgentId> trader_ids_to_remove;
        for(const auto& pair : traders_) {
            trader_ids_to_remove.push_back(pair.first);
        }
        for(AgentId id : trader_ids_to_remove) {
            event_bus_.deregister_entity(id);
        }
        traders_.clear();

        if (exchange_adapter_) event_bus_.deregister_entity(exchange_adapter_id_);
        if (cancel_fairy_) event_bus_.deregister_entity(cancel_fairy_id_);
        if (environment_processor_) event_bus_.deregister_entity(environment_processor_id_);

        if (event_printer_hook_) {
            event_bus_.deregister_pre_publish_hook(event_printer_hook_.get());
        }
    }

    TradingSimulation(const TradingSimulation&) = delete;
    TradingSimulation& operator=(const TradingSimulation&) = delete;
    TradingSimulation(TradingSimulation&&) = default;
    TradingSimulation& operator=(TradingSimulation&&) = default;

    template<typename DerivedAlgo, typename = std::enable_if_t<std::is_base_of_v<trading::algo::AlgoBase<DerivedAlgo>, DerivedAlgo>>>
    AgentId add_trader(std::shared_ptr<DerivedAlgo> trader) {
        if (!trader) {
            LogMessage(LogLevel::INFO, get_logger_source(), "Attempted to add a null trader pointer.");
            return EventBusSystem::INVALID_AGENT_ID;
        }
        AgentId trader_id = event_bus_.register_entity(trader.get());
        if (trader_id == EventBusSystem::INVALID_AGENT_ID) {
            LogMessage(LogLevel::INFO, get_logger_source(), "Failed to register trader (type: " + std::string(typeid(DerivedAlgo).name()) + ")");
            return EventBusSystem::INVALID_AGENT_ID;
        }
        trader->setup_subscriptions();
        traders_[trader_id] = trader;
        LogMessage(LogLevel::INFO, get_logger_source(), "Added trader with ID: " + std::to_string(trader_id));

        configure_trader_latencies(trader_id); // This will now use random profiles
        return trader_id;
    }

    std::optional<TraderInterfacePtr> get_trader(AgentId trader_id) const {
        auto it = traders_.find(trader_id);
        if (it != traders_.end()) {
            return it->second;
        }
        LogMessage(LogLevel::WARNING, get_logger_source(), "Trader with ID " + std::to_string(trader_id) + " not found.");
        return std::nullopt;
    }

    std::shared_ptr<const ModelEvents::LTwoOrderBookEvent> create_order_book_snapshot(
            FloatOrderBookLevel bids_float,
            FloatOrderBookLevel asks_float
    ) {
        ModelEvents::OrderBookLevel bids_int;
        bids_int.reserve(bids_float.size());
        for (const auto& p_q_float : bids_float) {
            bids_int.emplace_back(
                    ModelEvents::float_to_price(p_q_float.first),
                    ModelEvents::float_to_quantity(p_q_float.second)
            );
        }

        ModelEvents::OrderBookLevel asks_int;
        asks_int.reserve(asks_float.size());
        for (const auto& p_q_float : asks_float) {
            asks_int.emplace_back(
                    ModelEvents::float_to_price(p_q_float.first),
                    ModelEvents::float_to_quantity(p_q_float.second)
            );
        }

        Timestamp current_time = event_bus_.get_current_time();
        auto order_book_event_ptr = std::make_shared<const ModelEvents::LTwoOrderBookEvent>(
                current_time,
                symbol_,
                current_time,
                current_time,
                std::move(bids_int),
                std::move(asks_int)
        );

        std::string stream_id = "orderbook_snapshot_" + symbol_;
        std::string topic = "LTwoOrderBookEvent." + symbol_;

        event_bus_.publish(environment_processor_id_, topic, order_book_event_ptr, stream_id);

        LogMessage(LogLevel::DEBUG, get_logger_source(), "Published LTwoOrderBookEvent (Publisher ID: " + std::to_string(environment_processor_id_) + ") for symbol " + symbol_);
        return order_book_event_ptr;
    }


    void step(bool debug = false) {
        if (debug) {
            LogMessage(LogLevel::DEBUG, get_logger_source(), "Event queue size before step: " + std::to_string(event_bus_.get_event_queue_size()));
        }
        // auto processed_event_opt = // Commented out as unused
        event_bus_.step();
        if (debug) {
            LogMessage(LogLevel::DEBUG, get_logger_source(), "Event queue size after step: " + std::to_string(event_bus_.get_event_queue_size()));
        }
    }


    void run(int steps = 100) {
        int steps_run = 0;
        for (int i = 0; i < steps; ++i) {
            LogMessage(LogLevel::DEBUG, get_logger_source(), "Event queue before step " + std::to_string(i+1) + ": " + std::to_string(event_bus_.get_event_queue_size()) + " events");
            if (event_bus_.get_event_queue_size() == 0) {
                LogMessage(LogLevel::INFO, get_logger_source(), "Event queue empty. Stopping run early after " + std::to_string(i) + " steps.");
                break;
            }
            // auto processed_event_opt = // Commented out as unused
            event_bus_.step();
            steps_run = i + 1;
            LogMessage(LogLevel::DEBUG, get_logger_source(), "Event queue after step " + std::to_string(i+1) + ": " + std::to_string(event_bus_.get_event_queue_size()) + " events");
        }
        LogMessage(LogLevel::INFO, get_logger_source(), "Simulation ran for " + std::to_string(steps_run) + " steps, ended at time: " + ModelEvents::format_timestamp(event_bus_.get_current_time()) + ". Final queue size: " + std::to_string(event_bus_.get_event_queue_size()));
    }

    SimulationEventBus& get_event_bus() { return event_bus_; }
    const SimulationEventBus& get_event_bus() const { return event_bus_; }

private:
    std::string get_logger_source() const { return "TradingSimulation"; }

    void configure_core_component_latencies() {
        LogMessage(LogLevel::INFO, get_logger_source(), "Configuring core component latencies...");
        LatencyParameters min_fixed_latency = LatencyParameters::Fixed(1.0, 1.0); // 1 microsecond fixed

        // Exchange <-> CancelFairy (very fast, internal system communication)
        event_bus_.set_inter_agent_latency(exchange_adapter_id_, cancel_fairy_id_, min_fixed_latency);
        event_bus_.set_inter_agent_latency(cancel_fairy_id_, exchange_adapter_id_, min_fixed_latency);

        // Exchange <-> Environment (market data dissemination part, can be fast if internal)
        event_bus_.set_inter_agent_latency(exchange_adapter_id_, environment_processor_id_, min_fixed_latency);
        event_bus_.set_inter_agent_latency(environment_processor_id_, exchange_adapter_id_, min_fixed_latency);
    }

    void configure_trader_latencies(AgentId trader_id) {
        if (TRADER_LATENCY_PROFILES.empty()) {
            LogMessage(LogLevel::WARNING, get_logger_source(),
                       "No trader latency profiles defined for trader ID: " + std::to_string(trader_id) +
                       ". Using a default Lognormal(1000, 0.67, 5000).");
            // Fallback to a default (e.g., the previous hardcoded one "Same-city VPS")
            LatencyParameters trader_latency = LatencyParameters::Lognormal(1000.0, 0.67, 5000.0);
            event_bus_.set_inter_agent_latency(trader_id, exchange_adapter_id_, trader_latency);
            event_bus_.set_inter_agent_latency(exchange_adapter_id_, trader_id, trader_latency);
            event_bus_.set_inter_agent_latency(environment_processor_id_, trader_id, trader_latency);
            return;
        }

        std::uniform_int_distribution<size_t> dist(0, TRADER_LATENCY_PROFILES.size() - 1);
        size_t profile_index = dist(latency_rng_);
        const auto& selected_profile = TRADER_LATENCY_PROFILES[profile_index];

        LogMessage(LogLevel::INFO, get_logger_source(),
                   "Configuring latencies for trader ID: " + std::to_string(trader_id) +
                   " with profile: '" + selected_profile.name +
                   "' (Median: " + std::to_string(selected_profile.median_us) + "µs, " +
                   "Sigma: " + std::to_string(selected_profile.sigma) + ", " +
                   "Cap: " + std::to_string(selected_profile.cap_us) + "µs)");

        LatencyParameters trader_latency = selected_profile.to_latency_parameters();

        // Trader <-> ExchangeAdapter (orders, acks, fills)
        event_bus_.set_inter_agent_latency(trader_id, exchange_adapter_id_, trader_latency);
        event_bus_.set_inter_agent_latency(exchange_adapter_id_, trader_id, trader_latency);

        // EnvironmentProcessor -> Trader (market data updates)
        event_bus_.set_inter_agent_latency(environment_processor_id_, trader_id, trader_latency);

        // Note: Trader -> EnvironmentProcessor latency is not typically set unless traders directly message the environment.
        // If needed, it could be added:
        // event_bus_.set_inter_agent_latency(trader_id, environment_processor_id_, trader_latency);
    }

    SimulationEventBus event_bus_;
    SymbolType symbol_;
    std::unique_ptr<EventPrinterHook> event_printer_hook_;
    std::default_random_engine latency_rng_; // RNG for latency profile selection

    AgentId environment_processor_id_;
    AgentId exchange_adapter_id_;
    AgentId cancel_fairy_id_;

    std::shared_ptr<EnvironmentProcessor> environment_processor_;
    std::shared_ptr<EventModelExchangeAdapter> exchange_adapter_;
    std::shared_ptr<CancelFairyApp> cancel_fairy_;

    std::unordered_map<AgentId, TraderInterfacePtr> traders_;
};