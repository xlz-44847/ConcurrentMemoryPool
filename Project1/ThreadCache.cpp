#include "ThreadCache.h"
#include "CentralCache.h"

void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);

	size_t alignSize = SizeClass::RoundUp(size);
	size_t index = SizeClass::getIndex(size);
	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alignSize);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	//找到对应的自由链表桶，将内存对象挂起来
	size_t index = SizeClass::getIndex(size);
	_freeLists[index].Push(ptr);

	//限制ThreadCache链表长度：当闲置内存的长度大于一次批量申请的内存的上限时，就返还一批内存对象给CentralCache
	//除此之外还可以附加考虑限制限制内存对象的内存总量大小
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}

}

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	//慢开始反馈调节算法----控制一次性批量申请的内存块数
	//①首次申请时，CentralCache提供小批量内存块
	//②当后续ThreadCache不断向CentralCache申请同样size大小的内存需求，那么CentralCache一次性提供的内存块数batchNum会逐渐增加，直至上限
	//③size越小，其batchNum的上限越大；size越大，其batchNum的上限越小
	size_t batchNum = min(_freeLists[index].MaxSize(),SizeClass::NumMoveSize(size));

	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);

	//ThreadCache的申请是需要一个size大小内存，所以返回一块内存即可
	//如若取出了多个内存块，则将除第一个外多余内存块挂入CentralCache的自由链表中
	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	//从ThreadCache的自由链表中批量移出MaxSize个对象
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());

	//将移出的对象归还到上一层的CentralCache的span中
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);

}