//================
// Globals.h
//================

#ifndef ARENA_GLOBALS_H
#define ARENA_GLOBALS_H

#include <climits> // For LLONG_MAX (though consider replacing with specific std::numeric_limits)
#include <cstdint> // For fixed-width integers
#include <chrono>
#include <atomic>  // For atomic UOID generation if placed here (better in OrderBookCore)

enum class UPDATEOPTION{
    REMOVEMISSING,
    KEEPMISSING
};

enum class INPUTEVENTTYPE{
    MarketOrderEvent,
    LimitOrderEvent,
    PartialDeleteOrderEvent,
    FullDeleteOrderEvent,
    ModifyOrderEvent,
    LThreeQueryEvent,
    LTwoQueryEvent,
    RegisterAccountEvent,
    UpdateAccountEvent,
    DeleteAccountEvent,
    QueryAccountEvent,
    QueryAllAccountsEvent,
    UNDEFINED,
};

enum class SUBOUTPUTEVENTTYPE{
    LThreeBookEventSubElement,
    LThreeBookEventElement,
    LTwoBookEventElement,
};

enum class OUTPUTEVENTTYPE {
    MarketOrderEvent_Ack,
    MarketOrderEvent_Rej,

    LimitOrderEvent_Ack,
    LimitOrderEvent_NoPlaceAck,
    LimitOrderEvent_Rej,

    PartialDeleteOrderEvent_Ack,
    PartialDeleteOrderEvent_Rej,

    FullDeleteOrderEvent_Ack,
    FullDeleteOrderEvent_Rej,

    LTwoBookEvent,
    LThreeBookEvent,

    ModifyOrderEvent_Ack,
    ModifyOrderEvent_Rej,

    LimitTradeEvent,
    MarketTradeEvent,
    FillLimitTakerEvent,
    FillMakerEvent,
    FillMarketTakerEvent,

    RegisterAccountEvent_Ack,
    RegisterAccountEvent_Rej,
    UpdateAccountEvent_Ack,
    UpdateAccountEvent_Rej,
    DeleteAccountEvent_Ack,
    DeleteAccountEvent_Rej,
    QueryAccountEvent_Ack,
    QueryAccountEvent_Rej,
    QueryAllAccountsEvent_Ack,

    UNDEFINED,
};

enum class ORDERTYPE { REAL, VIRTUAL, NONE};
enum class SIDE { BID, ASK, NONE }; // Used directly instead of SIDE_TYPE

typedef std::uint64_t ID_TYPE;
typedef std::int64_t PRICE_TYPE;
typedef std::int64_t SIZE_TYPE;
typedef std::int64_t PRICE_SIZE_TYPE; // Assuming this might be price * size, could be large
// SIDE_TYPE bool removed, use enum class SIDE directly

typedef EventBusSystem::LatencyUnit time_res;
typedef time_res::rep TIME_TYPE;

// Consider making this a constexpr if chrono types allow, or ensure it's clear
static const TIME_TYPE time_units_per_second = std::chrono::duration_cast<time_res>(std::chrono::seconds(1)).count();
// Or, more directly for microseconds:
// static const TIME_TYPE time_units_per_second = 1000000;


static const ID_TYPE ID_DEFAULT  = 0; // Assuming valid UOIDs start from 1
static const PRICE_TYPE PRICE_DEFAULT = -1; // Or std::numeric_limits<PRICE_TYPE>::min() if appropriate
static const SIZE_TYPE SIZE_DEFAULT = -1;   // Or std::numeric_limits<SIZE_TYPE>::min()
static const ORDERTYPE ORDERTYPE_DEFAULT = ORDERTYPE::NONE;

const int LIMIT_ORDER_DEFAULT_TIMEOUT_MILLI = 10000;

#endif //ARENA_GLOBALS_H