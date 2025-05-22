// file: src/CancelFairyApp.h
#pragma once

#include "Model.h"      // For event types, ModelEventProcessor, ModelEventBus
#include "EventBus.h"   // For AgentId, Timestamp, etc.

#include <string>
#include <vector>
#include <unordered_map>
#include <utility> // For std::pair
#include <optional>

// Use the logging macros (ensure they are defined, e.g., via EventBus.h or Model.h)
#ifndef LOG_DEBUG
#define LOG_DEBUG(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::DEBUG, (logger_source), (message))
#define LOG_INFO(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::INFO, (logger_source), (message))
#define LOG_WARNING(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::WARNING, (logger_source), (message))
#define LOG_ERROR(logger_source, message) EventBusSystem::LogMessage(EventBusSystem::LogLevel::ERROR, (logger_source), (message))
#endif

class CancelFairyApp : public ModelEventProcessor<CancelFairyApp> {
public:
    // Type aliases from base and ModelEvents
    using Base = ModelEventProcessor<CancelFairyApp>;
    using AgentId = EventBusSystem::AgentId;
    using Timestamp = EventBusSystem::Timestamp;
    using Duration = EventBusSystem::Duration;
    using TopicId = EventBusSystem::TopicId;
    using StreamId = EventBusSystem::StreamId;
    using SequenceNumber = EventBusSystem::SequenceNumber;

    using SymbolType = ModelEvents::SymbolType;
    using ExchangeOrderIdType = ModelEvents::ExchangeOrderIdType;

    // Structure to hold metadata for orders being tracked for expiration
    struct OrderMetadata {
        SymbolType symbol;
        AgentId original_trader_id; // <<<< NEWLY ADDED >>>>
        Duration original_timeout;
    };

private:
    // Stores {exchange_order_id: OrderMetadata} for orders that might need expiring
    std::unordered_map<ExchangeOrderIdType, OrderMetadata> current_order_metadata_;

public:
    CancelFairyApp(AgentId agent_id)
            : Base(agent_id) {
        LOG_INFO(this->get_logger_source(), "CancelFairyApp constructed for Agent ID: " + std::to_string(agent_id));
    }

