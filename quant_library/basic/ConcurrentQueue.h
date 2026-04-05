#ifndef _CONCURRENT_QUEUE_H
#define _CONCURRENT_QUEUE_H

#include "concurrentqueue/concurrentqueue.h"
//#include "concurrentqueue/blockingconcurrentqueue.h"


template <class T, size_t Size>
class ConcurrentQueue {
public:
	ConcurrentQueue() {
		q = new moodycamel::ConcurrentQueue<T>(Size);
	}

	void Push(T data) {
		q->enqueue(data);
	}

	bool Pop(T& data) {
		if (q->try_dequeue(data)) {
			return true;
		} else {
			return false;
		}
	}

	~ConcurrentQueue() {
		if (q) {
			delete q;
			q = nullptr;
		}
	}

private:
	moodycamel::ConcurrentQueue<T>* q;
};

#endif
