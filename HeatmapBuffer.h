//
// Created by Esteban Lanter on 22.05.2025.
//

#ifndef PYCPPEXCHANGESIM_HEATMAPBUFFER_H
#define PYCPPEXCHANGESIM_HEATMAPBUFFER_H

#include <vector>
#include <deque>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cmath>
#include "src/Model.h"

// ─────────────────────────────────────────────────────────────────────────────
// 1. HeatmapSnapshot - represents L2 data at a single timestamp
// ─────────────────────────────────────────────────────────────────────────────
struct HeatmapSnapshot {
    ModelEvents::Timestamp timestamp;
    double mid_price;
    std::vector<std::pair<double, double>> bids;  // (price, volume) pairs
    std::vector<std::pair<double, double>> asks;  // (price, volume) pairs
    
    HeatmapSnapshot(ModelEvents::Timestamp ts, double mid) 
        : timestamp(ts), mid_price(mid) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// 2. PriceGrid - manages discretized price levels for heatmap
// ─────────────────────────────────────────────────────────────────────────────
class PriceGrid {
private:
    double base_price_;
    double tick_size_;
    int num_levels_;
    int center_offset_;
    
public:
    PriceGrid(double base_price, double tick_size = 1.0, int num_levels = 200)
        : base_price_(base_price), tick_size_(tick_size), num_levels_(num_levels), center_offset_(num_levels / 2) {}
    
    // Get the price level index for a given price
    int price_to_level(double price) const {
        int offset_from_base = static_cast<int>(std::round((price - base_price_) / tick_size_));
        return center_offset_ + offset_from_base;
    }
    
    // Get the price for a given level index
    double level_to_price(int level) const {
        int offset_from_center = level - center_offset_;
        return base_price_ + (offset_from_center * tick_size_);
    }
    
    // Check if level is valid
    bool is_valid_level(int level) const {
        return level >= 0 && level < num_levels_;
    }
    
    // Update base price (for when market moves significantly)
    void update_base_price(double new_base_price) {
        base_price_ = new_base_price;
    }
    
    int get_num_levels() const { return num_levels_; }
    double get_base_price() const { return base_price_; }
    double get_tick_size() const { return tick_size_; }
};

// ─────────────────────────────────────────────────────────────────────────────
// 3. HeatmapMatrix - efficient time-series matrix for volume data
// ─────────────────────────────────────────────────────────────────────────────
class HeatmapMatrix {
private:
    std::deque<std::vector<double>> bid_matrix_;  // [time][price_level] = volume
    std::deque<std::vector<double>> ask_matrix_;  // [time][price_level] = volume
    std::deque<ModelEvents::Timestamp> timestamps_;
    std::deque<double> mid_prices_;
    size_t max_time_steps_;
    int num_price_levels_;
    
public:
    HeatmapMatrix(size_t max_time_steps, int num_price_levels)
        : max_time_steps_(max_time_steps), num_price_levels_(num_price_levels) {
        // Note: std::deque doesn't have reserve() method, unlike std::vector
    }
    
    // Add a new time step
    void add_snapshot(const HeatmapSnapshot& snapshot, const PriceGrid& grid) {
        // Initialize volume vectors for this time step
        std::vector<double> bid_volumes(num_price_levels_, 0.0);
        std::vector<double> ask_volumes(num_price_levels_, 0.0);
        
        // Aggregate bid volumes by price level
        for (const auto& bid : snapshot.bids) {
            int level = grid.price_to_level(bid.first);
            if (grid.is_valid_level(level)) {
                bid_volumes[level] += bid.second;
            }
        }
        
        // Aggregate ask volumes by price level
        for (const auto& ask : snapshot.asks) {
            int level = grid.price_to_level(ask.first);
            if (grid.is_valid_level(level)) {
                ask_volumes[level] += ask.second;
            }
        }
        
        // Add to matrices
        bid_matrix_.push_back(std::move(bid_volumes));
        ask_matrix_.push_back(std::move(ask_volumes));
        timestamps_.push_back(snapshot.timestamp);
        mid_prices_.push_back(snapshot.mid_price);
        
        // Remove oldest data if buffer is full
        if (bid_matrix_.size() > max_time_steps_) {
            bid_matrix_.pop_front();
            ask_matrix_.pop_front();
            timestamps_.pop_front();
            mid_prices_.pop_front();
        }
    }
    
    // Get the latest volume data for efficient plotting
    std::pair<const std::vector<double>&, const std::vector<double>&> get_latest_volumes() const {
        static const std::vector<double> empty_vec;
        if (bid_matrix_.empty()) {
            return {empty_vec, empty_vec};
        }
        return {bid_matrix_.back(), ask_matrix_.back()};
    }
    
