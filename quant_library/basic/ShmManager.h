#ifndef _SHM_MANAGER_H
#define _SHM_MANAGER_H

#include "DataStruct.h"
#include "Shm.h"

class ShmManager {
public:
	ShmManager();
	~ShmManager();

	SingleWriter<stra::QuantOrder>* GetShmQuantOrder();
	SingleWriter<stra::QuantTransfer>* GetShmQuantTransfer();

	void PushQuantOrder(const stra::QuantOrder& order);
	void PushQuantTransfer(const stra::QuantTransfer& transfer);

private:
	SingleWriter<stra::QuantOrder>* shmQuantOrder;
	SingleWriter<stra::QuantTransfer>* shmQuantTransfer;
};

#endif
