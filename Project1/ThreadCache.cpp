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

	//�ҵ���Ӧ����������Ͱ�����ڴ���������
	size_t index = SizeClass::getIndex(size);
	_freeLists[index].Push(ptr);

	//����ThreadCache�����ȣ��������ڴ�ĳ��ȴ���һ������������ڴ������ʱ���ͷ���һ���ڴ�����CentralCache
	//����֮�⻹���Ը��ӿ������������ڴ������ڴ�������С
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}

}

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	//����ʼ���������㷨----����һ��������������ڴ����
	//���״�����ʱ��CentralCache�ṩС�����ڴ��
	//�ڵ�����ThreadCache������CentralCache����ͬ��size��С���ڴ�������ôCentralCacheһ�����ṩ���ڴ����batchNum�������ӣ�ֱ������
	//��sizeԽС����batchNum������Խ��sizeԽ����batchNum������ԽС
	size_t batchNum = min(_freeLists[index].MaxSize(),SizeClass::NumMoveSize(size));

	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);

	//ThreadCache����������Ҫһ��size��С�ڴ棬���Է���һ���ڴ漴��
	//����ȡ���˶���ڴ�飬�򽫳���һ��������ڴ�����CentralCache������������
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
	//��ThreadCache�����������������Ƴ�MaxSize������
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());

	//���Ƴ��Ķ���黹����һ���CentralCache��span��
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);

}