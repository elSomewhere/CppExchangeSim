//================
// ExchangeServer.h
//================

#ifndef EXCHANGE_SERVER_H
#define EXCHANGE_SERVER_H

#include "Globals.h"         // Assuming this contains PRICE_TYPE, SIZE_TYPE, ID_TYPE, TIME_TYPE, SIDE, ID_DEFAULT etc.
#include "OrderBookCore.h"   // Your updated OrderBookCore.h with OrderBookWrapper
#include <functional>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <utility> // For std::pair
#include <stdexcept> // For std::runtime_error

// For L2 snapshot data
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

    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_full_cancel_limit;
    std::function<void(ID_TYPE, int, int)> on_full_cancel_limit_reject;

    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, bool, int, int)> on_order_quantity_modified;
    std::function<void(ID_TYPE, const std::string&, int, int)> on_order_quantity_modified_rejected;
    std::function<void(ID_TYPE, PRICE_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_order_price_modified;
    std::function<void(ID_TYPE, const std::string&, int, int)> on_order_price_modified_rejected;
    std::function<void(ID_TYPE, PRICE_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, bool, int, int)> on_order_price_quantity_modified;
    std::function<void(ID_TYPE, const std::string&, int, int)> on_order_price_quantity_modified_rejected;

    std::function<void(ID_TYPE, ID_TYPE, PRICE_TYPE, SIZE_TYPE, bool, int, int, int, int)> on_trade;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_maker_partial_fill_limit;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, int, int)> on_taker_partial_fill_limit; // includes leaves_qty
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_maker_full_fill_limit;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_taker_full_fill_limit;

    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_maker_partial_fill_market;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, int, int)> on_taker_partial_fill_market; // includes leaves_qty
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_maker_full_fill_market;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int)> on_taker_full_fill_market;

    std::function<void(const std::vector<L2_DATA_TYPE>&, const std::vector<L2_DATA_TYPE>&)> on_order_book_snapshot;

    std::function<void(ID_TYPE, int, int)> on_reject_trigger_expiration;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, int, int, TIME_TYPE)> on_acknowledge_trigger_expiration;


    // Order placement methods
    ID_TYPE place_limit_order(SIDE side, PRICE_TYPE price, SIZE_TYPE quantity, TIME_TYPE timeout,
                              int trader_id = 0, int client_order_id = 0) {
        active_taker_metadata_ = std::make_pair(trader_id, client_order_id);
        active_taker_side_ = side;

        auto result_tuple = order_book_.template limit_match_book_price_quantity<DOUBLEOPTION::FRONT, DOUBLEOPTION::BACK>(
                side, price, quantity
        );

        std::optional<std::tuple<ID_TYPE, Price*>> placed_order_info_opt = std::get<0>(result_tuple);
        SIZE_TYPE final_remaining_quantity = std::get<1>(result_tuple);
        std::vector<LOBClearResult>& clearings = std::get<2>(result_tuple);

        ID_TYPE ack_exchange_order_id = ID_DEFAULT; // ID for the taker aspect / ack. If rests, will be actual ID.
        ID_TYPE resting_order_id_if_any = ID_DEFAULT;


        if (placed_order_info_opt) {
            resting_order_id_if_any = std::get<0>(placed_order_info_opt.value());
            ack_exchange_order_id = resting_order_id_if_any; // Use the actual resting ID for the ack
            order_metadata_[resting_order_id_if_any] = {trader_id, client_order_id};
        } else {
            // Order was fully filled or rejected, does not rest. ack_exchange_order_id remains ID_DEFAULT.
            // No metadata stored if not resting.
        }

        if (on_limit_order_acknowledged) {
            on_limit_order_acknowledged(
                    ack_exchange_order_id, // ID_DEFAULT if not resting, actual ID if resting
                    side,
                    price,
                    quantity, // Original quantity
                    final_remaining_quantity,
                    trader_id,
                    client_order_id,
                    timeout
            );
        }

        SIZE_TYPE original_requested_quantity = quantity;
        SIZE_TYPE total_filled_for_taker = 0;
        PRICE_TYPE last_fill_price = price;

        // The ID for the taker in trade/fill events. If the limit order was fully filled and didn't rest,
        // it's ID_DEFAULT. If it rested (even if partially filled first), it's its actual ID.
        ID_TYPE taker_event_id = resting_order_id_if_any != ID_DEFAULT ? resting_order_id_if_any : ID_DEFAULT;


        for (const auto& clearing : clearings) {
            last_fill_price = clearing.price_;
            for (const auto& trade : clearing.trades_) {
                SIZE_TYPE quantity_before_this_segment = original_requested_quantity - total_filled_for_taker;
                SIZE_TYPE leaves_qty_after_this_segment = quantity_before_this_segment - trade.quantity_;

                auto [maker_trader_id, maker_client_id] = _get_trader_client_ids_safe(trade.uoid_maker_);

                if (on_trade) {
                    on_trade(
                            trade.uoid_maker_,
                            taker_event_id,
                            clearing.price_,
                            trade.quantity_,
                            trade.exhausted_,
                            maker_trader_id, maker_client_id,
                            trader_id, client_order_id
                    );
                }

                if (trade.exhausted_) {
                    if (on_maker_full_fill_limit) {
                        on_maker_full_fill_limit(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_trader_id, maker_client_id);
                    }
                } else {
                    if (on_maker_partial_fill_limit) {
                        on_maker_partial_fill_limit(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_trader_id, maker_client_id);
                    }
                }

                if ( (total_filled_for_taker + trade.quantity_) < original_requested_quantity ) {
                    if (on_taker_partial_fill_limit) {
                        on_taker_partial_fill_limit(
                                taker_event_id,
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

        if (total_filled_for_taker == original_requested_quantity && original_requested_quantity > 0) {
            if (on_taker_full_fill_limit) {
                on_taker_full_fill_limit(taker_event_id, last_fill_price, total_filled_for_taker, trader_id, client_order_id);
            }
        }

        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;

        return resting_order_id_if_any; // Return actual ID if rests, else ID_DEFAULT
    }

    ID_TYPE place_market_order(SIDE side, SIZE_TYPE quantity,
                               int trader_id = 0, int client_order_id = 0) {
        ID_TYPE market_order_id = order_book_.generate_new_uoid();
        // Market orders are ephemeral but get an ID for tracking this specific event sequence.
        // Python stores metadata; C++ can too, though it's cleaned up shortly.
        order_metadata_[market_order_id] = {trader_id, client_order_id};

        active_taker_metadata_ = {trader_id, client_order_id};
        active_taker_side_ = side;

        SIZE_TYPE total_filled_for_taker = 0;

        auto result_tuple = order_book_.template market_match_quantity<DOUBLEOPTION::FRONT>(side, quantity);
        SIZE_TYPE remaining_quantity = std::get<0>(result_tuple);
        std::vector<LOBClearResult>& clearings = std::get<1>(result_tuple);

        SIZE_TYPE executed_quantity = quantity - remaining_quantity;

        if (on_market_order_acknowledged) {
            on_market_order_acknowledged(side, quantity, executed_quantity, remaining_quantity, trader_id, client_order_id);
        }

        PRICE_TYPE last_fill_price = PRICE_DEFAULT;

        for (const auto& clearing : clearings) {
            last_fill_price = clearing.price_;
            for (const auto& trade : clearing.trades_) {
                auto [maker_trader_id, maker_client_id] = _get_trader_client_ids_safe(trade.uoid_maker_);

                if (on_trade) {
                    on_trade(
                            trade.uoid_maker_, market_order_id, clearing.price_, trade.quantity_, trade.exhausted_,
                            maker_trader_id, maker_client_id, trader_id, client_order_id
                    );
                }

                if (trade.exhausted_) {
                    if (on_maker_full_fill_market) {
                        on_maker_full_fill_market(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_trader_id, maker_client_id);
                    }
                } else {
                    if (on_maker_partial_fill_market) {
                        on_maker_partial_fill_market(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_trader_id, maker_client_id);
                    }
                }

                SIZE_TYPE post_fill_total = total_filled_for_taker + trade.quantity_;
                SIZE_TYPE leaves_qty_after_this_segment = quantity - post_fill_total;

                if (post_fill_total < quantity) {
                    if (on_taker_partial_fill_market) {
                        on_taker_partial_fill_market(market_order_id, clearing.price_, trade.quantity_, leaves_qty_after_this_segment, trader_id, client_order_id);
                    }
                } else {
                    if (on_taker_full_fill_market) {
                        on_taker_full_fill_market(market_order_id, clearing.price_, trade.quantity_, trader_id, client_order_id);
                    }
                }
                total_filled_for_taker = post_fill_total;
            }
        }

        // Clean up metadata for this ephemeral market order. Python removes if fully filled.
        order_metadata_.erase(market_order_id);


        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;
        return market_order_id;
    }

    // Order management methods
    bool cancel_order(ID_TYPE exchange_order_id, int trader_id_req = 0, int client_order_id_req = 0) {
        int original_trader_id = 0;
        // int original_client_id = 0; // Not used for cancel decision, only for callback if req is 0
        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it != order_metadata_.end()) {
            original_trader_id = meta_it->second.first;
            // original_client_id = meta_it->second.second;
        } // If not in metadata, it might have been filled or already cancelled. delete_limit_order handles non-existence.

        int final_trader_id = (trader_id_req == 0) ? original_trader_id : trader_id_req;

        auto result_opt = order_book_.delete_limit_order(exchange_order_id);

        if (result_opt) {
            auto [price, quantity_cancelled] = result_opt.value();
            order_metadata_.erase(exchange_order_id); // Erase from server's map too
            if (on_full_cancel_limit) {
                on_full_cancel_limit(exchange_order_id, price, quantity_cancelled, final_trader_id, client_order_id_req);
            }
            return true;
        } else {
            if (on_full_cancel_limit_reject) {
                on_full_cancel_limit_reject(exchange_order_id, final_trader_id, client_order_id_req);
            }
            return false;
        }
    }

    bool cancel_expired_order(ID_TYPE exchange_order_id, TIME_TYPE timeout) {
        int original_trader_id = 0;
        int original_client_id = 0;
        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it != order_metadata_.end()) {
            original_trader_id = meta_it->second.first;
            original_client_id = meta_it->second.second;
        }

        auto result_opt = order_book_.delete_limit_order(exchange_order_id);

        if (result_opt) {
            auto [price, quantity_cancelled] = result_opt.value();
            order_metadata_.erase(exchange_order_id);
            if (on_acknowledge_trigger_expiration) {
                on_acknowledge_trigger_expiration(exchange_order_id, price, quantity_cancelled, original_trader_id, original_client_id, timeout);
            }
            return true;
        } else {
            if (on_reject_trigger_expiration) {
                // Following Python definition: (exchange_order_id, trader_id, client_order_id)
                on_reject_trigger_expiration(exchange_order_id, original_trader_id, original_client_id);
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
                on_order_quantity_modified_rejected(exchange_order_id, "quantity", trader_id_req, client_order_id_req);
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
            ID_TYPE final_uoid_in_callback = exchange_order_id;

            if (result.new_uoid) {
                final_uoid_in_callback = result.new_uoid.value();
                order_metadata_.erase(exchange_order_id);
                if (!result.removed) { // if not removed and has new uoid, re-map
                    order_metadata_[final_uoid_in_callback] = {original_trader_id, original_client_id_of_order};
                }
            } else if (result.removed) {
                order_metadata_.erase(exchange_order_id);
            }
            // If no new_uoid and not removed, metadata for exchange_order_id is still valid.

            if (on_order_quantity_modified) {
                on_order_quantity_modified(
                        final_uoid_in_callback,
                        result.price, result.old_volume, result.new_volume, result.removed,
                        final_trader_id_for_callback, final_client_order_id_for_callback
                );
            }
            return true;
        } else {
            if (on_order_quantity_modified_rejected) {
                on_order_quantity_modified_rejected(exchange_order_id, "quantity", final_trader_id_for_callback, final_client_order_id_for_callback);
            }
            return false;
        }
    }

    bool modify_order_price(ID_TYPE exchange_order_id, PRICE_TYPE new_price,
                            int trader_id_req = 0, int client_order_id_req = 0) {
        int original_trader_id = 0;
        int original_client_id_of_order = 0;

        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it == order_metadata_.end()) {
            if (on_order_price_modified_rejected) {
                on_order_price_modified_rejected(exchange_order_id, "price", trader_id_req, client_order_id_req);
            }
            return false;
        }
        original_trader_id = meta_it->second.first;
        original_client_id_of_order = meta_it->second.second;

        int final_trader_id_for_callback = (trader_id_req == 0) ? original_trader_id : trader_id_req;
        int final_client_order_id_for_callback = (client_order_id_req == 0 && trader_id_req == 0) ? original_client_id_of_order : client_order_id_req;

        // Python's ExchangeServer implicitly uses TRIPLEOPTION.INPLACE for price mods.
        std::optional<ModifyPriceResult> result_opt = order_book_.template modify_limit_order_price<TRIPLEOPTION::INPLACE>(
                exchange_order_id, new_price
        );

        if (result_opt) {
            const auto& result = result_opt.value();

            if (result.new_uoid && result.new_uoid.value() != exchange_order_id) {
                // UOID changed, update metadata
                order_metadata_.erase(exchange_order_id);
                order_metadata_[result.new_uoid.value()] = {original_trader_id, original_client_id_of_order};
            } else if (!result.new_uoid) {
                // Order was effectively removed (e.g. original volume was 0, though core asserts against this for resting)
                // Or if new_uoid is std::nullopt, means it was not re-booked.
                order_metadata_.erase(exchange_order_id);
            }
            // If new_uoid is present and same as exchange_order_id, metadata remains.

            if (on_order_price_modified) {
                on_order_price_modified(
                        exchange_order_id, // Original ID that was requested to be modified
                        result.before_price,
                        new_price,
                        result.volume_of_new_order,
                        final_trader_id_for_callback, final_client_order_id_for_callback
                );
            }
            return true;
        } else {
            if (on_order_price_modified_rejected) {
                on_order_price_modified_rejected(exchange_order_id, "price", final_trader_id_for_callback, final_client_order_id_for_callback);
            }
            return false;
        }
    }

    bool modify_order_price_and_quantity(ID_TYPE exchange_order_id, PRICE_TYPE new_price, SIZE_TYPE new_quantity,
                                         int trader_id_req = 0, int client_order_id_req = 0) {
        int original_trader_id = 0;
        int original_client_id_of_order = 0;

        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it == order_metadata_.end()) {
            if (on_order_price_quantity_modified_rejected) {
                on_order_price_quantity_modified_rejected(exchange_order_id, "price_and_quantity", trader_id_req, client_order_id_req);
            }
            return false;
        }
        original_trader_id = meta_it->second.first;
        original_client_id_of_order = meta_it->second.second;

        int final_trader_id_for_callback = (trader_id_req == 0) ? original_trader_id : trader_id_req;
        int final_client_order_id_for_callback = (client_order_id_req == 0 && trader_id_req == 0) ? original_client_id_of_order : client_order_id_req;

        std::optional<ModifyPriceVolResult> result_opt = order_book_.template modify_limit_order_price_vol<TRIPLEOPTION::INPLACE>(
                exchange_order_id, new_price, new_quantity
        );

        if (result_opt) {
            const auto& result = result_opt.value();
            bool order_effectively_removed = !result.new_uoid.has_value();

            if (result.new_uoid && result.new_uoid.value() != exchange_order_id) {
                order_metadata_.erase(exchange_order_id);
                order_metadata_[result.new_uoid.value()] = {original_trader_id, original_client_id_of_order};
            } else if (order_effectively_removed) { // No new UOID means it's gone
                order_metadata_.erase(exchange_order_id);
            }
            // If new_uoid is present and same as exchange_order_id, metadata for exchange_order_id is still valid.

            if (on_order_price_quantity_modified) {
                on_order_price_quantity_modified(
                        exchange_order_id,
                        result.before_price,
                        new_price,
                        result.old_volume,
                        result.new_volume_at_new_price,
                        order_effectively_removed,
                        final_trader_id_for_callback, final_client_order_id_for_callback
                );
            }
            return true;
        } else {
            if (on_order_price_quantity_modified_rejected) {
                on_order_price_quantity_modified_rejected(exchange_order_id, "price_and_quantity", final_trader_id_for_callback, final_client_order_id_for_callback);
            }
            return false;
        }
    }

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
        if (!price_opt) { // Should be consistent if lob_order was found
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
    }

    std::optional<std::pair<int, int>> active_taker_metadata_;
    std::optional<SIDE> active_taker_side_;

private:
    OrderBookWrapper order_book_;
    std::unordered_map<ID_TYPE, std::pair<int, int>> order_metadata_; // {exchange_order_id: {trader_id, client_order_id_of_original_order}}



    std::pair<int, int> _get_trader_client_ids_safe(ID_TYPE exchange_order_id) {
        auto it = order_metadata_.find(exchange_order_id);
        if (it != order_metadata_.end()) {
            return it->second;
        }
        throw std::runtime_error("ExchangeServer: Metadata not found for maker order " + std::to_string(exchange_order_id));
    }
};

#endif // EXCHANGE_SERVER_H