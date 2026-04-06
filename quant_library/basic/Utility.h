#ifndef _UTILITY_H
#define _UTILITY_H

#include "fmt/core.h"
#include "ConcurrentQueue.h"
#include "time_util.h"
#include <perf.h>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>

using namespace std;
using namespace std::chrono;


struct content {
	int type;
	string msg;
};


typedef ConcurrentQueue<string, 10000> RQUEUE;
typedef ConcurrentQueue<content, 100000> CONTENTQUEUE;
extern RQUEUE rLarkMsg;
extern CONTENTQUEUE contentQueue;



inline int64_t GetCurrentTimeUs() { // 微妙
	return high_resolution_clock::now().time_since_epoch().count() / 1000;
}

inline int64_t gettickcount() {  // 毫秒
    return GetCurrentTimeUs() / 1000; 
}

inline int64_t GetCurrentTime() {  // 秒
    return GetCurrentTimeUs() / 1000 / 1000; 
}

inline string CovertToUtcStr(int64_t tUs, bool hasUs = true) {
	if (tUs <= 0) {
		return "0";
	}
	time_t ts = tUs / 1000000;
	long us = tUs % 1000000;
	struct tm tmT = *gmtime(&ts);
	char s[64];
	strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &tmT);
	stringstream ss;
	if (hasUs) {
		ss << s << "." << us;
	} else {
		ss << s;
	}
	
	return ss.str();
}

inline string CovertToUtcDate(int64_t tUs) {
	if (tUs <= 0) {
		return "0";
	}
	time_t ts = tUs / 1000000;
	long us = tUs % 1000000;
	struct tm tmT = *gmtime(&ts);
	char s[64];
	strftime(s, sizeof(s), "%Y-%m-%d", &tmT);
	
	return string(s);
}

inline void splitString(const string& source, vector<string>& v, const string delimiters = " ") {
	string::size_type lastPos = source.find_first_not_of(delimiters, 0);
	string::size_type pos = source.find_first_of(delimiters, lastPos);
	while (string::npos != pos || string::npos != lastPos) {
		v.push_back(source.substr(lastPos, pos - lastPos));
		lastPos = source.find_first_not_of(delimiters, pos);
		pos = source.find_first_of(delimiters, lastPos);
	}
}

inline int DiffPeriod(int64_t preDateTime, int64_t currentTime, int period) {
    if (preDateTime <= 0 || currentTime <= 0 || currentTime < preDateTime) {
        return 0;
    }

    int64_t curPeriod = currentTime / period;
    int64_t prePeriod = currentTime / period;
    return curPeriod - prePeriod;
}

inline int64_t GenerateStrategyOrderId() {
	// strategyOrderId++;
	// return strategyOrderId;
	return crypto::rdtscp();
}

inline int64_t GenerateStrategyPairId() {
	// strategyPairId++;
	// return strategyPairId;
	return crypto::rdtscp();
}

inline int64_t GenerateStrategyAlgoPairId() {
	// strategyAlgoPairId++;
	// return strategyAlgoPairId;
	return crypto::rdtscp();
}



#endif
