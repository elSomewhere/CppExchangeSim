#pragma once

#include "src/EventBus.h"
#include "src/Model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/bind.h>
#endif

#include <chrono>
#include <atomic>
#include <string>
#include <iostream>
#include <functional>

using SimulationEventBusType = ModelEventBus<>;

// WebAssembly-compatible RealTimeBus that doesn't block the browser
class RealTimeBusWasm {
public:
    explicit RealTimeBusWasm(SimulationEventBusType& bus)
        : bus_(bus), running_flag_(false), speed_factor_(1.0) {}

    ~RealTimeBusWasm() {
        if (running_flag_.load()) {
            stop();
        }
    }

    // Non-blocking start that processes events via browser event loop
    inline void run(double speed_factor = 1.0) {
        if (speed_factor <= 0) {
            LogMessage(LogLevel::ERROR, get_logger_source(), "Speed factor must be positive. Aborting run.");
            return;
        }
        if (running_flag_.load()) {
            LogMessage(LogLevel::WARNING, get_logger_source(), "Already running. Aborting new run call.");
            return;
        }

        speed_factor_ = speed_factor;
        running_flag_.store(true);
        empty_queue_polls_ = 0;
        last_real_time_of_processing_ = std::chrono::steady_clock::now();
        last_sim_time_of_processing_ = bus_.get_current_time();

        LogMessage(LogLevel::INFO, get_logger_source(), "Starting WebAssembly real-time event bus processing with speed factor: " + std::to_string(speed_factor));

#ifdef __EMSCRIPTEN__
        // Schedule the first processing cycle
        emscripten_async_call(process_events_async, this, 0);
#else
        // Fallback for non-WASM builds - process everything immediately
        process_all_events_sync();
#endif
    }

    inline void stop() {
        running_flag_.store(false);
        LogMessage(LogLevel::INFO, get_logger_source(), "Stop requested.");
    }

    inline bool is_running() const {
        return running_flag_.load();
    }

private:
    SimulationEventBusType& bus_;
    std::atomic<bool> running_flag_;
    double speed_factor_;
    int empty_queue_polls_;
    std::chrono::steady_clock::time_point last_real_time_of_processing_;
    ModelEvents::Timestamp last_sim_time_of_processing_;

    static const int max_empty_queue_polls_before_stopping = 100;
    static const int max_events_per_cycle = 10; // Process max 10 events per browser frame

#ifdef __EMSCRIPTEN__
    // Static callback for Emscripten async call
    static void process_events_async(void* userData) {
        RealTimeBusWasm* self = static_cast<RealTimeBusWasm*>(userData);
        self->process_events_cycle();
    }

    // Process a batch of events and schedule the next cycle
    void process_events_cycle() {
        if (!running_flag_.load()) {
            return;
        }

        bool should_continue = process_events_batch();
        
        if (should_continue && running_flag_.load()) {
            // Schedule next processing cycle after a small delay
            emscripten_async_call(process_events_async, this, 1); // 1ms delay
        } else {
            running_flag_.store(false);
            LogMessage(LogLevel::INFO, get_logger_source(), "WebAssembly real-time event bus processing finished.");
        }
    }
#endif

    // Process a batch of events (non-blocking)
    bool process_events_batch() {
        int events_processed = 0;
        
        while (events_processed < max_events_per_cycle && running_flag_.load()) {
            auto next_event_to_process_opt = bus_.peak();

            if (!next_event_to_process_opt) {
                if (bus_.get_event_queue_size() == 0) {
                    empty_queue_polls_++;
                    if (empty_queue_polls_ > max_empty_queue_polls_before_stopping) {
                        LogMessage(LogLevel::INFO, get_logger_source(), 
                            "Event queue has been empty for " + std::to_string(max_empty_queue_polls_before_stopping) + " cycles. Stopping.");
                        return false;
                    }
                    return true; // Continue but no events to process
                } else {
                    LogMessage(LogLevel::WARNING, get_logger_source(), 
                        "peak() returned nullopt but queue size is " + std::to_string(bus_.get_event_queue_size()));
                    return true; // Continue and retry
                }
            }

            empty_queue_polls_ = 0;
            const auto& next_event = *next_event_to_process_opt;
            
            // Calculate timing
            ModelEvents::Duration sim_duration_until_next_event = next_event.scheduled_time - last_sim_time_of_processing_;
            if (sim_duration_until_next_event < ModelEvents::Duration::zero()) {
                sim_duration_until_next_event = ModelEvents::Duration::zero();
            }

            // In WebAssembly, we process events as fast as possible within reasonable batches
            // The browser's requestAnimationFrame/setTimeout provides the timing control
            auto processed_event_opt = bus_.step();
            last_real_time_of_processing_ = std::chrono::steady_clock::now();
            
            if (processed_event_opt) {
                last_sim_time_of_processing_ = bus_.get_current_time();
                events_processed++;
            } else {
                LogMessage(LogLevel::WARNING, get_logger_source(), 
                    "bus_.step() returned no event, though peak() had indicated one.");
                break;
            }
        }

        return true; // Continue processing
    }

    // Fallback for non-WASM builds
    void process_all_events_sync() {
        while (running_flag_.load() && bus_.get_event_queue_size() > 0) {
            bus_.step();
        }
        running_flag_.store(false);
        LogMessage(LogLevel::INFO, get_logger_source(), "Synchronous event processing finished.");
    }

    std::string get_logger_source() const { return "RealTimeBusWasm"; }
}; 