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

    // Frame-based run() method with independent simulation and visualization speeds
    // visualization_fps: frames per second for visualization updates (e.g., 30, 60)
    // simulation_speed_factor: how fast simulation runs (1.0 = real-time, >1.0 = faster)
    inline void run(double visualization_fps = 30.0, double simulation_speed_factor = 1.0) {
        if (visualization_fps <= 0 || simulation_speed_factor <= 0) {
            LogMessage(LogLevel::ERROR, get_logger_source(), 
                "Visualization FPS and simulation speed factor must be positive. Aborting run.");
            return;
        }
        if (running_flag_.load()) {
            LogMessage(LogLevel::WARNING, get_logger_source(), "Already running. Aborting new run call.");
            return;
        }

        running_flag_.store(true);
        LogMessage(LogLevel::INFO, get_logger_source(), 
            "Starting frame-based event bus processing. Visualization: " + std::to_string(visualization_fps) + 
            " FPS, Simulation speed: " + std::to_string(simulation_speed_factor) + "x");

        // Calculate frame timing
        const std::chrono::microseconds frame_duration_us(
            static_cast<long long>(1000000.0 / visualization_fps)
        );
        
        // Calculate how much simulation time advances per visualization frame
        const ModelEvents::Duration sim_time_per_frame = 
            std::chrono::duration_cast<ModelEvents::Duration>(
                std::chrono::microseconds(static_cast<long long>(
                    (1000000.0 / visualization_fps) * simulation_speed_factor
                ))
            );

        auto last_frame_real_time = std::chrono::steady_clock::now();
        ModelEvents::Timestamp current_sim_time = bus_.get_current_time();
        ModelEvents::Timestamp target_sim_time = current_sim_time + sim_time_per_frame;

        int empty_frames = 0;
        const int max_empty_frames_before_stopping = static_cast<int>(visualization_fps * 2); // 2 seconds worth

        LogMessage(LogLevel::DEBUG, get_logger_source(), 
            "Frame duration: " + std::to_string(frame_duration_us.count()) + "us, " +
            "Sim time per frame: " + ModelEvents::format_duration(sim_time_per_frame));

        while (running_flag_.load()) {
            auto frame_start_time = std::chrono::steady_clock::now();
            
            // Process all events within this frame's simulation time window
            int events_processed_this_frame = 0;
            bool has_events = false;

            while (running_flag_.load()) {
                auto next_event_opt = bus_.peak();
                
                if (!next_event_opt) {
                    // Check if queue is truly empty
                    if (bus_.get_event_queue_size() == 0) {
                        break; // No more events to process in this frame
                    } else {
                        LogMessage(LogLevel::WARNING, get_logger_source(), 
                            "peak() returned nullopt but queue size is " + std::to_string(bus_.get_event_queue_size()));
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                        continue;
                    }
                }

                const auto& next_event = *next_event_opt;
                
                // Check if this event falls within our current frame's time window
                if (next_event.scheduled_time > target_sim_time) {
                    // Event is beyond current frame, stop processing for this frame
                    break;
                }

                // Process this event (it's within our time window)
                auto processed_event_opt = bus_.step();
                
                if (processed_event_opt) {
                    events_processed_this_frame++;
                    has_events = true;
                    current_sim_time = bus_.get_current_time();
                    
                    if (processed_event_opt->sequence_number != next_event.sequence_number) {
                        LogMessage(LogLevel::WARNING, get_logger_source(), 
                            "Processed event (Seq: " + std::to_string(processed_event_opt->sequence_number) +
                            ") differs from peeked event (Seq: " + std::to_string(next_event.sequence_number) + ")");
                    }
                } else {
                    LogMessage(LogLevel::WARNING, get_logger_source(), 
                        "bus_.step() returned no event despite peak() indicating one");
                    break;
                }
            }

            // Update frame statistics
            if (has_events) {
                empty_frames = 0;
                if (events_processed_this_frame > 1) {
                    LogMessage(LogLevel::DEBUG, get_logger_source(), 
                        "Processed " + std::to_string(events_processed_this_frame) + 
                        " events in frame. Sim time: " + ModelEvents::format_timestamp(current_sim_time));
                }
            } else {
                empty_frames++;
                if (empty_frames > max_empty_frames_before_stopping) {
                    LogMessage(LogLevel::INFO, get_logger_source(), 
                        "No events for " + std::to_string(empty_frames) + " frames (" + 
                        std::to_string(empty_frames / visualization_fps) + " seconds). Stopping.");
                    running_flag_.store(false);
                    break;
                }
            }

            // Advance simulation time target for next frame
            target_sim_time += sim_time_per_frame;

            // Sleep until next frame is due
            auto next_frame_time = last_frame_real_time + frame_duration_us;
            auto current_real_time = std::chrono::steady_clock::now();
            
            if (next_frame_time > current_real_time) {
                auto sleep_duration = next_frame_time - current_real_time;
                std::this_thread::sleep_for(sleep_duration);
                last_frame_real_time = next_frame_time;
            } else {
                // We're running behind schedule
                last_frame_real_time = current_real_time;
                if (current_real_time > next_frame_time + frame_duration_us) {
                    LogMessage(LogLevel::WARNING, get_logger_source(), 
                        "Frame processing is running behind schedule by " +
                        std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                            current_real_time - next_frame_time).count()) + "us");
                }
            }
        }

        running_flag_.store(false);
        LogMessage(LogLevel::INFO, get_logger_source(), "Frame-based event bus processing finished.");
    }

    // Legacy method for backward compatibility
    inline void run(double speed_factor) {
        run(30.0, speed_factor); // Default to 30 FPS visualization
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

    // Update speeds dynamically while running
    inline void update_speeds(double visualization_fps, double simulation_speed_factor) {
        // Note: This would require the main loop to check for updated parameters
        // For now, this is a placeholder for future dynamic speed control
        LogMessage(LogLevel::INFO, get_logger_source(), 
            "Dynamic speed update requested: " + std::to_string(visualization_fps) + 
            " FPS, " + std::to_string(simulation_speed_factor) + "x speed");
    }

private:
    SimulationEventBusType& bus_; // Reference to the actual event bus
    std::atomic<bool> running_flag_;

    std::string get_logger_source() const { return "RealTimeBus"; }
};