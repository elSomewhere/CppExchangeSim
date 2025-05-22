#pragma once

#include "EventBus.h"
#include "Model.h" // For ModelEventBus, Timestamp, Duration, format_timestamp, format_duration

#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <iostream> // For logging in this class

// Use the specific Event Bus type alias from Model.h
using SimulationEventBusType = ModelEventBus<>;

// Define logging macros specifically for RealTimeBus if not globally available in a convenient way
// These will use the EventBusSystem's logger by default.



class RealTimeBus {
public:
    explicit RealTimeBus(SimulationEventBusType& bus)
        : bus_(bus), running_flag_(false) {}

    ~RealTimeBus() {
        if (running_flag_.load()) {
            stop(); // Ensure stop is called if destroyed while running
        }
    }

    // run() will loop until stop() is called or the event queue is empty for a while.
    // speed_factor: 1.0 = real-time, >1.0 = faster, <1.0 = slower.
    // `inline` keyword is used here as the definition is in the header.
    inline void run(double speed_factor = 1.0) {
        if (speed_factor <= 0) {
            LogMessage(LogLevel::ERROR, get_logger_source(), "Speed factor must be positive. Aborting run.");
            return;
        }
        if (running_flag_.load()) {
            LogMessage(LogLevel::WARNING, get_logger_source(), "Already running. Aborting new run call.");
            return;
        }

        running_flag_.store(true);
        LogMessage(LogLevel::INFO, get_logger_source(), "Starting real-time event bus processing with speed factor: " + std::to_string(speed_factor));

        std::chrono::steady_clock::time_point last_real_time_of_processing = std::chrono::steady_clock::now();
        ModelEvents::Timestamp last_sim_time_of_processing = bus_.get_current_time();

        int empty_queue_polls = 0;
        const int max_empty_queue_polls_before_stopping = 100; // e.g., stop after 1 sec of no events (100 * 10ms)

        while (running_flag_.load()) {
            auto next_event_to_process_opt = bus_.peak();

            if (!next_event_to_process_opt) {
                if (bus_.get_event_queue_size() == 0) {
                    empty_queue_polls++;
                    if (empty_queue_polls > max_empty_queue_polls_before_stopping) {
                        LogMessage(LogLevel::INFO, get_logger_source(), "Event queue has been empty for " + std::to_string(max_empty_queue_polls_before_stopping * 10) + "ms. Stopping real-time run.");
                        running_flag_.store(false);
                        break;
                    }
                    LogMessage(LogLevel::DEBUG, get_logger_source(), "Event queue empty. Sleeping for 10ms. Polls: " + std::to_string(empty_queue_polls));
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    last_real_time_of_processing = std::chrono::steady_clock::now();
                    continue;
                } else {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "peak() returned nullopt but queue size is " + std::to_string(bus_.get_event_queue_size()) + ". Retrying peak.");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    last_real_time_of_processing = std::chrono::steady_clock::now();
                    continue;
                }
            }
            empty_queue_polls = 0;

            const auto& next_event = *next_event_to_process_opt;
            ModelEvents::Duration sim_duration_until_next_event = next_event.scheduled_time - last_sim_time_of_processing;

            if (sim_duration_until_next_event < ModelEvents::Duration::zero()) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Next event in queue (Seq: " + std::to_string(next_event.sequence_number) +
                                                     ", Time: " + ModelEvents::format_timestamp(next_event.scheduled_time) +
                                                     ") is scheduled before current simulation time (" + ModelEvents::format_timestamp(last_sim_time_of_processing) +
                                                     "). Processing immediately in real-time terms.");
                sim_duration_until_next_event = ModelEvents::Duration::zero();
            }

            std::chrono::nanoseconds real_time_equivalent_of_sim_advance(
                static_cast<long long>(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(sim_duration_until_next_event).count()) / speed_factor
                )
            );

            auto target_real_time_for_next_processing = last_real_time_of_processing + real_time_equivalent_of_sim_advance;
            auto current_real_time_before_potential_sleep = std::chrono::steady_clock::now();

            if (target_real_time_for_next_processing > current_real_time_before_potential_sleep) {
                std::chrono::microseconds sleep_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    target_real_time_for_next_processing - current_real_time_before_potential_sleep
                );
//                std::cout<<"sleeping for: "<<sleep_duration_us.count()<<"us"<<std::endl;
                std::this_thread::sleep_for(sleep_duration_us);
            }

            if (!running_flag_.load()) {
                break;
            }

            auto processed_event_opt = bus_.step();
            last_real_time_of_processing = std::chrono::steady_clock::now();

            if (processed_event_opt) {
                last_sim_time_of_processing = bus_.get_current_time();
                if (processed_event_opt->sequence_number != next_event.sequence_number) {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "Processed event (Seq: " + std::to_string(processed_event_opt->sequence_number) +
                                                        ") differs from peeked event (Seq: " + std::to_string(next_event.sequence_number) + "). Possible concurrent modification or internal bus logic.");
                }
            } else {
                LogMessage(LogLevel::WARNING, get_logger_source(), "bus_.step() returned no event, though peak() had indicated one. Queue might be empty or concurrently modified.");
            }
        }
        running_flag_.store(false);
        LogMessage(LogLevel::INFO, get_logger_source(), "Real-time event bus processing finished.");
    }

    // Signals the run() loop to terminate.
    inline void stop() {
        running_flag_.store(false);
        LogMessage(LogLevel::INFO, get_logger_source(), "Stop requested.");
    }

    // Checks if the run loop is active.
    inline bool is_running() const {
        return running_flag_.load();
    }

private:
    SimulationEventBusType& bus_; // Reference to the actual event bus
    std::atomic<bool> running_flag_;

    std::string get_logger_source() const { return "RealTimeBus"; }
};