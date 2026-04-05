#include "ShmManager.h"
#include "StrategyConfig.h"

ShmManager::ShmManager() {
	/*
	string quantOrderPath = StrategyConfig::GetInstance().GetQuantOrderPath();
	int quantOrderSize = StrategyConfig::GetInstance().GetQuantOrderSize();

	string quantTransferPath = StrategyConfig::GetInstance().GetQuantTransferPath();
	int quantTransferSize = StrategyConfig::GetInstance().GetQuantTransferSize();

	shmQuantOrder = new SingleWriter<stra::QuantOrder>(quantOrderPath, quantOrderSize * sizeof(stra::QuantOrder) + sizeof(ShmHeader), true);
	shmQuantOrder->moveTo(shmQuantOrder->header()->frameCount);

	shmQuantTransfer = new SingleWriter<stra::QuantTransfer>(quantTransferPath, quantTransferSize * sizeof(stra::QuantTransfer) + sizeof(ShmHeader), true);
	shmQuantTransfer->moveTo(shmQuantTransfer->header()->frameCount);
	*/
}

ShmManager::~ShmManager() {
	if (shmQuantOrder) {
		delete shmQuantOrder;
		shmQuantOrder = nullptr;
	}

	if (shmQuantTransfer) {
		delete shmQuantTransfer;
		shmQuantTransfer = nullptr;
	}
}

SingleWriter<stra::QuantOrder>* ShmManager::GetShmQuantOrder() {
	return shmQuantOrder;
}

SingleWriter<stra::QuantTransfer>* ShmManager::GetShmQuantTransfer() {
	return shmQuantTransfer;
}

void ShmManager::PushQuantOrder(const stra::QuantOrder& order) {
	if (shmQuantOrder) {
		++shmQuantOrder->header()->frameCount;
		auto& curFrame = *shmQuantOrder->nextFrame(); // start from index 1
		memcpy(&curFrame, &order, sizeof(stra::QuantOrder));
	}
}

void ShmManager::PushQuantTransfer(const stra::QuantTransfer& transfer) {
	if (shmQuantTransfer) {
		++shmQuantTransfer->header()->frameCount;
		auto& curFrame = *shmQuantTransfer->nextFrame(); // start from index 1
		memcpy(&curFrame, &transfer, sizeof(stra::QuantTransfer));
	}
}
