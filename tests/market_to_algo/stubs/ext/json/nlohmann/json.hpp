// Minimal nlohmann::json stand-in. Nothing in the compiled translation units
// actually serialises JSON; AlgoPairOrder.h only needs the type to exist.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace nlohmann {

class json {
public:
    json() = default;
    template <typename T>
    json(T&&) {}
    template <typename T>
    json& operator=(T&&) { return *this; }
};

} // namespace nlohmann
