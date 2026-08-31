#include "DataArray.h"
#include "DataStruct.h"

template <class T>
DataArray<T>::DataArray() {
	size = 0;
	index = -1;
	fullFlag = false;
}

template <class T>
DataArray<T>::DataArray(int s) {
	Init(s);
}

template <class T>
DataArray<T>::DataArray(int s, int i, T value) {
	size = s;
	index = i % size;
	v = vector<T>(size, value);
}

template <class T>
DataArray<T>::~DataArray() {
	size = 0;
	index = -1;
	fullFlag = false;
	v.clear();
}

template <class T>
DataArray<T>::DataArray(const DataArray& arr) {
	size = arr.size;
	index = arr.index;
	v = arr.v;
	fullFlag = arr.fullFlag;
}

template <class T>
DataArray<T>& DataArray<T>::operator=(const DataArray& arr) {
	if (this != &arr) {
		this->index = arr.index;
		this->size = arr.size;
		this->v = arr.v;
		this->fullFlag = arr.fullFlag;
	}
	return *this;
}

template <class T>
T DataArray<T>::operator[](int i) const {
	int in = (i + size) % size;
	return v[in];
}

template <class T>
T& DataArray<T>::operator[](int i) {
	int in = (i + size) % size;
	return v[in];
}

template <class T>
void DataArray<T>::Init(int s) {
	index = -1;
	size = s;
	fullFlag = false;
	T t;
	v = vector<T>(size, t);
}

template <class T>
int DataArray<T>::GetSize() {
	return size;
}

template <class T>
int DataArray<T>::GetSize() const {
	return size;
}

template <class T>
int DataArray<T>::GetStartIndex() {
	int startIndex = (index + 1) % size;
	return startIndex;
}

template <class T>
int DataArray<T>::GetStartIndex() const {
	int startIndex = (index + 1) % size;
	return startIndex;
}

template <class T>
int DataArray<T>::GetEndIndex() {
	return index == size - 1 ? index : index + size;
}

template <class T>
int DataArray<T>::GetEndIndex() const {
	return index == size - 1 ? index : index + size;
}

template <class T>
T DataArray<T>::Get(int i) {
	int in = (i + size) % size;
	return v[in];
}

template <class T>
T DataArray<T>::Get(int i) const {
	int in = (i + size) % size;
	return v[in];
}

template <class T>
T DataArray<T>::GetEndValue() {
	int endIndex = GetEndIndex();
	return Get(endIndex);
}

template <class T>
void DataArray<T>::Add(T value) {
	index++;

	if (!fullFlag && index == size - 1) {
		fullFlag = true;
	}

	index = index % size;
	v[index] = value;
}

template <class T>
bool DataArray<T>::IsFull() {
	return fullFlag;
}

template <class T>
bool DataArray<T>::IsEmpty() {
    if (index < 0) {
        return true;
    } else {
        return false;
    }
}

template class DataArray<stra::QuantKline>;
template class DataArray<stra::QuantMarketDepth>;
template class DataArray<stra::QuantMarketTrade>;
template class DataArray<stra::QuantSpread>;
template class DataArray<double>;