// file: src/EnvironmentProcessor.h
#pragma once

#include "Model.h" // For ModelEventProcessor, logging macros, and event types

// Forward declare if needed, or ensure Model.h includes what's necessary
// (Model.h already includes EventBus.h which defines Timestamp, AgentId, etc.)

class EnvironmentProcessor : public ModelEventProcessor<EnvironmentProcessor> {
public:
    using Base = ModelEventProcessor<EnvironmentProcessor>;
    // Aliases for EventBusSystem types if used directly
    using AgentId = EventBusSystem::AgentId;
    using Timestamp = EventBusSystem::Timestamp;
    using TopicId = EventBusSystem::TopicId;
    using StreamId = EventBusSystem::StreamId;
    using SequenceNumber = EventBusSystem::SequenceNumber;


    EnvironmentProcessor() : Base() {
        // Logging here will use ID before it's set by the bus.
        LogMessage(LogLevel::INFO, this->get_logger_source(), "EnvironmentProcessor constructed. Agent ID will be set upon registration.");
    }

    void setup_subscriptions() {
        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "EnvironmentProcessor cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
            return;
        }
        LogMessage(LogLevel::INFO, this->get_logger_source(), "EnvironmentProcessor agent " + std::to_string(this->get_id()) + " setting up subscriptions (currently none).");
        // This processor typically originates events, might not need to subscribe to much.
    }

    virtual ~EnvironmentProcessor() override = default;

    // --- Implement all virtual handle_event methods from ModelEventProcessor ---
    // Most will be empty as this processor primarily originates events or reacts minimally.

    void handle_event(const ModelEvents::CheckLimitOrderExpirationEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring CheckLimitOrderExpirationEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::Bang& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_INFO(this->get_logger_source(), "EnvironmentProcessor received Bang event: " + event.to_string());
        // Could trigger its own logic if needed
    }
    void handle_event(const ModelEvents::LTwoOrderBookEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring LTwoOrderBookEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::LimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring LimitOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::MarketOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring MarketOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialCancelLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialCancelLimitOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialCancelMarketOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialCancelMarketOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullCancelLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullCancelLimitOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullCancelMarketOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullCancelMarketOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::LimitOrderAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring LimitOrderAckEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::MarketOrderAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring MarketOrderAckEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullCancelLimitOrderAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullCancelLimitOrderAckEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullCancelMarketOrderAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullCancelMarketOrderAckEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialCancelLimitAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialCancelLimitAckEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialCancelMarketAckEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialCancelMarketAckEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialCancelLimitOrderRejectEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialCancelLimitOrderRejectEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullCancelLimitOrderRejectEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullCancelLimitOrderRejectEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialCancelMarketOrderRejectEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialCancelMarketOrderRejectEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullCancelMarketOrderRejectEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullCancelMarketOrderRejectEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::LimitOrderRejectEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring LimitOrderRejectEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::MarketOrderRejectEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring MarketOrderRejectEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::MarketOrderExpiredEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring MarketOrderExpiredEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::LimitOrderExpiredEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring LimitOrderExpiredEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialFillLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialFillLimitOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::PartialFillMarketOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring PartialFillMarketOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullFillLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullFillLimitOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::FullFillMarketOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring FullFillMarketOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::TradeEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring TradeEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring TriggerExpiredLimitOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring RejectTriggerExpiredLimitOrderEvent: " + event.to_string());
    }
    void handle_event(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {
        // LOG_DEBUG(this->get_logger_source(), "Ignoring AckTriggerExpiredLimitOrderEvent: " + event.to_string());
    }
};