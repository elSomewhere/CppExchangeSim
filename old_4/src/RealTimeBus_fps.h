// file: src/RealTimeBus.h
#pragma once

#include "EventBus.h"
#include "Model.h" // For ModelEventBus, format_timestamp, format_duration

#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <vector>
#include <stdexcept> // For std::invalid_argument
#include <iostream>  // For EventBusSystem::LogMessage's underlying std::cerr

// Use the specific Event Bus type alias from Model.h
using SimulationEventBusType = ModelEventBus<>;
// Use base timestamp and duration types from EventBusSystem
using Timestamp = EventBusSystem::Timestamp;
using Duration = EventBusSystem::Duration;



class RealTimeBus {
public:
    explicit RealTimeBus(SimulationEventBusType& bus,
                         Duration sim_duration_per_batch = std::chrono::milliseconds(16))
        : bus_(bus),
          running_flag_(false),
          sim_duration_per_batch_(sim_duration_per_batch) {
        if (sim_duration_per_batch_ <= Duration::zero()) {
            LogMessage(LogLevel::ERROR, get_logger_source(), "sim_duration_per_batch must be positive. Defaulting to 100ms.");
            sim_duration_per_batch_ = std::chrono::milliseconds(100);
        }
        LogMessage(LogLevel::INFO, get_logger_source(), "RealTimeBus initialized with sim_duration_per_batch: " +
                                          ModelEvents::format_duration(sim_duration_per_batch_));
    }

    ~RealTimeBus() {
        if (running_flag_.load()) {
            stop();
        }
    }

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
        LogMessage(LogLevel::INFO, get_logger_source(), "Starting real-time event bus processing with speed factor: " + std::to_string(speed_factor) +
                                          ", sim_duration_per_batch: " + ModelEvents::format_duration(sim_duration_per_batch_));

        // Wall clock time when the run() loop started. Used as a reference for absolute real-time progression.
        const auto loop_start_wall_time = std::chrono::steady_clock::now();
        // Simulation time when the run() loop started.
        const Timestamp initial_sim_time_at_loop_start = bus_.get_current_time();

        int consecutive_empty_batches = 0;
        double sim_duration_per_batch_seconds_float = std::chrono::duration<double>(sim_duration_per_batch_).count();
        double real_seconds_per_batch_float = sim_duration_per_batch_seconds_float / speed_factor;
        if (real_seconds_per_batch_float <= 1e-9) {
             real_seconds_per_batch_float = 0.000001;
        }
        const int max_empty_batches_before_stopping = static_cast<int>(5.0 / real_seconds_per_batch_float) + 1;


        while (running_flag_.load()) {
            // Determine the simulation time window for this batch
            Timestamp sim_time_at_batch_start = bus_.get_current_time(); // Current sim time before processing this batch
            Timestamp sim_time_batch_boundary = sim_time_at_batch_start + sim_duration_per_batch_;

            size_t events_processed_this_batch = 0;
            while (running_flag_.load()) {
                auto next_event_opt = bus_.peak();
                if (!next_event_opt) {
                    break;
                }
                if (next_event_opt->scheduled_time <= sim_time_batch_boundary) {
                    auto processed_event = bus_.step();
                    if (processed_event) {
                        events_processed_this_batch++;
                    } else {
                        LogMessage(LogLevel::WARNING, get_logger_source(), "bus_.step() returned nullopt after peak() indicated an event. Breaking batch.");
                        break;
                    }
                } else {
                    break;
                }
            }

            if (!running_flag_.load()) break;

            if (events_processed_this_batch == 0 && bus_.get_event_queue_size() == 0) {
                consecutive_empty_batches++;
                if (consecutive_empty_batches > max_empty_batches_before_stopping) {
                    LogMessage(LogLevel::INFO, get_logger_source(), "Event queue empty and no events processed for " +
                                                      std::to_string(max_empty_batches_before_stopping) +
                                                      " batches. Stopping real-time run.");
                    running_flag_.store(false);
                    break;
                }
            } else {
                consecutive_empty_batches = 0;
            }

            if (!running_flag_.load()) break;

            // Adaptive sleep logic:
            // Calculate total simulation time that *should have* passed in an ideal world up to the end of this batch.
            // The bus_.get_current_time() now reflects the sim time *after* processing the current batch.
            Duration total_sim_time_processed_since_loop_start = bus_.get_current_time() - initial_sim_time_at_loop_start;

            // Calculate the ideal wall-clock duration that should have elapsed for this much simulation progress.
            auto ideal_total_wall_time_elapsed_for_sim_progress =
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(total_sim_time_processed_since_loop_start / speed_factor);

            // Determine the absolute wall-clock time when this batch ideally *should have completed*.
            auto ideal_wall_time_for_batch_completion = loop_start_wall_time + ideal_total_wall_time_elapsed_for_sim_progress;

            auto current_wall_time_after_processing = std::chrono::steady_clock::now();

            if (ideal_wall_time_for_batch_completion > current_wall_time_after_processing) {
                // We are ahead of or on the ideal overall timeline. Sleep until the ideal completion time.
                auto sleep_duration = ideal_wall_time_for_batch_completion - current_wall_time_after_processing;
                if (running_flag_.load()) {
                    std::this_thread::sleep_for(sleep_duration);
                }
            } else {
                // We are lagging behind the ideal overall timeline.
                // The ideal_wall_time_for_batch_completion is in the past. No sleep.
                auto lag_duration = current_wall_time_after_processing - ideal_wall_time_for_batch_completion;
                 if (events_processed_this_batch > 0 || bus_.get_event_queue_size() > 0) { // Avoid spamming if truly idle
                    LogMessage(LogLevel::WARNING, get_logger_source(), "System lagging ideal timeline by: " +
                        ModelEvents::format_duration(std::chrono::duration_cast<Duration>(lag_duration)) +
                        ". Events processed this batch: " + std::to_string(events_processed_this_batch));
                 }
            }
        }
        running_flag_.store(false);
        LogMessage(LogLevel::INFO, get_logger_source(), "Real-time event bus processing finished.");
    }

    inline void stop() {
        running_flag_.store(false);
        LogMessage(LogLevel::INFO, get_logger_source(), "Stop requested for RealTimeBus.");
    }

    inline bool is_running() const {
        return running_flag_.load();
    }

private:
    SimulationEventBusType& bus_;
    std::atomic<bool> running_flag_;
    Duration sim_duration_per_batch_;

    std::string get_logger_source() const { return "RealTimeBus"; }
};