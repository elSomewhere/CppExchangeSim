// file: src/ExchangeServer.h
#ifndef EXCHANGE_SERVER_H
#define EXCHANGE_SERVER_H

#include "Globals.h"
#include "OrderBookCore.h"
#include "Model.h"

#include <functional>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <utility> // For std::pair
#include <stdexcept> // For std::runtime_error
#include <algorithm> // For std::min


typedef PRICE_SIZE_TYPE L2_DATA_TYPE; // From Globals.h

using AgentId = EventBusSystem::AgentId; // From Model.h -> EventBus.h
using ClientOrderIdType = ModelEvents::ClientOrderIdType; // From Model.h


class ExchangeServer {
public:
    ExchangeServer() {
        // Callbacks are std::function, default constructed to empty.
    }
    ~ExchangeServer() = default;

    // Callback Signatures (no change to signatures themselves, only internal logic might be affected by renaming counter)
    std::function<void(ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType, TIME_TYPE)> on_limit_order_acknowledged;
    std::function<void(SIDE, SIZE_TYPE, SIZE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType)> on_market_order_acknowledged;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType)> on_partial_cancel_limit;
    std::function<void(ID_TYPE, AgentId, ClientOrderIdType)> on_partial_cancel_limit_reject;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, AgentId, ClientOrderIdType)> on_full_cancel_limit;
    std::function<void(ID_TYPE, AgentId, ClientOrderIdType)> on_full_cancel_limit_reject;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, bool, AgentId, ClientOrderIdType)> on_order_quantity_modified; // existing, used by modify_order_quantity
    std::function<void(ID_TYPE, const std::string&, AgentId, ClientOrderIdType)> on_order_quantity_modified_rejected;  // existing

    // ... (other modification callbacks, if ever implemented with public methods) ...

    std::function<void(ID_TYPE, SIDE, ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, bool, AgentId, ClientOrderIdType, AgentId, ClientOrderIdType)> on_trade;

    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, AgentId, ClientOrderIdType)> on_maker_partial_fill_limit;
    std::function<void(ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType)> on_taker_partial_fill_limit;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, AgentId, ClientOrderIdType)> on_maker_full_fill_limit;
    std::function<void(ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType)> on_taker_full_fill_limit;

    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, AgentId, ClientOrderIdType)> on_maker_partial_fill_market;
    std::function<void(ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType)> on_taker_partial_fill_market;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, SIDE, AgentId, ClientOrderIdType)> on_maker_full_fill_market;
    std::function<void(ID_TYPE, SIDE, PRICE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType)> on_taker_full_fill_market;

    std::function<void(const std::vector<L2_DATA_TYPE>&, const std::vector<L2_DATA_TYPE>&)> on_order_book_snapshot;
    std::function<void(ID_TYPE, AgentId, ClientOrderIdType, TIME_TYPE)> on_reject_trigger_expiration;
    std::function<void(ID_TYPE, PRICE_TYPE, SIZE_TYPE, AgentId, ClientOrderIdType, TIME_TYPE)> on_acknowledge_trigger_expiration;


    ID_TYPE place_limit_order(SIDE side, PRICE_TYPE price, SIZE_TYPE quantity, TIME_TYPE timeout_us_rep,
                              AgentId trader_id = AgentId(0), ClientOrderIdType client_order_id = ClientOrderIdType(0)) {
        active_taker_metadata_ = std::make_pair(trader_id, client_order_id);
        active_taker_side_ = side;

        // Attempt to match against the book
        auto result_tuple = order_book_.template limit_match_book_price_quantity<DOUBLEOPTION::FRONT, DOUBLEOPTION::BACK>(
                side, price, quantity
        );

        std::optional<std::tuple<ID_TYPE, Price*>> placed_order_info_opt = std::get<0>(result_tuple);
        SIZE_TYPE final_remaining_quantity_on_order = std::get<1>(result_tuple); // Quantity that rested or would rest
        std::vector<LOBClearResult>& clearings = std::get<2>(result_tuple);

        ID_TYPE ack_exchange_order_id_for_callback = ID_DEFAULT; // ID to be reported in LimitOrderAckEvent
        ID_TYPE resting_order_id_if_any = ID_DEFAULT; // Actual ID if any part of the order rests

        if (placed_order_info_opt) {
            resting_order_id_if_any = std::get<0>(placed_order_info_opt.value());
            ack_exchange_order_id_for_callback = resting_order_id_if_any;
            order_metadata_[resting_order_id_if_any] = {trader_id, client_order_id}; // Map the resting part
        }
        // If placed_order_info_opt is nullopt, it means the order was fully filled as a taker and nothing rested.
        // In this case, ack_exchange_order_id_for_callback remains ID_DEFAULT.

        if (on_limit_order_acknowledged) {
            on_limit_order_acknowledged(
                    ack_exchange_order_id_for_callback, // This will be ID_DEFAULT if nothing rested
                    side,
                    price,
                    quantity, // Original requested quantity
                    final_remaining_quantity_on_order, // Quantity that actually rested (0 if fully aggressive)
                    trader_id,
                    client_order_id,
                    timeout_us_rep
            );
        }

        SIZE_TYPE original_requested_quantity = quantity;
        SIZE_TYPE total_filled_for_taker = 0;
        PRICE_TYPE last_fill_price = price; // Default to order price, updated by actual fills

        // --- Transient ID for Taker Fills ---
        // If the order was purely aggressive (nothing rested, resting_order_id_if_any == ID_DEFAULT),
        // or if it partially rested but also had aggressive fills, we need an ID for the taker side of those fills.
        // If it rested, its resting_order_id_if_any can serve this purpose for fills related to it.
        // If it was purely aggressive, a new transient ID is generated.
        ID_TYPE taker_event_id_for_fills;
        if (resting_order_id_if_any != ID_DEFAULT) {
            taker_event_id_for_fills = resting_order_id_if_any;
        } else {
            // Order was fully aggressive, generate a transient ID for its fills.
            taker_event_id_for_fills = transient_order_id_counter_++;
            order_metadata_[taker_event_id_for_fills] = {trader_id, client_order_id}; // Map this transient ID
        }
        // --- End Transient ID for Taker Fills ---

        for (const auto& clearing : clearings) { // These are fills against resting orders
            last_fill_price = clearing.price_;
            for (const auto& trade : clearing.trades_) { // trade.uoid_maker_ is a resting order
                auto [maker_trader_id, maker_client_id] = _get_trader_client_ids_safe(trade.uoid_maker_);
                SIDE maker_actual_side = order_book_.get_order_side(trade.uoid_maker_).value_or(SIDE::NONE);
                SIDE taker_actual_side = side; // The side of the incoming limit order

                if (on_trade) {
                    on_trade(
                            trade.uoid_maker_, maker_actual_side,
                            taker_event_id_for_fills, taker_actual_side, // Use the determined taker_event_id
                            clearing.price_,
                            trade.quantity_,
                            trade.exhausted_, // maker_exhausted
                            maker_trader_id, maker_client_id,
                            trader_id, client_order_id // Taker's original IDs
                    );
                }

                // Maker side fill callbacks
                if (trade.exhausted_) {
                    if (on_maker_full_fill_limit) {
                        on_maker_full_fill_limit(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                    _remove_order_metadata_if_exists(trade.uoid_maker_);
                } else {
                    if (on_maker_partial_fill_limit) {
                        on_maker_partial_fill_limit(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                }

                SIZE_TYPE new_total_filled_for_taker = total_filled_for_taker + trade.quantity_;
                SIZE_TYPE leaves_qty_on_taker_after_this_segment = std::max((SIZE_TYPE)0, original_requested_quantity - new_total_filled_for_taker);

                // Taker side fill callbacks (for the incoming limit order acting as taker)
                if (new_total_filled_for_taker < original_requested_quantity) { // Still more to fill for the taker
                    if (on_taker_partial_fill_limit) {
                        on_taker_partial_fill_limit(
                                taker_event_id_for_fills,
                                side, // Taker's side
                                clearing.price_,
                                trade.quantity_, // Quantity filled in this segment for the taker
                                leaves_qty_on_taker_after_this_segment,
                                trader_id, client_order_id
                        );
                    }
                }
                total_filled_for_taker = new_total_filled_for_taker;
            }
        }

        if (total_filled_for_taker > 0 && total_filled_for_taker >= original_requested_quantity) { // Taker order fully filled
            if (on_taker_full_fill_limit) {
                // Report original_requested_quantity as filled if it met or exceeded request.
                // This handles cases where book structure might offer more than requested at a price.
                SIZE_TYPE reported_filled_qty_for_taker = original_requested_quantity;
                on_taker_full_fill_limit(taker_event_id_for_fills, side, last_fill_price, reported_filled_qty_for_taker, trader_id, client_order_id);
            }
        }

        // Cleanup metadata for transient ID if it was used and the order is now fully resolved
        // (either fully filled as taker, or any resting part is also gone).
        if (taker_event_id_for_fills >= TRANSIENT_ORDER_ID_COUNTER_START_VALUE_ && resting_order_id_if_any == ID_DEFAULT) {
            // If it was a purely aggressive limit order using a transient ID, and it's fully processed.
             if (total_filled_for_taker >= original_requested_quantity) {
                _remove_order_metadata_if_exists(taker_event_id_for_fills);
             }
        }
        // If `resting_order_id_if_any` was used for `taker_event_id_for_fills`, its metadata is removed
        // when the resting part itself is fully filled or cancelled via other callbacks.

        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;

        return resting_order_id_if_any; // Return the ID if it rested, ID_DEFAULT otherwise
    }

    ID_TYPE place_market_order(SIDE side, SIZE_TYPE quantity,
                               AgentId trader_id = AgentId(0), ClientOrderIdType client_order_id = ClientOrderIdType(0)) {
        // Market orders always get a transient ID.
        ID_TYPE market_order_transient_id = transient_order_id_counter_++;
        order_metadata_[market_order_transient_id] = {trader_id, client_order_id};

        active_taker_metadata_ = {trader_id, client_order_id};
        active_taker_side_ = side;

        SIZE_TYPE total_filled_for_taker = 0;

        auto result_tuple = order_book_.template market_match_quantity<DOUBLEOPTION::FRONT>(side, quantity);
        SIZE_TYPE remaining_quantity_on_market_order = std::get<0>(result_tuple); // Unfilled part of market order
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
                            market_order_transient_id, taker_actual_side,
                            clearing.price_, trade.quantity_, trade.exhausted_, // maker_exhausted
                            maker_trader_id, maker_client_id, trader_id, client_order_id
                    );
                }

                // Maker side (resting order) fill callbacks
                if (trade.exhausted_) {
                    // Note: ExchangeServer has on_maker_full_fill_market, implies a resting order (limit) was hit by this market order
                    if (on_maker_full_fill_market) { // Semantically, maker is usually limit, so _market suffix might be confusing
                        on_maker_full_fill_market(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                    _remove_order_metadata_if_exists(trade.uoid_maker_);
                } else {
                    if (on_maker_partial_fill_market) {
                        on_maker_partial_fill_market(trade.uoid_maker_, clearing.price_, trade.quantity_, maker_actual_side, maker_trader_id, maker_client_id);
                    }
                }

                SIZE_TYPE new_total_filled_for_taker = total_filled_for_taker + trade.quantity_;
                SIZE_TYPE leaves_qty_on_taker_after_this_segment = std::max((SIZE_TYPE)0, quantity - new_total_filled_for_taker);

                // Taker side (this market order) fill callbacks
                if (new_total_filled_for_taker < quantity) { // Market order still partially filled
                    if (on_taker_partial_fill_market) {
                        on_taker_partial_fill_market(market_order_transient_id, side, clearing.price_, trade.quantity_, leaves_qty_on_taker_after_this_segment, trader_id, client_order_id);
                    }
                }
                total_filled_for_taker = new_total_filled_for_taker;
            }
        }

        if (total_filled_for_taker > 0 && total_filled_for_taker >= quantity) { // Market order fully filled
            if (on_taker_full_fill_market && last_fill_price != PRICE_DEFAULT) { // PRICE_DEFAULT check ensures at least one fill happened
                on_taker_full_fill_market(market_order_transient_id, side, last_fill_price, quantity, trader_id, client_order_id);
            }
        }

        // Market order is fully processed (either filled or unfillable part remains after ack).
        // Metadata for its transient ID is removed here or upon ack processing by adapter.
        // If MarketOrderAck means it's terminal, then adapter removes. If fills complete it, then here.
        // Let's assume if it's fully filled (total_filled_for_taker >= quantity) OR if there's an unfillable part (remaining_quantity_on_market_order > 0 from ack),
        // its lifecycle for fills is over.
        if (total_filled_for_taker >= quantity || remaining_quantity_on_market_order > 0) {
            _remove_order_metadata_if_exists(market_order_transient_id);
        }

        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;
        return market_order_transient_id; // Return the transient ID used for this market order
    }

    bool cancel_order(ID_TYPE exchange_order_id, AgentId trader_id_req = AgentId(0), ClientOrderIdType client_order_id_req = ClientOrderIdType(0)) {
        AgentId original_trader_id_of_order = AgentId(0); // Default if not found in metadata
        // ClientOrderIdType original_client_id_of_order = ClientOrderIdType(0); // Not strictly needed here

        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it != order_metadata_.end()) {
            original_trader_id_of_order = meta_it->second.first;
            // original_client_id_of_order = meta_it->second.second;
        } else {
            // If no metadata, it might be a transient ID that was already cleaned up, or an invalid ID.
            // The cancel will likely fail in OrderBookCore if the ID is not there.
        }

        // The callback should receive the IDs of the *cancel request*, not necessarily the original order's owner.
        AgentId final_trader_id_for_cb = trader_id_req;
        ClientOrderIdType final_client_id_for_cb = client_order_id_req;

        std::optional<SIDE> order_side_opt = order_book_.get_order_side(exchange_order_id);
        if (!order_side_opt) {
            if (on_full_cancel_limit_reject) {
                on_full_cancel_limit_reject(exchange_order_id, final_trader_id_for_cb, final_client_id_for_cb);
            }
            return false; // Indicate call failed to find order to cancel
        }
        SIDE order_side = order_side_opt.value();

        auto result_opt = order_book_.delete_limit_order(exchange_order_id);

        if (result_opt) {
            auto [price, quantity_cancelled] = result_opt.value();
            order_metadata_.erase(exchange_order_id); // Clean up metadata for the cancelled order
            if (on_full_cancel_limit) {
                on_full_cancel_limit(exchange_order_id, price, quantity_cancelled, order_side, final_trader_id_for_cb, final_client_id_for_cb);
            }
            return true; // Indicate cancel was successful at core level
        } else {
            if (on_full_cancel_limit_reject) {
                on_full_cancel_limit_reject(exchange_order_id, final_trader_id_for_cb, final_client_id_for_cb);
            }
            return false; // Indicate cancel failed at core level
        }
    }

    bool cancel_expired_order(ID_TYPE exchange_order_id, TIME_TYPE timeout_us_rep) {
        AgentId original_trader_id = AgentId(0);
        ClientOrderIdType original_client_id = ClientOrderIdType(0);
        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it != order_metadata_.end()) {
            original_trader_id = meta_it->second.first;
            original_client_id = meta_it->second.second;
        } else {
            // Order metadata not found, it might have been filled/cancelled already.
            // The expiration trigger is "late".
            if (on_reject_trigger_expiration) {
                on_reject_trigger_expiration(exchange_order_id, original_trader_id, original_client_id, timeout_us_rep);
            }
            return false; // Indicate call cannot proceed as order metadata is missing
        }

        auto result_opt = order_book_.delete_limit_order(exchange_order_id);

        if (result_opt) {
            auto [price, quantity_cancelled] = result_opt.value();
            order_metadata_.erase(exchange_order_id); // Clean up metadata
            if (on_acknowledge_trigger_expiration) {
                on_acknowledge_trigger_expiration(exchange_order_id, price, quantity_cancelled, original_trader_id, original_client_id, timeout_us_rep);
            }
            return true;
        } else {
            // Order not found in book (e.g. filled just before expiration)
            if (on_reject_trigger_expiration) {
                on_reject_trigger_expiration(exchange_order_id, original_trader_id, original_client_id, timeout_us_rep);
            }
            return false;
        }
    }

    bool modify_order_quantity(ID_TYPE exchange_order_id, SIZE_TYPE new_quantity,
                               AgentId trader_id_req = AgentId(0), ClientOrderIdType client_order_id_req = ClientOrderIdType(0)) {
        AgentId original_trader_id_of_order = AgentId(0);
        ClientOrderIdType original_client_id_of_order = ClientOrderIdType(0);

        auto meta_it = order_metadata_.find(exchange_order_id);
        if (meta_it == order_metadata_.end()) {
            if (on_order_quantity_modified_rejected) {
                on_order_quantity_modified_rejected(exchange_order_id, "quantity: order not found in metadata", trader_id_req, client_order_id_req);
            }
            return false;
        }
        original_trader_id_of_order = meta_it->second.first;
        original_client_id_of_order = meta_it->second.second;

        // Callbacks should use the request's IDs
        AgentId final_trader_id_for_cb = trader_id_req;
        ClientOrderIdType final_client_order_id_for_cb = client_order_id_req;

        std::optional<ModifyVolResult> result_opt = order_book_.template modify_limit_order_vol<TRIPLEOPTION::INPLACE>(
                exchange_order_id, new_quantity
        );

        if (result_opt) {
            const auto& result = result_opt.value();
            ID_TYPE final_uoid_after_modify = result.new_uoid.value_or(exchange_order_id); // UOID might change if not INPLACE and not same price

            // Update metadata if UOID changed or order removed
            if (result.new_uoid && result.new_uoid.value() != exchange_order_id) {
                order_metadata_.erase(exchange_order_id);
                if (!result.removed) {
                    order_metadata_[final_uoid_after_modify] = {original_trader_id_of_order, original_client_id_of_order};
                }
            } else if (result.removed) {
                order_metadata_.erase(exchange_order_id);
            }

            // Generic quantity modification ack (if defined and used)
            if (on_order_quantity_modified) {
                on_order_quantity_modified(
                        final_uoid_after_modify, // Use the potentially new UOID
                        result.price, result.old_volume, result.new_volume, result.removed,
                        final_trader_id_for_cb, final_client_order_id_for_cb
                );
            }

            // Specific "partial cancel" ack if quantity was reduced but order not removed
            if (result.new_volume < result.old_volume && !result.removed) {
                if (on_partial_cancel_limit) {
                    SIZE_TYPE cancelled_qty = result.old_volume - result.new_volume;
                    on_partial_cancel_limit(final_uoid_after_modify, result.price, cancelled_qty, final_trader_id_for_cb, final_client_order_id_for_cb);
                }
            }
            // If result.removed is true (e.g. new_quantity was 0), on_full_cancel_limit should be triggered by the cancel_order path.
            // modify_order_quantity to 0 should behave like a cancel.
            // OrderBookCore's modify_limit_order_vol sets result.removed=true if new_volume is 0.
            // If modify_order_quantity with new_quantity=0 leads here, and result.removed=true,
            // then on_order_quantity_modified is called. If the adapter wants a FullCancelLimitOrderAckEvent,
            // it needs to interpret this. The current _process_partial_cancel_limit_order in adapter
            // already calls cancel_order if new_qty_target is 0.
            return true;
        } else {
            if (on_order_quantity_modified_rejected) {
                on_order_quantity_modified_rejected(exchange_order_id, "quantity: core modification failed or order not found in book", final_trader_id_for_cb, final_client_order_id_for_cb);
            }
            return false;
        }
    }


    std::pair<std::vector<L2_DATA_TYPE>, std::vector<L2_DATA_TYPE>> get_order_book_snapshot() {
        auto snapshot = order_book_.get_state_l2();
        if (on_order_book_snapshot) {
            on_order_book_snapshot(snapshot.first, snapshot.second);
        }
        return snapshot;
    }

    std::optional<std::tuple<PRICE_TYPE, SIZE_TYPE, SIDE>> get_order_details(ID_TYPE exchange_order_id) {
        // First, ensure the order is known to the exchange server's metadata
        // (primarily for non-transient orders, but good check)
        // auto meta_it = order_metadata_.find(exchange_order_id);
        // if (meta_it == order_metadata_.end()) {
        //     return std::nullopt; // Not a known persistent order or already cleaned up transient
        // }
        // This check might be too restrictive if OrderBookCore can have orders not in ExchangeServer's metadata temporarily.
        // Rely on OrderBookWrapper for details.

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
        if (!price_opt) { // Should not happen if lob_order was found
            return std::nullopt;
        }

        return std::make_tuple(price_opt.value(), lob_order->quantity_, side);
    }

    std::optional<std::pair<AgentId, ClientOrderIdType>> get_order_metadata(ID_TYPE exchange_order_id) {
        auto it = order_metadata_.find(exchange_order_id);
        if (it != order_metadata_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    size_t get_order_count() {
        return order_book_.get_num_orders(); // Orders in the book
        // return order_metadata_.size(); // Orders known to ExchangeServer (includes transient not in book)
    }

    void flush() {
        order_book_.flush();
        order_metadata_.clear();
        active_taker_metadata_ = std::nullopt;
        active_taker_side_ = std::nullopt;
        transient_order_id_counter_ = TRANSIENT_ORDER_ID_COUNTER_START_VALUE_; // Reset the renamed counter
    }

private:
    OrderBookWrapper order_book_;
    // Maps Exchange Order ID to {Trader ID, Client Order ID} of the original order placer
    std::unordered_map<ID_TYPE, std::pair<AgentId, ClientOrderIdType>> order_metadata_;

    // Counter for transient IDs (e.g., for market orders or aggressive fills of limit orders)
    // Start from a high number to distinguish from OrderBookCore's UOIDs if they are ever mixed (they shouldn't be directly).
    static constexpr ID_TYPE TRANSIENT_ORDER_ID_COUNTER_START_VALUE_ = 1000000000; // Renamed
    ID_TYPE transient_order_id_counter_ = TRANSIENT_ORDER_ID_COUNTER_START_VALUE_; // Renamed

    // Temporary state for processing current incoming order
    std::optional<std::pair<AgentId, ClientOrderIdType>> active_taker_metadata_;
    std::optional<SIDE> active_taker_side_;

    // Helper to safely get metadata, throws if not found (internal consistency error)
    std::pair<AgentId, ClientOrderIdType> _get_trader_client_ids_safe(ID_TYPE exchange_order_id) {
        auto it = order_metadata_.find(exchange_order_id);
        if (it != order_metadata_.end()) {
            return it->second;
        }
        // This is a critical internal error if metadata for a participating order is missing.
        throw std::runtime_error("ExchangeServer FATAL: Metadata not found for order XID " + std::to_string(exchange_order_id) + " involved in a trade/fill. System inconsistent.");
    }

    void _remove_order_metadata_if_exists(ID_TYPE exchange_order_id) {
        order_metadata_.erase(exchange_order_id);
    }
};

#endif // EXCHANGE_SERVER_H