// file: src/CancelFairy.h
// file: src/CancelFairy.h
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
        AgentId original_trader_id;
        Duration original_timeout;
    };

private:
    // Stores {exchange_order_id: OrderMetadata} for orders that might need expiring
    std::unordered_map<ExchangeOrderIdType, OrderMetadata> current_order_metadata_;

public:
    CancelFairyApp() // <<<< MODIFIED: Removed agent_id parameter >>>>
            : Base() { // <<<< MODIFIED: Call base default constructor >>>>
        // Logging here will use ID before it's set by the bus.
        LogMessage(LogLevel::INFO, this->get_logger_source(), "CancelFairyApp constructed. Agent ID will be set upon registration.");
    }

    void setup_subscriptions() {
        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "CancelFairyApp cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
            return;
        }
        LogMessage(LogLevel::INFO, this->get_logger_source(), "CancelFairyApp agent " + std::to_string(this->get_id()) + " setting up subscriptions.");
        this->subscribe("LimitOrderAckEvent"); // Subscribes to all LimitOrderAckEvents
        this->subscribe("FullFillLimitOrderEvent"); // Subscribes to all FullFillLimitOrderEvents
        this->subscribe("FullCancelLimitOrderAckEvent"); // Subscribes to all FullCancelLimitOrderAckEvents
        // Subscribes to events specifically targeted at this CancelFairy instance
        this->subscribe("CheckLimitOrderExpirationEvent." + std::to_string(this->get_id()));
        this->subscribe("RejectTriggerExpiredLimitOrderEvent." + std::to_string(this->get_id()));
        this->subscribe("Bang"); // General Bang
        this->subscribe("AckTriggerExpiredLimitOrderEvent." + std::to_string(this->get_id()));
        this->subscribe("LimitOrderExpiredEvent"); // To catch expirations from other sources if any, or direct exchange expirations
    }

    virtual ~CancelFairyApp() override = default;

    CancelFairyApp(const CancelFairyApp&) = delete;
    CancelFairyApp& operator=(const CancelFairyApp&) = delete;
    CancelFairyApp(CancelFairyApp&&) = delete;
    CancelFairyApp& operator=(CancelFairyApp&&) = delete;

    void handle_event(const ModelEvents::LimitOrderAckEvent& event, TopicId, AgentId sender_id_of_ack, Timestamp, StreamId, SequenceNumber) {
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Processing LimitOrderAckEvent from sender " + std::to_string(sender_id_of_ack) + ": " + event.to_string());

        if (event.order_id == ModelEvents::ExchangeOrderIdType{0}) {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "Received LimitOrderAckEvent with invalid/default order_id: " + std::to_string(event.order_id));
            return;
        }
        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "EventBus not available, cannot process LimitOrderAckEvent.");
            return;
        }

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

        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Scheduled expiration check for XID " + std::to_string(event.order_id) +
                                             " (Original Trader: " + std::to_string(event.original_trader_id) + ")" +
                                             " at " + ModelEvents::format_timestamp(expiration_timestamp) + " (Original Timeout: " + ModelEvents::format_duration(event.timeout) + ")");
    }

    void handle_event(const ModelEvents::FullFillLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Processing FullFillLimitOrderEvent for XID: " + std::to_string(event.order_id));
        process_terminal_event(event.order_id);
    }

    void handle_event(const ModelEvents::FullCancelLimitOrderAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Processing FullCancelLimitOrderAckEvent for XID: " + std::to_string(event.order_id));
        process_terminal_event(event.order_id);
    }

    void handle_event(const ModelEvents::CheckLimitOrderExpirationEvent& event, TopicId, AgentId, Timestamp current_sim_time, StreamId, SequenceNumber) {
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Processing CheckLimitOrderExpirationEvent for XID: " + std::to_string(event.target_exchange_order_id) +
                                             " at time " + ModelEvents::format_timestamp(current_sim_time));

        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "EventBus not available, cannot process CheckLimitOrderExpirationEvent.");
            return;
        }

        auto it = current_order_metadata_.find(event.target_exchange_order_id);
        if (it != current_order_metadata_.end()) {
            const OrderMetadata& metadata = it->second;
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Order XID " + std::to_string(event.target_exchange_order_id) +
                                                " is active, attempting to trigger expiration. Symbol: " + metadata.symbol +
                                                ", Original Trader: " + std::to_string(metadata.original_trader_id));

            auto trigger_event_ptr = std::make_shared<const ModelEvents::TriggerExpiredLimitOrderEvent>(
                    current_sim_time,
                    metadata.symbol,
                    event.target_exchange_order_id,
                    metadata.original_timeout,
                    metadata.original_trader_id
            );

            std::string trigger_topic = "TriggerExpiredLimitOrderEvent." + metadata.symbol;
            std::string trigger_stream_id = "expire_trigger_" + std::to_string(event.target_exchange_order_id);

            this->publish(trigger_topic, trigger_event_ptr, trigger_stream_id);
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Published TriggerExpiredLimitOrderEvent to " + trigger_topic);

            // MODIFICATION: Do NOT remove tracking here. Wait for AckTriggerExpiredLimitOrderEvent or RejectTriggerExpiredLimitOrderEvent.
            // current_order_metadata_.erase(it);
            // LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Removed tracking for triggered order XID " + std::to_string(event.target_exchange_order_id));
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Keeping tracking for order XID " + std::to_string(event.target_exchange_order_id) + " pending Ack/Reject of trigger.");

        } else {
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Order XID " + std::to_string(event.target_exchange_order_id) +
                                                 " already terminated or not tracked when CheckLimitOrderExpirationEvent received. Ignoring expiration check.");
        }
    }

    void handle_event(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp current_sim_time, StreamId, SequenceNumber) {
        LogMessage(LogLevel::WARNING, this->get_logger_source(), "Received rejection of an expiry trigger for order XID " +
                                               std::to_string(event.target_exchange_order_id) + " at time " + ModelEvents::format_timestamp(current_sim_time) +
                                               ". Original timeout was: " + ModelEvents::format_duration(event.timeout_value) +
                                               ". This typically means the order was not found on the exchange (e.g., already filled/cancelled). Untracking.");
        // MODIFICATION: Process as a terminal event for the tracked XID.
        process_terminal_event(event.target_exchange_order_id);
    }

    void handle_event(const ModelEvents::Bang& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LogMessage(LogLevel::INFO, this->get_logger_source(), "Processing Bang event. Clearing all tracked orders.");
        current_order_metadata_.clear();
    }

    void handle_event(const ModelEvents::LimitOrderExpiredEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // This event might come directly from an exchange or other source,
        // or it could be the result of our own TriggerExpiredLimitOrderEvent->AckTriggerExpiredLimitOrderEvent cycle
        // if the ExchangeAdapter also publishes a generic LimitOrderExpiredEvent upon successful expiration.
        // In any case, it's a terminal event for the order.
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Processing LimitOrderExpiredEvent for XID: " + std::to_string(event.order_id));
        process_terminal_event(event.order_id);
    }

    void handle_event(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Received AckTriggerExpiredLimitOrderEvent for XID: " + std::to_string(event.target_exchange_order_id) + ". Order successfully expired by trigger. Untracking.");
        // MODIFICATION: Process as a terminal event for the tracked XID.
        process_terminal_event(event.target_exchange_order_id);
    }

    // Empty handlers for other events (as before)
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
    void handle_event(const ModelEvents::PartialFillLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::TradeEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}


private:
    void process_terminal_event(ExchangeOrderIdType order_id) {
        auto it = current_order_metadata_.find(order_id);
        if (it != current_order_metadata_.end()) {
            const OrderMetadata& metadata = it->second;
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Order XID " + std::to_string(order_id) +
                                                 " (Symbol: " + metadata.symbol + ", Original Trader: " + std::to_string(metadata.original_trader_id) +
                                                 ") is now terminal. Removing tracking.");
            current_order_metadata_.erase(it);
        } else {
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Received terminal event for XID " + std::to_string(order_id) +
                                                 ", but it was not actively tracked (or already removed).");
        }
    }
};