    void setup_subscriptions() {
        if (!this->bus_) {
            LOG_ERROR(this->get_logger_source(), "CancelFairyApp cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
            return;
        }
        LOG_INFO(this->get_logger_source(), "CancelFairyApp agent " + std::to_string(this->get_id()) + " setting up subscriptions.");
        this->subscribe("LimitOrderAckEvent");
        this->subscribe("FullFillLimitOrderEvent");
        this->subscribe("FullCancelLimitOrderAckEvent");
        this->subscribe("CheckLimitOrderExpirationEvent." + std::to_string(this->get_id()));
        this->subscribe("RejectTriggerExpiredLimitOrderEvent." + std::to_string(this->get_id()));
        this->subscribe("Bang");
        this->subscribe("AckTriggerExpiredLimitOrderEvent." + std::to_string(this->get_id()));
    }

    virtual ~CancelFairyApp() override = default;

    CancelFairyApp(const CancelFairyApp&) = delete;
    CancelFairyApp& operator=(const CancelFairyApp&) = delete;
    CancelFairyApp(CancelFairyApp&&) = delete;
    CancelFairyApp& operator=(CancelFairyApp&&) = delete;

    void handle_event(const ModelEvents::LimitOrderAckEvent& event, TopicId, AgentId sender_id_of_ack, Timestamp, StreamId, SequenceNumber) {
        // `sender_id_of_ack` is the ID of the Exchange Adapter.
        // `event.original_trader_id` is the ID of the agent who placed the limit order. <<<< NOW AVAILABLE >>>>
        LOG_DEBUG(this->get_logger_source(), "Processing LimitOrderAckEvent from sender " + std::to_string(sender_id_of_ack) + ": " + event.to_string());

        if (event.order_id == ModelEvents::ExchangeOrderIdType{0}) {
            LOG_WARNING(this->get_logger_source(), "Received LimitOrderAckEvent with invalid/default order_id: " + std::to_string(event.order_id));
            return;
        }
        if (!this->bus_) {
            LOG_ERROR(this->get_logger_source(), "EventBus not available, cannot process LimitOrderAckEvent.");
            return;
        }

        // <<<< STORE ORIGINAL TRADER ID >>>>
        current_order_metadata_[event.order_id] = {event.symbol, event.original_trader_id, event.timeout};

        Timestamp current_sim_time = this->bus_->get_current_time();
        Timestamp expiration_timestamp = current_sim_time + event.timeout;

        auto check_event_ptr = std::make_shared<const ModelEvents::CheckLimitOrderExpirationEvent>(
                current_sim_time,
                event.order_id,
                event.timeout
        );

        std::string check_topic = "CheckLimitOrderExpirationEvent." + std::to_string(this->get_id());
        std::string check_stream_id = "expire_check_" + std::to_string(event.order_id);

        this->schedule_for_self_at(expiration_timestamp, check_event_ptr, check_topic, check_stream_id);

        LOG_DEBUG(this->get_logger_source(), "Scheduled expiration check for XID " + std::to_string(event.order_id) +
                                             " (Original Trader: " + std::to_string(event.original_trader_id) + ")" + // Log added trader ID
                                             " at " + ModelEvents::format_timestamp(expiration_timestamp) + " (Original Timeout: " + ModelEvents::format_duration(event.timeout) + ")");
    }

    void handle_event(const ModelEvents::FullFillLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LOG_DEBUG(this->get_logger_source(), "Processing FullFillLimitOrderEvent for XID: " + std::to_string(event.order_id));
        process_terminal_event(event.order_id);
    }

    void handle_event(const ModelEvents::FullCancelLimitOrderAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LOG_DEBUG(this->get_logger_source(), "Processing FullCancelLimitOrderAckEvent for XID: " + std::to_string(event.order_id));
        process_terminal_event(event.order_id);
    }

    void handle_event(const ModelEvents::CheckLimitOrderExpirationEvent& event, TopicId, AgentId, Timestamp current_sim_time, StreamId, SequenceNumber) {
        LOG_DEBUG(this->get_logger_source(), "Processing CheckLimitOrderExpirationEvent for XID: " + std::to_string(event.target_exchange_order_id) +
                                             " at time " + ModelEvents::format_timestamp(current_sim_time));

        if (!this->bus_) {
            LOG_ERROR(this->get_logger_source(), "EventBus not available, cannot process CheckLimitOrderExpirationEvent.");
            return;
        }

        auto it = current_order_metadata_.find(event.target_exchange_order_id);
        if (it != current_order_metadata_.end()) {
            const OrderMetadata& metadata = it->second;
            LOG_INFO(this->get_logger_source(), "Order XID " + std::to_string(event.target_exchange_order_id) +
                                                " is active, triggering expiration. Symbol: " + metadata.symbol +
                                                ", Original Trader: " + std::to_string(metadata.original_trader_id)); // Log added trader ID

            // <<<< PASS ORIGINAL TRADER ID TO TRIGGER EVENT >>>>
            auto trigger_event_ptr = std::make_shared<const ModelEvents::TriggerExpiredLimitOrderEvent>(
                    current_sim_time,
                    metadata.symbol,
                    event.target_exchange_order_id,
                    metadata.original_timeout, // This is event.original_timeout from CheckEvent
                    metadata.original_trader_id // Pass the stored original trader ID
            );

            std::string trigger_topic = "TriggerExpiredLimitOrderEvent." + metadata.symbol;
            std::string trigger_stream_id = "expire_trigger_" + std::to_string(event.target_exchange_order_id);

            this->publish(trigger_topic, trigger_event_ptr, trigger_stream_id);
            LOG_DEBUG(this->get_logger_source(), "Published TriggerExpiredLimitOrderEvent to " + trigger_topic);

            current_order_metadata_.erase(it);
            LOG_DEBUG(this->get_logger_source(), "Removed tracking for triggered order XID " + std::to_string(event.target_exchange_order_id));
        } else {
            LOG_DEBUG(this->get_logger_source(), "Order XID " + std::to_string(event.target_exchange_order_id) +
                                                 " already terminated or not tracked. Ignoring expiration check.");
        }
    }

    void handle_event(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp current_sim_time, StreamId, SequenceNumber) {
        LOG_WARNING(this->get_logger_source(), "Received rejection of an expiry trigger for order XID " +
                                               std::to_string(event.target_exchange_order_id) + " at time " + ModelEvents::format_timestamp(current_sim_time) +
                                               ". Original timeout was: " + ModelEvents::format_duration(event.timeout_value));
    }

    void handle_event(const ModelEvents::Bang& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LOG_INFO(this->get_logger_source(), "Processing Bang event. Clearing all tracked orders.");
        current_order_metadata_.clear();
    }

    // --- Handlers for other events in ModelEventProcessor variant to satisfy CRTP ---
    void handle_event(const ModelEvents::LTwoOrderBookEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelMarketOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelLimitAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelMarketAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelLimitOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelLimitOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelMarketOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelMarketOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderRejectEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderExpiredEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderExpiredEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LOG_DEBUG(this->get_logger_source(), "Processing direct LimitOrderExpiredEvent for XID: " + std::to_string(event.order_id));
        process_terminal_event(event.order_id);
    }
    void handle_event(const ModelEvents::PartialFillLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::TradeEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LOG_DEBUG(this->get_logger_source(), "Received AckTriggerExpiredLimitOrderEvent for XID: " + std::to_string(event.target_exchange_order_id) + ". No state change, already untracked.");
    }

private:
    void process_terminal_event(ExchangeOrderIdType order_id) {
        auto it = current_order_metadata_.find(order_id);
        if (it != current_order_metadata_.end()) {
            const OrderMetadata& metadata = it->second;
            LOG_DEBUG(this->get_logger_source(), "Order XID " + std::to_string(order_id) +
                                                 " (Symbol: " + metadata.symbol + ", Original Trader: " + std::to_string(metadata.original_trader_id) + // Log added trader ID
                                                 ") is now terminal. Removing tracking.");
            current_order_metadata_.erase(it);
        } else {
            LOG_DEBUG(this->get_logger_source(), "Received terminal event for XID " + std::to_string(order_id) +
                                                 ", but it was not actively tracked (or already removed).");
        }
    }
};