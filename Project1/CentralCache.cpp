#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	//检查当前SpanList，如果还有未分配对象的span，则返回这个span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else
		{
			it = it->_next;
		}
	}

	//先把CentralCache的桶锁解掉，防止阻塞其他线程释向桶内放内存对象
	list._mtx.unlock();

	PageCache::GetInstance()->_pageMtx.lock();//对PageCache加锁
	//如果没有空闲span，则向PageCache申请
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;//出_pageMtx锁之前置为在使用，防止被相邻页合并
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();//对PageCache解锁

	//以下是对取得的span进行切分，不需要加桶锁，因为这个span还未被处理完毕放入桶中，其他线程访问不到

	char* start = (char*)(span->_pageId << PAGE_SHIFT);//计算span大块内存的起始地址（虚拟地址）
	//因为页号是通过虚拟地址/(8*1024)计算出来的，所以虚拟地址=页号*8*1024
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;
	//申请到span后，需要把大内存切成小内存后链入span的自由链表中
	//使用尾插的方法，保证小内存存入自由链表的顺序和真正内存之间的顺序一致
	span->_freeList = start;
	void* tail = start;
	start += size;
	while (start < end)
	{
		NextObj(tail) = start;
		tail = start;
		start += size;
	}
	NextObj(tail) = nullptr;

	//将处理好的span挂入桶中去的时候再加锁
	list._mtx.lock();
	list.PushFront(span);

	return span;
}

size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::getIndex(size);

	_spanLists[index]._mtx.lock();//加锁

	//CentralCache的内存块根据大小不同，由多个spanList哈希桶组织
	//每个spanList中是多个Span，Span管理着由小内存构成的的自由链表_freeList
	//所以得到内存块首先要找一个当前哈希桶中的非空的Span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	//从span中获取batchNum个对象，如果不够则取到几个交付几个
	start = span->_freeList;
	end = start;
	size_t actualNum = 1;
	while (actualNum < batchNum && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		actualNum++;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;

	_spanLists[index]._mtx.unlock();//解锁

	return actualNum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::getIndex(size);
	_spanLists[index]._mtx.lock();

	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInstance()->MapObjToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;

		//当span收到内存后发现_useCount为0，那么说明所有内存块都被收回
		//于是span可以被PageCache回收，然后PageCache对其尝试前后页合并
		if (span->_useCount == 0)
		{
			//从span双向链表中取出该span
			_spanLists[index].Erase(span);
			//该span中的除了页号和页数（页号用于标识实际的内存地址，页数标识内存大小），其他信息置空
			//_freeList用于串联未分配出去的小内存，当所有小内存都回归后将要作为整体返还PageCache，_freeList就没有作用了，因为通过页号就能找到内存地址
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			//释放span给PageCache时，就可以暂时解开CentralCache的桶锁
			_spanLists[index]._mtx.unlock();

			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToSpanCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}

		start = next;
	}

	_spanLists[index]._mtx.unlock();

}