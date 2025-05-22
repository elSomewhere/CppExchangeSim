// file: src/L2SnapshotCollector.h
// file: src/L2SnapshotCollector.h
#pragma once

#include "Model.h" // For LTwoOrderBookEvent, ModelEventProcessor, SymbolType, logging macros
#include <string>
#include <functional> // For std::function
#include "Logging.h"

class L2SnapshotCollector : public ModelEventProcessor<L2SnapshotCollector> {
public:
    using Base = ModelEventProcessor<L2SnapshotCollector>;
    using SymbolType = ModelEvents::SymbolType;
    using LTwoOrderBookEvent = ModelEvents::LTwoOrderBookEvent;
    using Timestamp = EventBusSystem::Timestamp;
    using AgentId = EventBusSystem::AgentId;
    using TopicId = EventBusSystem::TopicId;
    using StreamId = EventBusSystem::StreamId;
    using SequenceNumber = EventBusSystem::SequenceNumber;

    // <<<< MODIFIED: Removed agent_id parameter >>>>
    L2SnapshotCollector(const SymbolType& symbol_to_watch,
                        std::function<void(const LTwoOrderBookEvent&)> on_snapshot_cb)
        : Base(), // <<<< MODIFIED: Call base default constructor >>>>
          symbol_to_watch_(symbol_to_watch),
          on_snapshot_cb_(std::move(on_snapshot_cb)) {
        LogMessage(LogLevel::INFO, this->get_logger_source(), "L2SnapshotCollector constructed for symbol: " + symbol_to_watch_ + ". Agent ID will be set upon registration.");
    }

    void setup_subscriptions() {
        if (!this->bus_) {
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "L2SnapshotCollector cannot setup subscriptions: EventBus not set for agent " + std::to_string(this->get_id()));
            return;
        }
        LogMessage(LogLevel::INFO, this->get_logger_source(), "L2SnapshotCollector agent " + std::to_string(this->get_id()) + " setting up subscriptions for symbol: " + symbol_to_watch_);
        this->subscribe("LTwoOrderBookEvent." + symbol_to_watch_);
    }

    virtual ~L2SnapshotCollector() override = default;

    void handle_event(const LTwoOrderBookEvent& event, TopicId, AgentId, Timestamp ts, StreamId, SequenceNumber) {
        if (event.symbol == symbol_to_watch_) {
            if (on_snapshot_cb_) {
                try {
                    on_snapshot_cb_(event);
                } catch (const std::exception& e) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Exception in L2SnapshotCollector callback: " + std::string(e.what()));
                } catch (...) {
                    LogMessage(LogLevel::ERROR, this->get_logger_source(), "Unknown exception in L2SnapshotCollector callback.");
                }
            }
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Processed L2 book for " + event.symbol + " at " + ModelEvents::format_timestamp(ts));
        } else {
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Ignored L2 book for " + event.symbol + " (watching " + symbol_to_watch_ + ")");
        }
    }

    void handle_event(const ModelEvents::CheckLimitOrderExpirationEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::Bang&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialCancelMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::LimitOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::MarketOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullCancelLimitOrderAckEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
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
    void handle_event(const ModelEvents::LimitOrderExpiredEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialFillLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::PartialFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullFillLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::FullFillMarketOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::TradeEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::TriggerExpiredLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::RejectTriggerExpiredLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}
    void handle_event(const ModelEvents::AckTriggerExpiredLimitOrderEvent&, TopicId, AgentId, Timestamp, StreamId, SequenceNumber) {}

private:
    SymbolType symbol_to_watch_;
    std::function<void(const LTwoOrderBookEvent&)> on_snapshot_cb_;
};