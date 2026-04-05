#pragma once

#include <math.h>
#include <vector>

using namespace std;

template <class T>
class DataArray {
private:
	vector<T> v;
	int size;
	int index;
	bool fullFlag;
public:
	DataArray();
	DataArray(int s);
	DataArray(int s, int i, T value);
	DataArray(const DataArray& arr);
	DataArray<T>& operator=(const DataArray& arr);
	T operator[](int i) const;
	T& operator[](int i);
	~DataArray();
	void Init(int s);
	int GetSize();
	int GetSize() const;
	int GetStartIndex();
	int GetStartIndex() const;
	int GetEndIndex();
	int GetEndIndex() const;
	T Get(int i);
	T Get(int i) const;
	T GetEndValue();
	void Add(T value);
	bool IsFull();
    bool IsEmpty();
};