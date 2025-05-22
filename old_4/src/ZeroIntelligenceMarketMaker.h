#pragma once

#include "AlgoBase.h"       // Base class using CRTP
#include "Model.h"          // Event types, conversions, aliases
#include "Logging.h"
#include <random>           // For C++ random number generation
#include <cmath>            // For std::log, std::pow, std::max, std::min
#include <vector>
#include <optional>
#include <string>
#include <chrono>
#include <algorithm>        // For std::min, std::max
#include <utility>          // For std::swap

namespace trading {
    namespace algo {
        using ModelEvents::PriceType;
        using ModelEvents::QuantityType;
        using ModelEvents::SymbolType;
        using ModelEvents::Side;
        using ModelEvents::Duration;
        using ModelEvents::Timestamp;
        using ModelEvents::ClientOrderIdType;
        using ModelEvents::ExchangeOrderIdType;
        using ModelEvents::TopicId;
        using ModelEvents::StreamId;
        using ModelEvents::SequenceNumber;
        using ModelEvents::LTwoOrderBookEvent;
        using ModelEvents::LimitOrderAckEvent;
        using ModelEvents::MarketOrderAckEvent;

        class ZeroIntelligenceMarketMaker : public AlgoBase<ZeroIntelligenceMarketMaker> {
        private:
            int min_spread_bps_;
            int max_spread_bps_;
            double min_order_size_float_;
            double max_order_size_float_;
            const size_t imbalance_levels_;
            const int max_imbalance_adj_bps_;
            const std::string timeout_dist_;
            const double median_timeout_seconds_;
            const double sigma_timeout_;
            const double pareto_alpha_;
            const double pareto_scale_;
            const double tail_mix_;
            const double min_timeout_s_;
            const double max_timeout_s_;
            double default_price_float_;
            ModelEvents::OrderBookLevel current_bids_;
            ModelEvents::OrderBookLevel current_asks_;
            std::optional<ClientOrderIdType> active_bid_cid_;
            std::optional<ClientOrderIdType> active_ask_cid_;
            std::default_random_engine random_engine_;
            std::uniform_real_distribution<double> uniform_dist_;
            std::lognormal_distribution<double> lognormal_dist_;

        public:
            // ------------------------------------------------------------------ #
            // Constructor
            // ------------------------------------------------------------------ #
            ZeroIntelligenceMarketMaker(
                    SymbolType exchange_name, // Use SymbolType alias
                    // --- Knobs ---
                    int min_spread_bps,
                    int max_spread_bps,
                    double min_order_size_float,
                    double max_order_size_float,
                    size_t imbalance_levels, // Changed to size_t
                    int max_imbalance_adj_bps,
                    const std::string& timeout_dist = "pareto",
                    double median_timeout_seconds = 60.0,
                    double sigma_timeout = 1.4,
                    double pareto_alpha = 1.1,
                    double pareto_scale = 3600.0,
                    double tail_mix = 0.1,
                    double min_timeout_s = 5.0,
                    double max_timeout_s = 3600.0 * 24.0,
                    unsigned int zimm_seed = 0
            ) : AlgoBase<ZeroIntelligenceMarketMaker>(exchange_name),
                min_spread_bps_(min_spread_bps),
                max_spread_bps_(max_spread_bps),
                min_order_size_float_(min_order_size_float),
                max_order_size_float_(max_order_size_float),
                imbalance_levels_(imbalance_levels),
                max_imbalance_adj_bps_(max_imbalance_adj_bps),
                timeout_dist_(timeout_dist),
                median_timeout_seconds_(median_timeout_seconds),
                sigma_timeout_(sigma_timeout),
                pareto_alpha_(pareto_alpha),
                pareto_scale_(pareto_scale),
                tail_mix_(tail_mix),
                min_timeout_s_(min_timeout_s),
                max_timeout_s_(max_timeout_s),
                default_price_float_(50000.0),
                current_bids_(),
                current_asks_(),
                active_bid_cid_(std::nullopt),
                active_ask_cid_(std::nullopt),
                random_engine_((zimm_seed == 0) ? std::random_device{}() : zimm_seed),
                uniform_dist_(0.0, 1.0),
                lognormal_dist_(std::log(median_timeout_seconds_), sigma_timeout_)
            {
                // Basic sanity checks (use std::swap) - NOW VALID as members are non-const
                if (min_order_size_float_ > max_order_size_float_) {
                    std::swap(min_order_size_float_, max_order_size_float_);
                }
                if (min_spread_bps_ > max_spread_bps_) {
                    std::swap(min_spread_bps_, max_spread_bps_);
                }

                LogMessage(LogLevel::DEBUG, this->get_logger_source(), "ZIMM init (ID will be set by bus): size=[" + std::to_string(min_order_size_float_) + "–" + std::to_string(max_order_size_float_) + "], spread=[" + std::to_string(min_spread_bps_) + "–" + std::to_string(max_spread_bps_) + "] bps" + ", timeout-dist=" + timeout_dist_);
            }

