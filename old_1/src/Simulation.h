// file: src/TradingSimulation.h
#pragma once

#include "Model.h"
#include "EventBus.h"
#include "ExchangeAdapter.h"
#include "CancelFairy.h"
#include "AlgoBase.h"
#include "L2SnapshotCollector.h"
#include "ZeroIntelligenceMarketMaker.h" // For example trader type

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>    // For std::shared_ptr
#include <optional>  // For std::optional
#include <type_traits> // For std::enable_if_t, std::is_base_of_v
#include <functional> // For std::function
#include <chrono>    // For std::chrono::milliseconds

class TradingSimulation {
public:
    // Type aliases for clarity
    using AgentId = EventBusSystem::AgentId;
    using SymbolType = ModelEvents::SymbolType;
    using PriceType = ModelEvents::PriceType;
    using QuantityType = ModelEvents::QuantityType;
    using Duration = ModelEvents::Duration;
    using Side = ModelEvents::Side;
    using ClientOrderIdType = ModelEvents::ClientOrderIdType;
    using Timestamp = EventBusSystem::Timestamp;
    using L2CallbackType = std::function<void(const ModelEvents::LTwoOrderBookEvent&)>;

    // Event Bus type using the alias from Model.h
    using SimulationEventBus = ModelEventBus<>;

    // Define IEventProcessor interface type pointer for storing traders
    // This uses the full list of events from Model.h's ModelEventProcessor alias
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

    explicit TradingSimulation(const SymbolType& symbol, L2CallbackType l2_snapshot_callback)
        : event_bus_(Timestamp{}),
          symbol_(symbol) {

        // 1. Create Exchange Adapter
        exchange_adapter_ = std::make_shared<EventModelExchangeAdapter>(
            symbol_,
            EXCHANGE_ADAPTER_ID
            // No bus pointer passed here, register_entity will set it.
        );
        // 2. Register Exchange Adapter (this calls exchange_adapter_->set_event_bus(&event_bus_))
        event_bus_.register_entity(EXCHANGE_ADAPTER_ID, exchange_adapter_.get());
        // 3. Setup subscriptions for Exchange Adapter
        exchange_adapter_->setup_subscriptions();

        // 1. Create L2 Snapshot Collector
        l2_collector_ = std::make_shared<L2SnapshotCollector>(
            L2_COLLECTOR_ID,
            symbol_,
            std::move(l2_snapshot_callback)
        );
        // 2. Register L2 Collector
        event_bus_.register_entity(L2_COLLECTOR_ID, l2_collector_.get());
        // 3. Setup subscriptions for L2 Collector
        l2_collector_->setup_subscriptions();

        // 1. Create CancelFairy
        cancel_fairy_ = std::make_shared<CancelFairyApp>(
            CANCEL_FAIRY_ID
        );
        // 2. Register CancelFairy
        event_bus_.register_entity(CANCEL_FAIRY_ID, cancel_fairy_.get());
        // 3. Setup subscriptions for CancelFairy
        cancel_fairy_->setup_subscriptions();

        LOG_INFO(get_logger_source(), "TradingSimulation initialized for symbol: " + symbol_);
    }

    ~TradingSimulation() {
        LOG_INFO(get_logger_source(), "TradingSimulation shutting down.");
        // Explicitly deregister entities before bus is destroyed
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

    // Prevent copying
    TradingSimulation(const TradingSimulation&) = delete;
    TradingSimulation& operator=(const TradingSimulation&) = delete;
    // Allow moving
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
        // 1. Trader is already created (passed as argument)
        // 2. Register Trader
        event_bus_.register_entity(trader_id, trader.get());
        // 3. Setup subscriptions for Trader
        trader->setup_subscriptions(); // Call setup_subscriptions on AlgoBase instance

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

    void step(bool debug = false) {
        if (debug) {
            LOG_DEBUG(get_logger_source(), "Event queue size before step: " + std::to_string(event_bus_.get_event_queue_size()));
        }
        LOG_DEBUG(get_logger_source(), "***************************************");
        LOG_DEBUG(get_logger_source(), "* Before step");
        LOG_DEBUG(get_logger_source(), "***************************************");
        event_bus_.step();
        LOG_DEBUG(get_logger_source(), "=======================================");
        if (debug) {
             LOG_DEBUG(get_logger_source(), "Event queue size after step: " + std::to_string(event_bus_.get_event_queue_size()));
        }
        LOG_DEBUG(get_logger_source(), "***************************************");
        LOG_DEBUG(get_logger_source(), "* After step");
        LOG_DEBUG(get_logger_source(), "***************************************");
    }

    void run(int steps = 100) {
        int steps_run = 0;
        for (int i = 0; i < steps; ++i) {
            std::string q_status_before = "Event queue before step " + std::to_string(i+1) + ": " + std::to_string(event_bus_.get_event_queue_size()) + " events";
            LOG_DEBUG(get_logger_source(), "\n--- " + q_status_before + " ---");


            if (event_bus_.get_event_queue_size() == 0) {
                LOG_INFO(get_logger_source(), "Event queue empty. Stopping run early after " + std::to_string(i) + " steps.");
                break;
            }
            event_bus_.step();
            steps_run = i + 1;

            std::string q_status_after = "Event queue after step " + std::to_string(i+1) + ": " + std::to_string(event_bus_.get_event_queue_size()) + " events";
            LOG_DEBUG(get_logger_source(), "--- " + q_status_after + " ---");

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

    // Agent IDs
    static constexpr AgentId EXCHANGE_ADAPTER_ID = 0;
    static constexpr AgentId L2_COLLECTOR_ID = 998;
    static constexpr AgentId CANCEL_FAIRY_ID = 999;
};