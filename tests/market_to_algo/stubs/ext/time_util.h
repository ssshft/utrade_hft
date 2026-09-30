#pragma once
#include <chrono>
#include <cstdint>

// The real time_util.h pulls in the crypto/crypto_errors machinery that does not
// exist on macOS. The strategy side only needs a wall-clock microsecond stamp.
namespace crypto {

inline int64_t getCurrentTime() {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

inline int64_t rdtscp() {
    return getCurrentTime();
}

} // namespace crypto