            // Override virtual destructor
            virtual ~ZeroIntelligenceMarketMaker() override = default;

        private: // Make helpers private

            // ------------------------------------------------------------------ #
            // Helper: per-order lifetime sampling
            // ------------------------------------------------------------------ #
            double _sample_pareto() {
                // Inverse transform sampling for Pareto Type I
                // P(x) = 1 - (scale / x)^alpha for x >= scale
                // U = 1 - (scale / x)^alpha => x = scale * (1 - U)^(-1/alpha)
                // Since U is uniform(0,1), 1-U is also uniform(0,1).
                // => x = scale * U^(-1/alpha)
                if (pareto_alpha_ <= 0) return pareto_scale_; // Avoid issues
                double u = uniform_dist_(random_engine_);
                if (u == 0) u = std::numeric_limits<double>::min(); // Avoid division by zero
                return pareto_scale_ * std::pow(u, -1.0 / pareto_alpha_);
            }

            double _draw_timeout_seconds() {
                if (timeout_dist_ == "pareto") {
                    return min_timeout_s_ + _sample_pareto();
                }

                if (timeout_dist_ == "lognormal_pareto_mix") {
                    if (uniform_dist_(random_engine_) < tail_mix_) {
                        return min_timeout_s_ + _sample_pareto();
                    }
                    // else fall through to log-normal
                }

                // Default to lognormal
                return lognormal_dist_(random_engine_);
            }

            // Returns Duration directly
            Duration _sample_timeout_duration() {
                double seconds = _draw_timeout_seconds();
                // Clamp the duration
                seconds = std::max(min_timeout_s_, std::min(seconds, max_timeout_s_));
                // Use the helper function that should now be in Model.h
                return ModelEvents::float_seconds_to_duration(seconds);
            }

            // ------------------------------------------------------------------ #
            // Helper: imbalance calculation
            // ------------------------------------------------------------------ #
            double _calculate_imbalance_adjustment_bps() {
                if (current_bids_.empty() && current_asks_.empty()) {
                    return 0.0;
                }

                QuantityType bid_vol_int = 0;
                size_t levels_to_sum_bid = std::min(imbalance_levels_, current_bids_.size());
                for (size_t i = 0; i < levels_to_sum_bid; ++i) {
                    bid_vol_int += current_bids_[i].second;
                }

                QuantityType ask_vol_int = 0;
                size_t levels_to_sum_ask = std::min(imbalance_levels_, current_asks_.size());
                for (size_t i = 0; i < levels_to_sum_ask; ++i) {
                    ask_vol_int += current_asks_[i].second;
                }

                // Convert to double for calculation using helpers from Model.h
                double bid_vol = ModelEvents::quantity_to_float(bid_vol_int);
                double ask_vol = ModelEvents::quantity_to_float(ask_vol_int);

                double total_vol = bid_vol + ask_vol;
                if (total_vol <= 1e-9) { // Use tolerance for floating point comparison
                    return 0.0;
                }

                double rho = bid_vol / total_vol;
                double skew = (rho - 0.5) * 2.0; // Skew between -1.0 (all ask) and +1.0 (all bid)

                // Adjustment: More bid volume (rho > 0.5, skew > 0) -> lower bid/ask (negative adjustment)
                // More ask volume (rho < 0.5, skew < 0) -> higher bid/ask (positive adjustment)
                return -skew * max_imbalance_adj_bps_; // Note the negative sign compared to Python
            }

