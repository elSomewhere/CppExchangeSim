//================
// ExchangeServer.h
//================

#ifndef EXCHANGE_SERVER_H
#define EXCHANGE_SERVER_H

#include "Globals.h"
#include "OrderBookCore.h"
#include <functional>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <utility>
#include <stdexcept>

typedef PRICE_SIZE_TYPE L2_DATA_TYPE;

class ExchangeServer {
public:
    ExchangeServer() {
        // Callbacks are std::function, default constructed to empty (like Python's None)
    }
    ~ExchangeServer() = default;

    // Callback definitions
    std::function<void(ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, int, int, TIME_TYPE)> on_limit_order_acknowledged;
    std::function<void(SIDE, SIZE_TYPE, SIZE_TYPE, SIZE_TYPE, int, int)> on_market_order_acknowledged;

    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_partial_cancel_limit;
    std::function<void(ID_TYPE, int, int)> on_partial_cancel_limit_reject;

    // MODIFIED: Added SIDE order_side
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, int, int)> on_full_cancel_limit;
    std::function<void(ID_TYPE, int, int)> on_full_cancel_limit_reject;

    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, bool, int, int)> on_order_quantity_modified;
    std::function<void(ID_TYPE, const std::string&, int, int)> on_order_quantity_modified_rejected;
    std::function<void(ID_TYPE, PRICE_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_order_price_modified;
    std::function<void(ID_TYPE, const std::string&, int, int)> on_order_price_modified_rejected;
    std::function<void(ID_TYPE, PRICE_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, bool, int, int)> on_order_price_quantity_modified;
    std::function<void(ID_TYPE, const std::string&, int, int)> on_order_price_quantity_modified_rejected;

    // MODIFIED: Added SIDE maker_side, SIDE taker_side
    std::function<void(ID_TYPE, SIDE, ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, bool, int, int, int, int)> on_trade;

    // MODIFIED: Added SIDE maker_order_side
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, int, int)> on_maker_partial_fill_limit;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, int, int)> on_taker_partial_fill_limit; // includes leaves_qty

    // MODIFIED: Added SIDE maker_order_side
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, int, int)> on_maker_full_fill_limit;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_taker_full_fill_limit;

    // MODIFIED: Added SIDE maker_order_side
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, int, int)> on_maker_partial_fill_market;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, int, int)> on_taker_partial_fill_market; // includes leaves_qty

    // MODIFIED: Added SIDE maker_order_side
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, int, int)> on_maker_full_fill_market;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_taker_full_fill_market;

    std::function<void(const std::vector<L2_DATA_TYPE>&, const std::vector<L2_DATA_TYPE>&)> on_order_book_snapshot;

    // MODIFIED: Added TIME_TYPE original_timeout_us_rep
    std::function<void(ID_TYPE, int, int, TIME_TYPE)> on_reject_trigger_expiration;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int, TIME_TYPE)> on_acknowledge_trigger_expiration;


    // Order placement methods
    ID_TYPE place_limit_order(SIDE side, PRICE_TYPE price, SIZE_TYPE quantity, TIME_TYPE timeout_us_rep,
                              int trader_id = 0, int client_order_id = 0) {
        active_taker_metadata_ = std::make_pair(trader_id, client_order_id);
        active_taker_side_ = side;

        auto result_tuple = order_book_.template limit_match_book_price_quantity<DOUBLEOPTION::FRONT, DOUBLEOPTION::BACK>(
                side, price, quantity
        );

        std::optional<std::tuple<ID_TYPE, Price*>> placed_order_info_opt = std::get<0>(result_tuple);
        SIZE_TYPE final_remaining_quantity = std::get<1>(result_tuple);
        std::vector<LOBClearResult>& clearings = std::get<2>(result_tuple);

        ID_TYPE ack_exchange_order_id = ID_DEFAULT;
        ID_TYPE resting_order_id_if_any = ID_DEFAULT;


        if (placed_order_info_opt) {
            resting_order_id_if_any = std::get<0>(placed_order_info_opt.value());
            ack_exchange_order_id = resting_order_id_if_any;
            order_metadata_[resting_order_id_if_any] = {trader_id, client_order_id};
        }

        if (on_limit_order_acknowledged) {
            on_limit_order_acknowledged(
                    ack_exchange_order_id,
                    side,
                    price,
                    quantity,
                    final_remaining_quantity,
                    trader_id,
                    client_order_id,
                    timeout_us_rep
            );
        }

        SIZE_TYPE original_requested_quantity = quantity;
        SIZE_TYPE total_filled_for_taker = 0;
        PRICE_TYPE last_fill_price = price; // Default to order price if no fills

        // If limit order acts as taker, it needs an ID for fill events.
        // If it rests, use its resting_order_id_if_any.
        // If it's fully filled as taker and doesn't rest, use a temporary ID or ID_DEFAULT.
        ID_TYPE taker_event_id_for_fills = (resting_order_id_if_any != ID_DEFAULT) ? resting_order_id_if_any : market_order_id_counter_++;


        for (const auto& clearing : clearings) {
            last_fill_price = clearing.price_; // Update last_fill_price with actual fill price
            for (const auto& trade : clearing.trades_) {
                SIZE_TYPE quantity_before_this_segment = original_requested_quantity - total_filled_for_taker;
                SIZE_TYPE leaves_qty_after_this_segment = quantity_before_this_segment - trade.quantity_;

                auto [maker_trader_id, maker_client_id] = _get_trader_client_ids_safe(trade.uoid_maker_);
                SIDE maker_actual_side = order_book_.get_order_side(trade.uoid_maker_).value_or(SIDE::NONE); // Maker side
                SIDE taker_actual_side = side; // Side of the incoming limit order

                if (on_trade) {
                    on_trade(
                            trade.uoid_maker_, maker_actual_side,
                            taker_event_id_for_fills, taker_actual_side,
                            clearing.price_,
                            trade.quantity_,
                            trade.exhausted_,
                            maker_trader_id, maker_client_id,
                            trader_id, client_order_id
                    );
                }

                if (trade.exhausted_) {
                    if (on_maker_full_fill_limit) {
                        on_maker_full_fill_limit(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                    _remove_order_metadata_if_exists(trade.uoid_maker_); // Clean up fully filled maker
                } else {
                    if (on_maker_partial_fill_limit) {
                        on_maker_partial_fill_limit(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                }

                if ( (total_filled_for_taker + trade.quantity_) < original_requested_quantity ) {
                    if (on_taker_partial_fill_limit) {
                        on_taker_partial_fill_limit(
                                taker_event_id_for_fills,
                                clearing.price_,
                                trade.quantity_,
                                leaves_qty_after_this_segment,
                                trader_id, client_order_id
                        );
                    }
                }
                total_filled_for_taker += trade.quantity_;
            }
        }

        if (total_filled_for_taker > 0 && total_filled_for_taker == original_requested_quantity) { // Ensure some fills occurred
            if (on_taker_full_fill_limit) {
                on_taker_full_fill_limit(taker_event_id_for_fills, last_fill_price, total_filled_for_taker, trader_id, client_order_id);
            }
            if (taker_event_id_for_fills >= market_order_id_counter_start_value_) { // Clean up temporary ID
                _remove_order_metadata_if_exists(taker_event_id_for_fills);
            }
        }


        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;

        return resting_order_id_if_any;
    }

    ID_TYPE place_market_order(SIDE side, SIZE_TYPE quantity,
                               int trader_id = 0, int client_order_id = 0) {
        ID_TYPE market_order_id = market_order_id_counter_++;
        order_metadata_[market_order_id] = {trader_id, client_order_id};

        active_taker_metadata_ = {trader_id, client_order_id};
        active_taker_side_ = side;

        SIZE_TYPE total_filled_for_taker = 0;

        auto result_tuple = order_book_.template market_match_quantity<DOUBLEOPTION::FRONT>(side, quantity);
        SIZE_TYPE remaining_quantity_on_market_order = std::get<0>(result_tuple);
        std::vector<LOBClearResult>& clearings = std::get<1>(result_tuple);

        SIZE_TYPE executed_quantity = quantity - remaining_quantity_on_market_order;

        if (on_market_order_acknowledged) {
            on_market_order_acknowledged(side, quantity, executed_quantity, remaining_quantity_on_market_order, trader_id, client_order_id);
        }

        PRICE_TYPE last_fill_price = PRICE_DEFAULT;

        for (const auto& clearing : clearings) {
            last_fill_price = clearing.price_;
            for (const auto& trade : clearing.trades_) {
                auto [maker_trader_id, maker_client_id] = _get_trader_client_ids_safe(trade.uoid_maker_);
                SIDE maker_actual_side = order_book_.get_order_side(trade.uoid_maker_).value_or(SIDE::NONE);
                SIDE taker_actual_side = side;

                if (on_trade) {
                    on_trade(
                            trade.uoid_maker_, maker_actual_side,
                            market_order_id, taker_actual_side,
                            clearing.price_, trade.quantity_, trade.exhausted_,
                            maker_trader_id, maker_client_id, trader_id, client_order_id
                    );
                }

                if (trade.exhausted_) {
                    if (on_maker_full_fill_market) {
                        on_maker_full_fill_market(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                    _remove_order_metadata_if_exists(trade.uoid_maker_); // Clean up fully filled maker
                } else {
                    if (on_maker_partial_fill_market) {
                        on_maker_partial_fill_market(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                }

                SIZE_TYPE post_fill_total = total_filled_for_taker + trade.quantity_;
                SIZE_TYPE leaves_qty_after_this_segment = quantity - post_fill_total;

                if (post_fill_total < quantity) {
                    if (on_taker_partial_fill_market) {
                        on_taker_partial_fill_market(market_order_id, clearing.price_, trade.quantity_, leaves_qty_after_this_segment, trader_id, client_order_id);
                    }
                }
                total_filled_for_taker = post_fill_total; // Accumulate before check for full fill
            }
        }

        // Check for full fill of market order after all clearings
        if (total_filled_for_taker > 0 && total_filled_for_taker == quantity) { // Market order is fully filled
            if (on_taker_full_fill_market && last_fill_price != PRICE_DEFAULT) { // Ensure there was at least one fill
                on_taker_full_fill_market(market_order_id, last_fill_price, total_filled_for_taker, trader_id, client_order_id);
            }
        }


        // Clean up metadata for this market order as it's terminal (either filled or no liquidity)
        order_metadata_.erase(market_order_id);


        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;
        return market_order_id;
    }

    // Order management methods
    bool cancel_order(ID_TYPE exchange_order_id, int trader_id_req = 0, int client_order_id_req = 0) {
        int original_trader_id = 0;
        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it != order_metadata_.end()) {
            original_trader_id = meta_it->second.first;
        }

        int final_trader_id = (trader_id_req == 0) ? original_trader_id : trader_id_req;

        std::optional<SIDE> order_side_opt = order_book_.get_order_side(exchange_order_id);
        if (!order_side_opt) {
            if (on_full_cancel_limit_reject) {
                on_full_cancel_limit_reject(exchange_order_id, final_trader_id, client_order_id_req);
            }
            return false;
        }
        SIDE order_side = order_side_opt.value();

        auto result_opt = order_book_.delete_limit_order(exchange_order_id);

        if (result_opt) {
            auto [price, quantity_cancelled] = result_opt.value();
            order_metadata_.erase(exchange_order_id);
            if (on_full_cancel_limit) {
                on_full_cancel_limit(exchange_order_id, price, quantity_cancelled, order_side, final_trader_id, client_order_id_req);
            }
            return true;
        } else {
            if (on_full_cancel_limit_reject) {
                on_full_cancel_limit_reject(exchange_order_id, final_trader_id, client_order_id_req);
            }
            return false;
        }
    }

    bool cancel_expired_order(ID_TYPE exchange_order_id, TIME_TYPE timeout_us_rep) {
        int original_trader_id = 0;
        int original_client_id = 0;
        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it != order_metadata_.end()) {
            original_trader_id = meta_it->second.first;
            original_client_id = meta_it->second.second;
        }
        // else: if not in metadata, order might have already been processed/removed.
        // delete_limit_order will handle non-existence.

        auto result_opt = order_book_.delete_limit_order(exchange_order_id);

        if (result_opt) {
            auto [price, quantity_cancelled] = result_opt.value();
            order_metadata_.erase(exchange_order_id); // Also remove from internal tracking
            if (on_acknowledge_trigger_expiration) {
                on_acknowledge_trigger_expiration(exchange_order_id, price, quantity_cancelled, original_trader_id, original_client_id, timeout_us_rep);
            }
            return true;
        } else {
            if (on_reject_trigger_expiration) {
                // Pass the original timeout_us_rep to the reject callback
                on_reject_trigger_expiration(exchange_order_id, original_trader_id, original_client_id, timeout_us_rep);
            }
            return false;
        }
    }

    bool modify_order_quantity(ID_TYPE exchange_order_id, SIZE_TYPE new_quantity,
                               int trader_id_req = 0, int client_order_id_req = 0) {
        int original_trader_id = 0;
        int original_client_id_of_order = 0;

        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it == order_metadata_.end()) {
            if (on_order_quantity_modified_rejected) {
                on_order_quantity_modified_rejected(exchange_order_id, "quantity: order not found", trader_id_req, client_order_id_req);
            }
            return false;
        }
        original_trader_id = meta_it->second.first;
        original_client_id_of_order = meta_it->second.second;

        int final_trader_id_for_callback = (trader_id_req == 0) ? original_trader_id : trader_id_req;
        int final_client_order_id_for_callback = (client_order_id_req == 0 && trader_id_req == 0) ? original_client_id_of_order : client_order_id_req;

        std::optional<ModifyVolResult> result_opt = order_book_.template modify_limit_order_vol<TRIPLEOPTION::INPLACE>(
                exchange_order_id, new_quantity
        );

        if (result_opt) {
            const auto& result = result_opt.value();
            ID_TYPE final_uoid_in_callback = result.new_uoid.value_or(exchange_order_id);


            if (result.new_uoid && result.new_uoid.value() != exchange_order_id) {
                order_metadata_.erase(exchange_order_id);
                if (!result.removed) {
                    order_metadata_[final_uoid_in_callback] = {original_trader_id, original_client_id_of_order};
                }
            } else if (result.removed) {
                order_metadata_.erase(exchange_order_id);
            }

            if (on_order_quantity_modified) {
                on_order_quantity_modified(
                        final_uoid_in_callback,
                        result.price, result.old_volume, result.new_volume, result.removed,
                        final_trader_id_for_callback, final_client_order_id_for_callback
                );
            }
            // Check if callback for partial cancel should be called
            if (result.new_volume < result.old_volume && !result.removed) { // Quantity reduced
                if (on_partial_cancel_limit) {
                    SIZE_TYPE cancelled_qty = result.old_volume - result.new_volume;
                    // The client_order_id_req for on_partial_cancel_limit is the ID of the cancel request
                    // The modify_order_quantity doesn't have a "cancel request ID".
                    // Assuming client_order_id_req of modify is used.
                    on_partial_cancel_limit(final_uoid_in_callback, result.price, cancelled_qty, final_trader_id_for_callback, final_client_order_id_for_callback);
                }
            }
            return true;
        } else {
            if (on_order_quantity_modified_rejected) {
                on_order_quantity_modified_rejected(exchange_order_id, "quantity: core modification failed", final_trader_id_for_callback, final_client_order_id_for_callback);
            }
            return false;
        }
    }

    // ... (modify_order_price and modify_order_price_and_quantity can be similarly updated if needed,
    //      though their primary impact is on price/priority, not usually direct side info for the event itself) ...

    // Query methods
    std::pair<std::vector<L2_DATA_TYPE>, std::vector<L2_DATA_TYPE>> get_order_book_snapshot() {
        auto snapshot = order_book_.get_state_l2();
        if (on_order_book_snapshot) {
            on_order_book_snapshot(snapshot.first, snapshot.second);
        }
        return snapshot;
    }

    std::optional<std::tuple<PRICE_TYPE, SIZE_TYPE, SIDE>> get_order_details(ID_TYPE exchange_order_id) {
        auto side_opt = order_book_.get_order_side(exchange_order_id);
        if (!side_opt) {
            return std::nullopt;
        }
        SIDE side = side_opt.value();

        const LOBOrder* lob_order = order_book_.get_lob_order(exchange_order_id);
        if (!lob_order) {
            return std::nullopt;
        }

        std::optional<PRICE_TYPE> price_opt = order_book_.get_price_for_order(exchange_order_id);
        if (!price_opt) {
            return std::nullopt;
        }

        return std::make_tuple(price_opt.value(), lob_order->quantity_, side);
    }

    std::optional<std::pair<int, int>> get_order_metadata(ID_TYPE exchange_order_id) {
        auto it = order_metadata_.find(exchange_order_id);
        if (it != order_metadata_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    size_t get_order_count() {
        return order_book_.get_num_orders();
    }

    // Utility
    void flush() {
        order_book_.flush();
        order_metadata_.clear();
        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;
        market_order_id_counter_ = market_order_id_counter_start_value_;
    }


public: // Public for EventModelExchangeAdapter to access or for testing
    std::optional<std::pair<int, int>> active_taker_metadata_;
    std::optional<SIDE> active_taker_side_;

private:
    OrderBookWrapper order_book_;
    std::unordered_map<ID_TYPE, std::pair<int, int>> order_metadata_; // {exchange_order_id: {trader_id, client_order_id_of_original_order}}

    static constexpr ID_TYPE market_order_id_counter_start_value_ = 1000000000; // Ensures distinct range
    ID_TYPE market_order_id_counter_ = market_order_id_counter_start_value_;


    std::pair<int, int> _get_trader_client_ids_safe(ID_TYPE exchange_order_id) {
        auto it = order_metadata_.find(exchange_order_id);
        if (it != order_metadata_.end()) {
            return it->second;
        }
        // This indicates a logic error if a maker order involved in a trade doesn't have metadata
        // For robustness, one might return default IDs or handle this more gracefully.
        // Keeping throw for now as it was in the original, but this should be reviewed for production.
        throw std::runtime_error("ExchangeServer: Metadata not found for maker order " + std::to_string(exchange_order_id) + " involved in a trade.");
    }

    void _remove_order_metadata_if_exists(ID_TYPE exchange_order_id) {
        order_metadata_.erase(exchange_order_id);
    }
};

#endif // EXCHANGE_SERVER_H