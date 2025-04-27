#pragma once

#include "Common.h"

//单例模式--饿汉模式
class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	//在一个CentralCache的指定的SpanList哈希桶中，获取一个非空的Span
	Span* GetOneSpan(SpanList& list, size_t size);

	//希望从中心缓存获取batchNum个size大小的对象给线程缓存
	//start和end是输出型参数，start输出取出的批量内存块中第一个对象的地址，end则是最后一个对象的地址
	//可能中心缓存的自由链表中没有足够的内存块，但仍要被取出，所以函数返回值为实际获得的内存块数
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	//将ThreadCache中一定数量的对象返还给span
	void ReleaseListToSpans(void* start, size_t size);

private:
	CentralCache()
	{}
	CentralCache(const CentralCache&) = delete;
	CentralCache& operator=(const CentralCache&) = delete;

	//CentralCache的对其映射规则和ThreadCache相同，哈希桶数量一致
	SpanList _spanLists[NUM_FREELISTS];

	static CentralCache _sInst;
};