            // ------------------------------------------------------------------ #
            // Main quoting logic
            // ------------------------------------------------------------------ #
            void check_and_place_orders() {
                // Check if bus is available (important if called before registration completes)
                if (!this->bus_) return;

                double imbalance_adj_bps = _calculate_imbalance_adjustment_bps();
                _check_and_place_bid(imbalance_adj_bps);
                _check_and_place_ask(imbalance_adj_bps);
            }

            void _check_and_place_bid(double imbalance_adj_bps) {
                if (active_bid_cid_) {
                    return; // existing bid still resting
                }

                std::uniform_real_distribution<double> spread_dist(min_spread_bps_, max_spread_bps_);
                std::uniform_real_distribution<double> edge_dist(0.0, min_spread_bps_ / 2.0); // For crossing midpoint slightly
                std::uniform_real_distribution<double> size_dist(min_order_size_float_, max_order_size_float_);

                // --- Decide base price ---
                double base_bid_float;
                if (!current_asks_.empty()) {
                    // Use helper from Model.h
                    double best_ask_float = ModelEvents::price_to_float(current_asks_[0].first);
                    double spread_bps = spread_dist(random_engine_);
                    // Use constant from Model.h
                    base_bid_float = best_ask_float * (1.0 - spread_bps / ModelEvents::BPS_DIVISOR);
                } else if (!current_bids_.empty()) {
                    // Use helper from Model.h
                    double best_bid_float = ModelEvents::price_to_float(current_bids_[0].first);
                    // Use constant from Model.h
                    base_bid_float = best_bid_float * (1.0 - edge_dist(random_engine_) / ModelEvents::BPS_DIVISOR);
                } else {
                    // Use constant from Model.h
                    base_bid_float = default_price_float_ * (1.0 - spread_dist(random_engine_) / ModelEvents::BPS_DIVISOR);
                }

                // Apply imbalance adjustment
                // Use constant from Model.h
                double final_price_float = base_bid_float * (1.0 + imbalance_adj_bps / ModelEvents::BPS_DIVISOR); // Add positive adjustment to lift price
                // Use helper from Model.h
                PriceType target_price = ModelEvents::float_to_price(final_price_float);

                // --- Decide size ---
                double volume_float = size_dist(random_engine_);
                // Use helper from Model.h
                QuantityType target_qty = ModelEvents::float_to_quantity(volume_float);

                if (target_price <= 0 || target_qty <= 0) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Calculated invalid bid price/qty: P=" + std::to_string(target_price) + " Q=" + std::to_string(target_qty));
                    return;
                }

                // --- Timeout ---
                Duration timeout = _sample_timeout_duration();

                // --- Place Order ---
                // Note: Python uses False for BUY, True for SELL. C++ uses Enum.
                active_bid_cid_ = this->create_limit_order(
                        this->get_exchange_name(), Side::BUY, target_price, target_qty, timeout
                );

