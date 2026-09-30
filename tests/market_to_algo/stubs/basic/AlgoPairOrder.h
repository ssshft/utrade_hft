#ifndef _ALGO_PAIR_ORDER_H
#define _ALGO_PAIR_ORDER_H

// Stub for quant_library/basic/AlgoPairOrder.h.
// The real one additionally pulls json/nlohmann/json.hpp. The strategy side only does
// `new AlgoPairOrder()` and then writes inherited BaseAlgoOrder fields, so the stub
// keeps just the constructor declaration (defined in stubs.cpp).
#include "BaseAlgoOrder.h"

struct AlgoPairOrder : public BaseAlgoOrder {
    AlgoPairOrder();
};

#endif