    // Get volume data for a specific time index (0 = oldest, size()-1 = newest)
    std::pair<std::vector<double>, std::vector<double>> get_volumes_at_time(size_t time_index) const {
        if (time_index >= bid_matrix_.size()) {
            return {{}, {}};
        }
        return {bid_matrix_[time_index], ask_matrix_[time_index]};
    }
    
    // Get statistics for normalization
    struct VolumeStats {
        double max_bid_volume = 0.0;
        double max_ask_volume = 0.0;
        double total_bid_volume = 0.0;
        double total_ask_volume = 0.0;
        double p95_bid_volume = 0.0;  // 95th percentile for outlier-resistant scaling
        double p95_ask_volume = 0.0;
    };
    
    VolumeStats get_volume_stats() const {
        VolumeStats stats;
        std::vector<double> all_bid_volumes, all_ask_volumes;
        
        for (const auto& bid_vec : bid_matrix_) {
            for (double vol : bid_vec) {
                if (vol > 0) {
                    all_bid_volumes.push_back(vol);
                    stats.total_bid_volume += vol;
                    stats.max_bid_volume = std::max(stats.max_bid_volume, vol);
                }
            }
        }
        
        for (const auto& ask_vec : ask_matrix_) {
            for (double vol : ask_vec) {
                if (vol > 0) {
                    all_ask_volumes.push_back(vol);
                    stats.total_ask_volume += vol;
                    stats.max_ask_volume = std::max(stats.max_ask_volume, vol);
                }
            }
        }
        
        // Calculate 95th percentiles
        if (!all_bid_volumes.empty()) {
            std::sort(all_bid_volumes.begin(), all_bid_volumes.end());
            size_t p95_idx = static_cast<size_t>(all_bid_volumes.size() * 0.95);
            stats.p95_bid_volume = all_bid_volumes[std::min(p95_idx, all_bid_volumes.size() - 1)];
        }
        
        if (!all_ask_volumes.empty()) {
            std::sort(all_ask_volumes.begin(), all_ask_volumes.end());
            size_t p95_idx = static_cast<size_t>(all_ask_volumes.size() * 0.95);
            stats.p95_ask_volume = all_ask_volumes[std::min(p95_idx, all_ask_volumes.size() - 1)];
        }
        
        return stats;
    }
    
    size_t get_time_steps() const { return timestamps_.size(); }
    size_t get_max_time_steps() const { return max_time_steps_; }
    int get_num_price_levels() const { return num_price_levels_; }
    
    const std::deque<ModelEvents::Timestamp>& get_timestamps() const { return timestamps_; }
    const std::deque<double>& get_mid_prices() const { return mid_prices_; }
};

// ─────────────────────────────────────────────────────────────────────────────
// 4. Main HeatmapBuffer class - manages the complete heatmap data system
// ─────────────────────────────────────────────────────────────────────────────
class HeatmapBuffer {
private:
    std::unique_ptr<PriceGrid> price_grid_;
    std::unique_ptr<HeatmapMatrix> matrix_;
    size_t buffer_size_;
    int num_price_levels_;
    double tick_size_;
    
    // Statistics for auto-adjustment
    double last_mid_price_;
    size_t snapshots_since_grid_update_;
    static constexpr size_t GRID_UPDATE_FREQUENCY = 50;  // Update grid every N snapshots
    
public:
    HeatmapBuffer(size_t buffer_size = 300, int num_price_levels = 200, double tick_size = 1.0)
        : buffer_size_(buffer_size), num_price_levels_(num_price_levels), tick_size_(tick_size),
          last_mid_price_(0.0), snapshots_since_grid_update_(0) {
        
        // Will be initialized on first snapshot
        price_grid_ = nullptr;
        matrix_ = nullptr;
    }
    
