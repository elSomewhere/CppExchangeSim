// file: src/TradingSimulation.h
#pragma once

#include "Model.h"
#include "EventBus.h"
#include "ExchangeAdapter.h" // Still needed for the exchange_adapter_ member
#include "CancelFairy.h"
#include "AlgoBase.h"
#include "L2SnapshotCollector.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <type_traits>
#include <functional>
#include <chrono>
#include <utility>

// Logging macros
#ifndef LOG_DEBUG
#define LOG_DEBUG(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::DEBUG, (logger_source), (message))
#define LOG_INFO(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::INFO, (logger_source), (message))
#define LOG_WARNING(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::WARNING, (logger_source), (message))
#define LOG_ERROR(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::ERROR, (logger_source), (message))
#endif

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
    using L2CallbackType = std::function<void(const ModelEvents::LTwoOrderBookEvent&)>;
    using FloatPriceQuantityPair = std::pair<double, double>;
    using FloatOrderBookLevel = std::vector<FloatPriceQuantityPair>;
    using SimulationEventBus = ModelEventBus<>;

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
            L2CallbackType l2_snapshot_callback,
            unsigned int bus_seed = 0
    )
            : event_bus_(Timestamp{}, bus_seed),
              symbol_(symbol) {

        exchange_adapter_ = std::make_shared<EventModelExchangeAdapter>(
                symbol_,
                EXCHANGE_ADAPTER_ID
        );
        event_bus_.register_entity(EXCHANGE_ADAPTER_ID, exchange_adapter_.get());
        exchange_adapter_->setup_subscriptions();

        l2_collector_ = std::make_shared<L2SnapshotCollector>(
                L2_COLLECTOR_ID,
                symbol_,
                std::move(l2_snapshot_callback)
        );
        event_bus_.register_entity(L2_COLLECTOR_ID, l2_collector_.get());
        l2_collector_->setup_subscriptions();

        cancel_fairy_ = std::make_shared<CancelFairyApp>(
                CANCEL_FAIRY_ID
        );
        event_bus_.register_entity(CANCEL_FAIRY_ID, cancel_fairy_.get());
        cancel_fairy_->setup_subscriptions();

        LOG_INFO(get_logger_source(), "TradingSimulation initialized for symbol: " + symbol_);
    }

    ~TradingSimulation() {
        LOG_INFO(get_logger_source(), "TradingSimulation shutting down.");
        if (cancel_fairy_) event_bus_.deregister_entity(CANCEL_FAIRY_ID);
        if (l2_collector_) event_bus_.deregister_entity(L2_COLLECTOR_ID);
        if (exchange_adapter_) event_bus_.deregister_entity(EXCHANGE_ADAPTER_ID);

        std::vector<AgentId> trader_ids_to_remove;
        for(const auto& pair : traders_) {
            trader_ids_to_remove.push_back(pair.first);
        }
        for(AgentId id : trader_ids_to_remove) {
            event_bus_.deregister_entity(id);
        }
        traders_.clear();
    }

    TradingSimulation(const TradingSimulation&) = delete;
    TradingSimulation& operator=(const TradingSimulation&) = delete;
    TradingSimulation(TradingSimulation&&) = default;
    TradingSimulation& operator=(TradingSimulation&&) = default;

    template<typename DerivedAlgo,
            typename = std::enable_if_t<std::is_base_of_v<trading::algo::AlgoBase<DerivedAlgo>, DerivedAlgo>>>
    AgentId add_trader(std::shared_ptr<DerivedAlgo> trader) {
        if (!trader) {
            LOG_ERROR(get_logger_source(), "Attempted to add a null trader pointer.");
            return static_cast<AgentId>(-1);
        }
        AgentId trader_id = trader->get_id();
        event_bus_.register_entity(trader_id, trader.get());
        trader->setup_subscriptions();
        traders_[trader_id] = trader;
        LOG_INFO(get_logger_source(), "Added trader with ID: " + std::to_string(trader_id));
        return trader_id;
    }

    std::optional<TraderInterfacePtr> get_trader(AgentId trader_id) const {
        auto it = traders_.find(trader_id);
        if (it != traders_.end()) {
            return it->second;
        }
        LOG_WARNING(get_logger_source(), "Trader with ID " + std::to_string(trader_id) + " not found.");
        return std::nullopt;
    }

    std::shared_ptr<const ModelEvents::LTwoOrderBookEvent> create_order_book_snapshot(
            FloatOrderBookLevel bids_float,
            FloatOrderBookLevel asks_float
    ) {
        // **MODIFICATION**: Publish directly via event_bus_

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

        std::string stream_id = "orderbook_snapshot";
        std::string topic = "LTwoOrderBookEvent." + symbol_;

        // Use a system/environment ID for the publisher.
        // EXCHANGE_ADAPTER_ID (0) can be used if we consider this as originating from "the exchange"
        // Or define a new SIMULATION_CONTROLLER_ID if preferred.
        AgentId publisher_id_for_snapshot = ENVIRONMENT_PUBLISHER_ID; // Or EXCHANGE_ADAPTER_ID
        event_bus_.publish(publisher_id_for_snapshot, topic, order_book_event_ptr, stream_id);

        LOG_DEBUG(get_logger_source(), "Published LTwoOrderBookEvent directly via EventBus for symbol " + symbol_);
        return order_book_event_ptr;
    }


    void step(bool debug = false) { // Content unchanged from previous, just for context
        if (debug) {
            LOG_DEBUG(get_logger_source(), "Event queue size before step: " + std::to_string(event_bus_.get_event_queue_size()));
        }
        auto processed_event_opt = event_bus_.step();
        if (debug) {
            LOG_DEBUG(get_logger_source(), "Event queue size after step: " + std::to_string(event_bus_.get_event_queue_size()));
        }
    }


    void run(int steps = 100) { // Content unchanged from previous, just for context
        int steps_run = 0;
        for (int i = 0; i < steps; ++i) {
            if (EventBusSystem::LoggerConfig::G_CURRENT_LOG_LEVEL <= EventBusSystem::LogLevel::DEBUG) {
                std::string q_status_before = "Event queue before step " + std::to_string(i+1) + ": " + std::to_string(event_bus_.get_event_queue_size()) + " events";
                LOG_DEBUG(get_logger_source(), "\n--- " + q_status_before + " ---");
            }
            if (event_bus_.get_event_queue_size() == 0) {
                LOG_INFO(get_logger_source(), "Event queue empty. Stopping run early after " + std::to_string(i) + " steps.");
                break;
            }
            auto processed_event_opt = event_bus_.step();
            steps_run = i + 1;
            if (EventBusSystem::LoggerConfig::G_CURRENT_LOG_LEVEL <= EventBusSystem::LogLevel::DEBUG) {
                std::string q_status_after = "Event queue after step " + std::to_string(i+1) + ": " + std::to_string(event_bus_.get_event_queue_size()) + " events";
                LOG_DEBUG(get_logger_source(), "--- " + q_status_after + " ---");
            }
        }
        LOG_INFO(get_logger_source(), "\nSimulation ran for " + std::to_string(steps_run) + " steps, ended at time: " +
                                      ModelEvents::format_timestamp(event_bus_.get_current_time()) +
                                      ". Final queue size: " + std::to_string(event_bus_.get_event_queue_size()));
    }

    SimulationEventBus& get_event_bus() { return event_bus_; }
    const SimulationEventBus& get_event_bus() const { return event_bus_; }

private:
    std::string get_logger_source() const { return "TradingSimulation"; }

    SimulationEventBus event_bus_;
    SymbolType symbol_;

    std::shared_ptr<EventModelExchangeAdapter> exchange_adapter_;
    std::shared_ptr<L2SnapshotCollector> l2_collector_;
    std::shared_ptr<CancelFairyApp> cancel_fairy_;

    std::unordered_map<AgentId, TraderInterfacePtr> traders_;

    static constexpr AgentId EXCHANGE_ADAPTER_ID = 0;
    static constexpr AgentId L2_COLLECTOR_ID = 998;
    static constexpr AgentId CANCEL_FAIRY_ID = 999;
    static constexpr AgentId ENVIRONMENT_PUBLISHER_ID = 0; // Using ID 0 for environment/system published events
};