                if (active_bid_cid_) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(this->get_id()) +
                                                         " BID: p=" + std::to_string(ModelEvents::price_to_float(target_price)) +
                                                         ", q=" + std::to_string(target_qty) + // Log integer quantity
                                                         ", τ=" + std::to_string(ModelEvents::duration_to_float_seconds(timeout)) + "s" +
                                                         " (CID: " + std::to_string(*active_bid_cid_) + ")");
                } else {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Agent " + std::to_string(this->get_id()) + " FAILED to create bid order.");
                }
            }


            void _check_and_place_ask(double imbalance_adj_bps) {
                if (active_ask_cid_) {
                    return; // existing ask still resting
                }

                std::uniform_real_distribution<double> spread_dist(min_spread_bps_, max_spread_bps_);
                std::uniform_real_distribution<double> edge_dist(0.0, min_spread_bps_ / 2.0);
                std::uniform_real_distribution<double> size_dist(min_order_size_float_, max_order_size_float_);


                // --- Decide base price ---
                double base_ask_float;
                if (!current_bids_.empty()) {
                    // Use helper from Model.h
                    double best_bid_float = ModelEvents::price_to_float(current_bids_[0].first);
                    double spread_bps = spread_dist(random_engine_);
                    // Use constant from Model.h
                    base_ask_float = best_bid_float * (1.0 + spread_bps / ModelEvents::BPS_DIVISOR);
                } else if (!current_asks_.empty()) {
                    // Use helper from Model.h
                    double best_ask_float = ModelEvents::price_to_float(current_asks_[0].first);
                    // Use constant from Model.h
                    base_ask_float = best_ask_float * (1.0 + edge_dist(random_engine_) / ModelEvents::BPS_DIVISOR);
                } else {
                    // Use constant from Model.h
                    base_ask_float = default_price_float_ * (1.0 + spread_dist(random_engine_) / ModelEvents::BPS_DIVISOR);
                }

                // Apply imbalance adjustment
                // Use constant from Model.h
                double final_price_float = base_ask_float * (1.0 + imbalance_adj_bps / ModelEvents::BPS_DIVISOR); // Add positive adjustment to lift price
                // Use helper from Model.h
                PriceType target_price = ModelEvents::float_to_price(final_price_float);

                // --- Decide size ---
                double volume_float = size_dist(random_engine_);
                // Use helper from Model.h
                QuantityType target_qty = ModelEvents::float_to_quantity(volume_float);

                if (target_price <= 0 || target_qty <= 0) {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Calculated invalid ask price/qty: P=" + std::to_string(target_price) + " Q=" + std::to_string(target_qty));
                    return;
                }

                // --- Timeout ---
                Duration timeout = _sample_timeout_duration();

                // --- Place Order ---
                active_ask_cid_ = this->create_limit_order(
                        this->get_exchange_name(), Side::SELL, target_price, target_qty, timeout
                );

                if (active_ask_cid_) {
                    // Use helpers from Model.h for logging
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(this->get_id()) +
                                                         " ASK: p=" + std::to_string(ModelEvents::price_to_float(target_price)) +
                                                         ", q=" + std::to_string(target_qty) +
                                                         ", τ=" + std::to_string(ModelEvents::duration_to_float_seconds(timeout)) + "s" +
                                                         " (CID: " + std::to_string(*active_ask_cid_) + ")");
                } else {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Agent " + std::to_string(this->get_id()) + " FAILED to create ask order.");
                }
            }


        protected: // Implement the pure virtual on_... methods from AlgoBase

            // --- Event Handlers (Overrides) ---
            void on_LTwoOrderBookEvent(const ModelEvents::LTwoOrderBookEvent& event) override {
                current_bids_ = event.bids;
                current_asks_ = event.asks;
                check_and_place_orders();
            }

            void on_LimitOrderAckEvent(const ModelEvents::LimitOrderAckEvent& event) override {
                LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Received Limit ACK for CID: " + std::to_string(event.client_order_id));
            }

            void on_FullFillLimitOrderEvent(const ModelEvents::FullFillLimitOrderEvent& event) override {
                LogMessage(LogLevel::INFO, this->get_logger_source(), "Received Full Fill for CID: " + std::to_string(event.client_order_id));
                bool re_quote = false;
                if (active_bid_cid_ && *active_bid_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Bid CID " + std::to_string(*active_bid_cid_) + " was fully filled.");
                    active_bid_cid_.reset();
                    re_quote = true;
                } else if (active_ask_cid_ && *active_ask_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Ask CID " + std::to_string(*active_ask_cid_) + " was fully filled.");
                    active_ask_cid_.reset();
                    re_quote = true;
                }
                if(re_quote) {
                    check_and_place_orders();
                }
            }

            void on_PartialFillLimitOrderEvent(const ModelEvents::PartialFillLimitOrderEvent& event) override {
                LogMessage(LogLevel::INFO, this->get_logger_source(), "Received Partial Fill for CID: " + std::to_string(event.client_order_id) +
                                                    ", Filled: " + std::to_string(event.fill_qty) +
                                                    ", Leaves: " + std::to_string(event.leaves_qty));
            }

            void on_FullCancelLimitOrderAckEvent(const ModelEvents::FullCancelLimitOrderAckEvent& event) override {
                LogMessage(LogLevel::INFO, this->get_logger_source(), "Received Full Cancel ACK for Target CID: " + std::to_string(event.target_order_id) +
                                                    " (Cancel Request CID: " + std::to_string(event.client_order_id) + ")");
                bool re_quote = false;
                if (active_bid_cid_ && *active_bid_cid_ == event.target_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Bid CID " + std::to_string(*active_bid_cid_) + " was successfully cancelled.");
                    active_bid_cid_.reset();
                    re_quote = true;
                } else if (active_ask_cid_ && *active_ask_cid_ == event.target_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Ask CID " + std::to_string(*active_ask_cid_) + " was successfully cancelled.");
                    active_ask_cid_.reset();
                    re_quote = true;
                }
                if(re_quote) {
                    check_and_place_orders();
                }
            }

            void on_PartialCancelLimitAckEvent(const ModelEvents::PartialCancelLimitAckEvent& event) override {
                LogMessage(LogLevel::INFO, this->get_logger_source(), "Received Partial Cancel ACK for Target CID: " + std::to_string(event.target_order_id) +
                                                    " (Cancel Request CID: " + std::to_string(event.client_order_id) + ")" +
                                                    ", Remaining Qty: " + std::to_string(event.remaining_qty));
            }

            void on_LimitOrderExpiredEvent(const ModelEvents::LimitOrderExpiredEvent& event) override {
                LogMessage(LogLevel::INFO, this->get_logger_source(), "Received Direct Limit Order EXPIRED event for CID: " + std::to_string(event.client_order_id));
                bool re_quote = false;
                if (active_bid_cid_ && *active_bid_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Bid CID " + std::to_string(*active_bid_cid_) + " expired (direct event).");
                    active_bid_cid_.reset();
                    re_quote = true;
                } else if (active_ask_cid_ && *active_ask_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Ask CID " + std::to_string(*active_ask_cid_) + " expired (direct event).");
                    active_ask_cid_.reset();
                    re_quote = true;
                }
                if(re_quote) {
                    check_and_place_orders();
                }
            }

            void on_FullCancelLimitOrderRejectEvent(const ModelEvents::FullCancelLimitOrderRejectEvent& event) override {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "Full Cancel Limit REJECTED for Cancel CID: " + std::to_string(event.client_order_id));
            }

            void on_PartialCancelLimitOrderRejectEvent(const ModelEvents::PartialCancelLimitOrderRejectEvent& event) override {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "Partial Cancel Limit REJECTED for Cancel CID: " + std::to_string(event.client_order_id));
            }


            void on_Bang(const ModelEvents::Bang& event) override {
                LogMessage(LogLevel::INFO, this->get_logger_source(), "Received Bang! Resetting state.");
                this->create_full_cancel_all_limit_orders();
                current_bids_.clear();
                current_asks_.clear();
                active_bid_cid_.reset();
                active_ask_cid_.reset();
            }

            void on_TradeEvent(const ModelEvents::TradeEvent& event) override {
                LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Observed Trade: " + event.to_string());
            }

            void on_AckTriggerExpiredLimitOrderEvent(const ModelEvents::AckTriggerExpiredLimitOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Received AckTriggerExpired for Target CID: " + std::to_string(event.client_order_id));
                bool re_quote = false;
                if (active_bid_cid_ && *active_bid_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Bid CID " + std::to_string(*active_bid_cid_) + " expired (via trigger).");
                    active_bid_cid_.reset();
                    re_quote = true;
                } else if (active_ask_cid_ && *active_ask_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Ask CID " + std::to_string(*active_ask_cid_) + " expired (via trigger).");
                    active_ask_cid_.reset();
                    re_quote = true;
                }
                if(re_quote) {
                    check_and_place_orders();
                }
            }

            void on_LimitOrderRejectEvent(const ModelEvents::LimitOrderRejectEvent& event) override {
                LogMessage(LogLevel::WARNING, this->get_logger_source(), "Limit Order REJECTED for CID: " + std::to_string(event.client_order_id));
                bool re_quote = false;
                if (active_bid_cid_ && *active_bid_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Bid CID " + std::to_string(*active_bid_cid_) + " was rejected.");
                    active_bid_cid_.reset();
                    re_quote = true;
                } else if (active_ask_cid_ && *active_ask_cid_ == event.client_order_id) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Active Ask CID " + std::to_string(*active_ask_cid_) + " was rejected.");
                    active_ask_cid_.reset();
                    re_quote = true;
                }
                if(re_quote) {
                    check_and_place_orders();
                }
            }

            void on_CheckLimitOrderExpirationEvent(const ModelEvents::CheckLimitOrderExpirationEvent& event) override {
                 LogMessage(LogLevel::DEBUG, this->get_logger_source(), "ZIMM ignoring CheckLimitOrderExpirationEvent for target XID: " + std::to_string(event.target_exchange_order_id));
            }

            // --- Market Order Handlers (ZIMM doesn't place market orders, but implements handlers) ---
            void on_MarketOrderAckEvent(const ModelEvents::MarketOrderAckEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring MarketOrderAckEvent"); }
            void on_MarketOrderRejectEvent(const ModelEvents::MarketOrderRejectEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring MarketOrderRejectEvent"); }
            void on_FullFillMarketOrderEvent(const ModelEvents::FullFillMarketOrderEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring FullFillMarketOrderEvent"); }
            void on_PartialFillMarketOrderEvent(const ModelEvents::PartialFillMarketOrderEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring PartialFillMarketOrderEvent"); }
            void on_MarketOrderExpiredEvent(const ModelEvents::MarketOrderExpiredEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring MarketOrderExpiredEvent"); }
            void on_FullCancelMarketOrderAckEvent(const ModelEvents::FullCancelMarketOrderAckEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring FullCancelMarketOrderAckEvent"); }
            void on_PartialCancelMarketAckEvent(const ModelEvents::PartialCancelMarketAckEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring PartialCancelMarketAckEvent"); }
            void on_FullCancelMarketOrderRejectEvent(const ModelEvents::FullCancelMarketOrderRejectEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring FullCancelMarketOrderRejectEvent"); }
            void on_PartialCancelMarketOrderRejectEvent(const ModelEvents::PartialCancelMarketOrderRejectEvent& event) override { LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring PartialCancelMarketOrderRejectEvent"); }

            // --- ADDED OVERRIDES FOR NEW PURE VIRTUALS IN AlgoBase ---
            void on_LimitOrderEvent(const ModelEvents::LimitOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (outgoing) LimitOrderEvent: " + event.to_string());
            }
            void on_MarketOrderEvent(const ModelEvents::MarketOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (outgoing) MarketOrderEvent: " + event.to_string());
            }
            void on_PartialCancelLimitOrderEvent(const ModelEvents::PartialCancelLimitOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (outgoing) PartialCancelLimitOrderEvent: " + event.to_string());
            }
            void on_PartialCancelMarketOrderEvent(const ModelEvents::PartialCancelMarketOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (outgoing) PartialCancelMarketOrderEvent: " + event.to_string());
            }
            void on_FullCancelLimitOrderEvent(const ModelEvents::FullCancelLimitOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (outgoing) FullCancelLimitOrderEvent: " + event.to_string());
            }
            void on_FullCancelMarketOrderEvent(const ModelEvents::FullCancelMarketOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (outgoing) FullCancelMarketOrderEvent: " + event.to_string());
            }
            void on_TriggerExpiredLimitOrderEvent(const ModelEvents::TriggerExpiredLimitOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (internal) TriggerExpiredLimitOrderEvent: " + event.to_string());
            }
            void on_RejectTriggerExpiredLimitOrderEvent(const ModelEvents::RejectTriggerExpiredLimitOrderEvent& event) override {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "ZIMM ignoring received (internal) RejectTriggerExpiredLimitOrderEvent: " + event.to_string());
            }


        }; // class ZeroIntelligenceMarketMaker

    } // namespace algo
} // namespace trading