    // Add a new L2 snapshot
    void add_l2_snapshot(const ModelEvents::LTwoOrderBookEvent& event) {
        // Calculate mid price
        double best_bid = 0.0, best_ask = 0.0;
        if (!event.bids.empty()) {
            best_bid = ModelEvents::price_to_float(event.bids[0].first);
        }
        if (!event.asks.empty()) {
            best_ask = ModelEvents::price_to_float(event.asks[0].first);
        }
        
        double mid_price = (best_bid + best_ask) / 2.0;
        if (mid_price <= 0.0) {
            // Fallback if no valid mid price
            mid_price = last_mid_price_ > 0 ? last_mid_price_ : 50000.0;
        }
        
        // Initialize grid and matrix on first snapshot
        if (!price_grid_) {
            price_grid_ = std::make_unique<PriceGrid>(mid_price, tick_size_, num_price_levels_);
            matrix_ = std::make_unique<HeatmapMatrix>(buffer_size_, num_price_levels_);
            last_mid_price_ = mid_price;
        }
        
        // Check if we need to recenter the price grid
        if (snapshots_since_grid_update_ >= GRID_UPDATE_FREQUENCY) {
            double price_drift = std::abs(mid_price - price_grid_->get_base_price());
            if (price_drift > price_grid_->get_tick_size() * (num_price_levels_ / 8)) {
                // Recenter grid around current mid price
                price_grid_->update_base_price(mid_price);
            }
            snapshots_since_grid_update_ = 0;
        }
        
        // Create snapshot
        HeatmapSnapshot snapshot(event.ingress_ts, mid_price);
        
        // Convert bids and asks to floating point
        for (const auto& bid : event.bids) {
            snapshot.bids.emplace_back(
                ModelEvents::price_to_float(bid.first),
                ModelEvents::quantity_to_float(bid.second)
            );
        }
        
        for (const auto& ask : event.asks) {
            snapshot.asks.emplace_back(
                ModelEvents::price_to_float(ask.first),
                ModelEvents::quantity_to_float(ask.second)
            );
        }
        
        // Add to matrix
        matrix_->add_snapshot(snapshot, *price_grid_);
        
        last_mid_price_ = mid_price;
        snapshots_since_grid_update_++;
    }
    
    // Get data for visualization (latest snapshot only for efficiency)
    struct VisualizationData {
        std::vector<double> bid_volumes;
        std::vector<double> ask_volumes;
        double mid_price;
        double base_price;
        double tick_size;
        int num_levels;
        ModelEvents::Timestamp timestamp;
        HeatmapMatrix::VolumeStats stats;
    };
    
    VisualizationData get_visualization_data() const {
        VisualizationData data;
        
        if (!matrix_ || matrix_->get_time_steps() == 0) {
            return data;  // Return empty data
        }
        
        auto latest_volumes = matrix_->get_latest_volumes();
        data.bid_volumes = latest_volumes.first;
        data.ask_volumes = latest_volumes.second;
        data.mid_price = matrix_->get_mid_prices().back();
        data.base_price = price_grid_->get_base_price();
        data.tick_size = price_grid_->get_tick_size();
        data.num_levels = price_grid_->get_num_levels();
        data.timestamp = matrix_->get_timestamps().back();
        data.stats = matrix_->get_volume_stats();
        
        return data;
    }
    
    // Get full time-series data for heatmap rendering
    struct HeatmapData {
        std::vector<std::vector<double>> bid_matrix;  // [time][price_level]
        std::vector<std::vector<double>> ask_matrix;  // [time][price_level]
        std::vector<double> mid_prices;
        std::vector<ModelEvents::Timestamp> timestamps;
        double base_price;
        double tick_size;
        int num_levels;
        HeatmapMatrix::VolumeStats stats;
    };
    
    HeatmapData get_heatmap_data() const {
        HeatmapData data;
        
        if (!matrix_ || matrix_->get_time_steps() == 0) {
            return data;
        }
        
        // Copy all time series data
        size_t num_times = matrix_->get_time_steps();
        data.bid_matrix.reserve(num_times);
        data.ask_matrix.reserve(num_times);
        
        for (size_t t = 0; t < num_times; ++t) {
            auto volumes = matrix_->get_volumes_at_time(t);
            data.bid_matrix.push_back(std::move(volumes.first));
            data.ask_matrix.push_back(std::move(volumes.second));
        }
        
        // Copy metadata
        const auto& timestamps = matrix_->get_timestamps();
        const auto& mid_prices = matrix_->get_mid_prices();
        data.timestamps.assign(timestamps.begin(), timestamps.end());
        data.mid_prices.assign(mid_prices.begin(), mid_prices.end());
        
        data.base_price = price_grid_->get_base_price();
        data.tick_size = price_grid_->get_tick_size();
        data.num_levels = price_grid_->get_num_levels();
        data.stats = matrix_->get_volume_stats();
        
        return data;
    }
    
    // Configuration getters/setters
    size_t get_buffer_size() const { return buffer_size_; }
    int get_num_price_levels() const { return num_price_levels_; }
    double get_tick_size() const { return tick_size_; }
    size_t get_current_size() const { return matrix_ ? matrix_->get_time_steps() : 0; }
    
    void set_buffer_size(size_t new_size) {
        buffer_size_ = new_size;
        // TODO: Could resize existing matrix here if needed
    }
    
    bool is_initialized() const { return price_grid_ != nullptr && matrix_ != nullptr; }
};

#endif //PYCPPEXCHANGESIM_HEATMAPBUFFER_H 