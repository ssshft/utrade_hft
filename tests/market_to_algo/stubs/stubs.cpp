// Definitions for the stubbed-out pieces of the algo-order execution side.
// PairTradingContext.cpp does `new AlgoPairOrder()`; nothing else from the execution
// side is linked in (the tests drive the strategy half only).
#include "basic/AlgoPairOrder.h"

AlgoPairOrder::AlgoPairOrder() = default;
