#ifndef _UTILITY_H
#define _UTILITY_H

#include "fmt/core.h"
#include "ConcurrentQueue.h"
#include "time_util.h"
#include "DataStruct.h"
#include <perf.h>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>

using namespace std;
using namespace std::chrono;


typedef ConcurrentQueue<string, 10000> RQUEUE;
// content 已经从 {int + std::string} 变成 {int + union}，sizeof(content) 实测 808 字节
// （最大成员是 AlgoOrderRecord = 800）。100000 的初始容量会预分配约 77MB，
// 而写线程现在会把队列排空，正常应该长期贴近 0，16384（约 12MB）足够。
// 真要调大，moodycamel 在写满时会自行扩容，不会丢数据，只是会多一次分配。
typedef ConcurrentQueue<content, 16384> CONTENTQUEUE;

extern RQUEUE rLarkMsg;
extern CONTENTQUEUE contentQueue;

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

inline int64_t GenerateStrategyOrderId() {
	return crypto::rdtscp();
}

inline int64_t GenerateStrategyPairId() {
	return crypto::rdtscp();
}

inline int64_t GenerateStrategyAlgoPairId() {
	return crypto::rdtscp();
}



#endif
