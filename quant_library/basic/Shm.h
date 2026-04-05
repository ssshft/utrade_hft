#ifndef _SHM_H
#define _SHM_H

#ifdef WINDOWS
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

inline uintptr_t load_mmap_buffer(const string path, size_t size, bool isWriting, bool lazy) {
#ifdef WINDOWS
	bool master = isWriting || !lazy;
	HANDLE dumpFileDescriptor = CreateFileA(path.c_str(),
		master ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		master ? OPEN_ALWAYS : OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	if (dumpFileDescriptor == INVALID_HANDLE_VALUE) {
		throw "unable to mmap for page " + path;
	}

	HANDLE fileMappingObject = CreateFileMapping(dumpFileDescriptor,
		nullptr,
		master ? PAGE_READWRITE : PAGE_READONLY,
		0,
		size,
		nullptr);

	if (fileMappingObject == nullptr) {
		int nRet = GetLastError();
		throw "unable to mmap for page " + path;
	}

	void* buffer = MapViewOfFile(fileMappingObject,
		master ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ,
		0,
		0,
		size);

	if (buffer == nullptr) {
		int nRet = GetLastError();
		throw "failed to load page " + path + ", MapViewOfFile Error " + std::to_string(nRet);
	}

	CloseHandle(fileMappingObject);
	CloseHandle(dumpFileDescriptor);
#else
	bool master = isWriting || lazy;
	int fd = open(path.c_str(), (master ? O_RDWR : O_RDONLY) | O_CREAT, (mode_t)0600);
	if (fd < 0) {
		throw "failed to open file for page " + path;
	}

	if (master) {
		if (lseek(fd, size - 1, SEEK_SET) == -1) {
			close(fd);
			throw "failed to stretch for page " + path;
		}
		if (write(fd, "", 1) == -1) {
			close(fd);
			throw "unable to write for page " + path;
		}
	}

	void* buffer = mmap(0, size, master ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_SHARED, fd, 0);
	if (buffer == MAP_FAILED) {
		close(fd);
		throw "Error mapping file to buffer!";
	}

	if (!lazy && madvise(buffer, size, MADV_RANDOM) != 0 && mlock(buffer, size) != 0) {
		munmap(buffer, size);
		close(fd);
		throw "failed to lock memory for page " + path;
	}

	close(fd);
#endif
	return reinterpret_cast<uintptr_t>(buffer);
}

inline bool release_mmap_buffer(uintptr_t address, size_t size, bool lazy) {
	void* buffer = reinterpret_cast<void*>(address);
#ifdef WINDOWS
	FlushViewOfFile(buffer, 0);
	UnmapViewOfFile(buffer);
#else
	if (!lazy && munlock(buffer, size) != 0) {
		return false;
	}

	if (munmap(buffer, size) != 0) {
		return false;
	}
#endif	
	return true;
}

struct ShmHeader {
	uint32_t version{1};
	uint32_t headerLen;
	uint32_t frameLen;
	int64_t createTime;
	int64_t flushTime;
	char userReserve[8];
	volatile uint64_t frameCount;
#ifndef WINDOWS
} __attribute__((packed));
#else
};
#pragma pack(pop)
#endif


template<typename T>
class Shm {
public:
	Shm(const string pa, size_t s, bool hasH = false, bool isW = false, bool la = true);
	virtual ~Shm();

	ShmHeader* header() {
		return hd;
	}

	T* beginFrame() {
		return headFrame;
	}

	T* currentFrame() {
		//return curFrame;
		return &headFrame[currentIndex];
	}

	T* nextFrame();
	void next();
	
	bool last() {
		return currentIndex >= maxIndex;
	}

	size_t frameLen() {
		return maxIndex + 1;
	}

	void moveTo(size_t fNb);

	T* at(size_t index) {
		auto nb = index >= maxIndex ? maxIndex : index;
		return &headFrame[nb];
	}

private:
	ShmHeader* hd;
	uintptr_t root;
	//T* curFrame;
	T* headFrame;
	size_t currentIndex;
	size_t maxIndex;
	const bool lazy;
	const size_t size;
	const bool isWriting;
	const bool hasHeader;
};

template<typename T>
Shm<T>::Shm(const string pa, size_t s, bool hasH, bool isW, bool la) : size(s), isWriting(isW), lazy(la), currentIndex(0), hasHeader(hasH) {
	root = load_mmap_buffer(pa, size, isWriting, lazy);
	size_t headerBufLen = 0;
	if (hasHeader) {
		hd = reinterpret_cast<ShmHeader*>(root);
		headerBufLen = sizeof(ShmHeader);
	} else {
		hd = new ShmHeader();
	}

	size_t frameLength = sizeof(T);
	maxIndex = (size - headerBufLen) / frameLength - 1;
	headFrame = reinterpret_cast<T*>(root + headerBufLen);
	//curFrame = headFrame;
	currentIndex = 0;
}

template<typename T>
Shm<T>::~Shm() {
	release_mmap_buffer(root, size, lazy);
}

template<typename T>
void Shm<T>::next() {
	if (last()) {
		return;
	}

	//++curFrame;
	++currentIndex;
}

template<typename T>
T* Shm<T>::nextFrame() {
	++currentIndex;
	//return ++curFrame;
	currentIndex = currentIndex > maxIndex ? 1 : currentIndex;  // 循环数组
	return &headFrame[currentIndex];
}

template<typename T>
void Shm<T>::moveTo(size_t index) {
	size_t nb = index >= maxIndex ? maxIndex : index;
	currentIndex = nb;
	//curFrame = &headFrame[currentIndex];
}

template<typename T>
class Reader : public Shm<T> {
public:
	explicit Reader(const string pa, size_t s, bool hasH = true): Shm<T>(pa, s, hasH) {}

	~Reader() {}
};

template<typename T>
class SingleWriter : public Shm<T> {
public:
	explicit SingleWriter(const string pa, size_t s, bool hasH): Shm<T>(pa, s, hasH, true) {}

	~SingleWriter() {}	
};

template<typename T>
class Initializer : public Shm<T> {
public:
	 explicit Initializer(const string pa, size_t s, bool hasH = true);

	~Initializer() {}
};

template<typename T>
Initializer<T>::Initializer(const string pa, size_t s, bool hasH): Shm<T>(pa, s, hasH, true) {
	ShmHeader* head = this->header();
	head->version = 1;
	head->headerLen = sizeof(ShmHeader);
	head->frameLen = sizeof(T);
	head->createTime = 0;
	head->flushTime = 0;
	head->frameCount = 0;
}

#endif
