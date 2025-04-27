#pragma once

#include "Common.h"

class ThreadCache
{
public:
	//申请一个size大小的内存
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	//当ThreadCache的自由链表中一个内存块都没有，则向CentralCache申请一个size大小内存块
	void* FetchFromCentralCache(size_t index, size_t size);
	
	//对象内存释放并检查链表长度后，发现链表过长，则回收内存到CentralCache
	void ListTooLong(FreeList& list, size_t size);

private:
	//采用多个哈希桶，每个哈希桶收纳不同字节范围的内存块
	FreeList _freeLists[NUM_FREELISTS];
};

//TLS——thread local storage
//允许每个线程拥有变量的独立副本,这意味着虽然所有线程看到的变量名相同，但每个线程可以存储和访问自己独立的值
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;