// file: src/PrePublishHookBase.h
//
// Created by elanter on 5/22/25.
// Modified to include an abstraction layer for pre-publish hooks (no macros).
//

#ifndef PREPUBLISHHOOKBASE_H
#define PREPUBLISHHOOKBASE_H

#include <iostream>
#include <chrono>
#include <string>

// ─── Project headers ─────────────────────────────────────────────────────────
#include "src/Model.h"
#include "src/EventBus.h"



class TradingPrePublishHook : public EventBusSystem::PrePublishHook<TradingPrePublishHook,
            ModelEvents::CheckLimitOrderExpirationEvent, ModelEvents::Bang, ModelEvents::LTwoOrderBookEvent,
            ModelEvents::LimitOrderEvent, ModelEvents::MarketOrderEvent, ModelEvents::PartialCancelLimitOrderEvent,
            ModelEvents::PartialCancelMarketOrderEvent, ModelEvents::FullCancelLimitOrderEvent,
            ModelEvents::FullCancelMarketOrderEvent,
            ModelEvents::LimitOrderAckEvent, ModelEvents::MarketOrderAckEvent, ModelEvents::FullCancelLimitOrderAckEvent
            ,
            ModelEvents::FullCancelMarketOrderAckEvent, ModelEvents::PartialCancelLimitAckEvent,
            ModelEvents::PartialCancelMarketAckEvent,
            ModelEvents::PartialCancelLimitOrderRejectEvent, ModelEvents::FullCancelLimitOrderRejectEvent,
            ModelEvents::PartialCancelMarketOrderRejectEvent, ModelEvents::FullCancelMarketOrderRejectEvent,
            ModelEvents::LimitOrderRejectEvent, ModelEvents::MarketOrderRejectEvent,
            ModelEvents::MarketOrderExpiredEvent,
            ModelEvents::LimitOrderExpiredEvent, ModelEvents::PartialFillLimitOrderEvent,
            ModelEvents::PartialFillMarketOrderEvent,
            ModelEvents::FullFillLimitOrderEvent, ModelEvents::FullFillMarketOrderEvent, ModelEvents::TradeEvent,
            ModelEvents::TriggerExpiredLimitOrderEvent, ModelEvents::RejectTriggerExpiredLimitOrderEvent,
            ModelEvents::AckTriggerExpiredLimitOrderEvent> {
public:
    using Base = EventBusSystem::PrePublishHook<TradingPrePublishHook, ModelEvents::CheckLimitOrderExpirationEvent,
        ModelEvents::Bang, ModelEvents::LTwoOrderBookEvent,
        ModelEvents::LimitOrderEvent, ModelEvents::MarketOrderEvent, ModelEvents::PartialCancelLimitOrderEvent,
        ModelEvents::PartialCancelMarketOrderEvent, ModelEvents::FullCancelLimitOrderEvent,
        ModelEvents::FullCancelMarketOrderEvent,
        ModelEvents::LimitOrderAckEvent, ModelEvents::MarketOrderAckEvent, ModelEvents::FullCancelLimitOrderAckEvent,
        ModelEvents::FullCancelMarketOrderAckEvent, ModelEvents::PartialCancelLimitAckEvent,
        ModelEvents::PartialCancelMarketAckEvent,
        ModelEvents::PartialCancelLimitOrderRejectEvent, ModelEvents::FullCancelLimitOrderRejectEvent,
        ModelEvents::PartialCancelMarketOrderRejectEvent, ModelEvents::FullCancelMarketOrderRejectEvent,
        ModelEvents::LimitOrderRejectEvent, ModelEvents::MarketOrderRejectEvent, ModelEvents::MarketOrderExpiredEvent,
        ModelEvents::LimitOrderExpiredEvent, ModelEvents::PartialFillLimitOrderEvent,
        ModelEvents::PartialFillMarketOrderEvent,
        ModelEvents::FullFillLimitOrderEvent, ModelEvents::FullFillMarketOrderEvent, ModelEvents::TradeEvent,
        ModelEvents::TriggerExpiredLimitOrderEvent, ModelEvents::RejectTriggerExpiredLimitOrderEvent,
        ModelEvents::AckTriggerExpiredLimitOrderEvent>;
    using AgentId = EventBusSystem::AgentId;
    using TopicId = EventBusSystem::TopicId;
    using Timestamp = EventBusSystem::Timestamp;
    using BusT = typename Base::BusT;

    TradingPrePublishHook() = default;

    virtual ~TradingPrePublishHook() = default;

    virtual std::string hook_name() const = 0;

    // --- Specific handle_pre_publish overloads ---
    // Each calls a corresponding virtual on_pre_publish_SpecificEvent method.

    void handle_pre_publish(const ModelEvents::CheckLimitOrderExpirationEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_CheckLimitOrderExpirationEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::Bang &event, AgentId pid, TopicId tid, Timestamp ts, const BusT *bus) {
        this->on_pre_publish_Bang(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::LTwoOrderBookEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_LTwoOrderBookEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::LimitOrderEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_LimitOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::MarketOrderEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_MarketOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialCancelLimitOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialCancelLimitOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialCancelMarketOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialCancelMarketOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullCancelLimitOrderEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_FullCancelLimitOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullCancelMarketOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_FullCancelMarketOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::LimitOrderAckEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_LimitOrderAckEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::MarketOrderAckEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_MarketOrderAckEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullCancelLimitOrderAckEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_FullCancelLimitOrderAckEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullCancelMarketOrderAckEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_FullCancelMarketOrderAckEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialCancelLimitAckEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialCancelLimitAckEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialCancelMarketAckEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialCancelMarketAckEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialCancelLimitOrderRejectEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialCancelLimitOrderRejectEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullCancelLimitOrderRejectEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_FullCancelLimitOrderRejectEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialCancelMarketOrderRejectEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialCancelMarketOrderRejectEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullCancelMarketOrderRejectEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_FullCancelMarketOrderRejectEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::LimitOrderRejectEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_LimitOrderRejectEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::MarketOrderRejectEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_MarketOrderRejectEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::MarketOrderExpiredEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_MarketOrderExpiredEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::LimitOrderExpiredEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_LimitOrderExpiredEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialFillLimitOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialFillLimitOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::PartialFillMarketOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_PartialFillMarketOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullFillLimitOrderEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_FullFillLimitOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::FullFillMarketOrderEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_FullFillMarketOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::TradeEvent &event, AgentId pid, TopicId tid, Timestamp ts,
                            const BusT *bus) {
        this->on_pre_publish_TradeEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::TriggerExpiredLimitOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_TriggerExpiredLimitOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::RejectTriggerExpiredLimitOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_RejectTriggerExpiredLimitOrderEvent(event, pid, tid, ts, bus);
    }

    void handle_pre_publish(const ModelEvents::AckTriggerExpiredLimitOrderEvent &event, AgentId pid, TopicId tid,
                            Timestamp ts, const BusT *bus) {
        this->on_pre_publish_AckTriggerExpiredLimitOrderEvent(event, pid, tid, ts, bus);
    }

    // --- Virtual on_pre_publish_SpecificEvent methods for derived classes to override ---
    // Default implementations call on_pre_publish_event_default_dispatch.

    virtual void on_pre_publish_CheckLimitOrderExpirationEvent(const ModelEvents::CheckLimitOrderExpirationEvent &e,
                                                               AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void
    on_pre_publish_Bang(const ModelEvents::Bang &e, AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_LTwoOrderBookEvent(const ModelEvents::LTwoOrderBookEvent &e, AgentId pid, TopicId tid,
                                                   Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_LimitOrderEvent(const ModelEvents::LimitOrderEvent &e, AgentId pid, TopicId tid,
                                                Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_MarketOrderEvent(const ModelEvents::MarketOrderEvent &e, AgentId pid, TopicId tid,
                                                 Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialCancelLimitOrderEvent(const ModelEvents::PartialCancelLimitOrderEvent &e,
                                                             AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialCancelMarketOrderEvent(const ModelEvents::PartialCancelMarketOrderEvent &e,
                                                              AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_FullCancelLimitOrderEvent(const ModelEvents::FullCancelLimitOrderEvent &e, AgentId pid,
                                                          TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_FullCancelMarketOrderEvent(const ModelEvents::FullCancelMarketOrderEvent &e,
                                                           AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_LimitOrderAckEvent(const ModelEvents::LimitOrderAckEvent &e, AgentId pid, TopicId tid,
                                                   Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_MarketOrderAckEvent(const ModelEvents::MarketOrderAckEvent &e, AgentId pid, TopicId tid,
                                                    Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_FullCancelLimitOrderAckEvent(const ModelEvents::FullCancelLimitOrderAckEvent &e,
                                                             AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_FullCancelMarketOrderAckEvent(const ModelEvents::FullCancelMarketOrderAckEvent &e,
                                                              AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialCancelLimitAckEvent(const ModelEvents::PartialCancelLimitAckEvent &e,
                                                           AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialCancelMarketAckEvent(const ModelEvents::PartialCancelMarketAckEvent &e,
                                                            AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialCancelLimitOrderRejectEvent(
        const ModelEvents::PartialCancelLimitOrderRejectEvent &e, AgentId pid, TopicId tid, Timestamp ts,
        const BusT *b) { on_pre_publish_event_default_dispatch(e, pid, tid, ts, b); }

    virtual void on_pre_publish_FullCancelLimitOrderRejectEvent(const ModelEvents::FullCancelLimitOrderRejectEvent &e,
                                                                AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialCancelMarketOrderRejectEvent(
        const ModelEvents::PartialCancelMarketOrderRejectEvent &e, AgentId pid, TopicId tid, Timestamp ts,
        const BusT *b) { on_pre_publish_event_default_dispatch(e, pid, tid, ts, b); }

    virtual void on_pre_publish_FullCancelMarketOrderRejectEvent(const ModelEvents::FullCancelMarketOrderRejectEvent &e,
                                                                 AgentId pid, TopicId tid, Timestamp ts,
                                                                 const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_LimitOrderRejectEvent(const ModelEvents::LimitOrderRejectEvent &e, AgentId pid,
                                                      TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_MarketOrderRejectEvent(const ModelEvents::MarketOrderRejectEvent &e, AgentId pid,
                                                       TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_MarketOrderExpiredEvent(const ModelEvents::MarketOrderExpiredEvent &e, AgentId pid,
                                                        TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_LimitOrderExpiredEvent(const ModelEvents::LimitOrderExpiredEvent &e, AgentId pid,
                                                       TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialFillLimitOrderEvent(const ModelEvents::PartialFillLimitOrderEvent &e,
                                                           AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_PartialFillMarketOrderEvent(const ModelEvents::PartialFillMarketOrderEvent &e,
                                                            AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_FullFillLimitOrderEvent(const ModelEvents::FullFillLimitOrderEvent &e, AgentId pid,
                                                        TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_FullFillMarketOrderEvent(const ModelEvents::FullFillMarketOrderEvent &e, AgentId pid,
                                                         TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_TradeEvent(const ModelEvents::TradeEvent &e, AgentId pid, TopicId tid, Timestamp ts,
                                           const BusT *b) { on_pre_publish_event_default_dispatch(e, pid, tid, ts, b); }

    virtual void on_pre_publish_TriggerExpiredLimitOrderEvent(const ModelEvents::TriggerExpiredLimitOrderEvent &e,
                                                              AgentId pid, TopicId tid, Timestamp ts, const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }

    virtual void on_pre_publish_RejectTriggerExpiredLimitOrderEvent(
        const ModelEvents::RejectTriggerExpiredLimitOrderEvent &e, AgentId pid, TopicId tid, Timestamp ts,
        const BusT *b) { on_pre_publish_event_default_dispatch(e, pid, tid, ts, b); }

    virtual void on_pre_publish_AckTriggerExpiredLimitOrderEvent(const ModelEvents::AckTriggerExpiredLimitOrderEvent &e,
                                                                 AgentId pid, TopicId tid, Timestamp ts,
                                                                 const BusT *b) {
        on_pre_publish_event_default_dispatch(e, pid, tid, ts, b);
    }


    // Templated fallback to call the base's default handler.
    // This is crucial for the CRTP mechanism of EventBusSystem::PrePublishHook.
    template<typename E>
    void handle_pre_publish(
        const E &event,
        AgentId publisher_id,
        TopicId published_topic_id,
        Timestamp publish_time,
        const Base::BusT *bus) {
        this->handle_pre_publish_default(event, publisher_id, published_topic_id, publish_time, bus);
    }

protected:
    // Generic default dispatch target for the virtual `on_pre_publish_...` methods.
    template<typename E>
    void on_pre_publish_event_default_dispatch(
        const E &event,
        AgentId publisher_id,
        TopicId published_topic_id,
        Timestamp publish_time,
        const BusT *bus
    ) {
    }
};

#endif //PREPUBLISHHOOKBASE_H
