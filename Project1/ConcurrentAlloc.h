#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

static void* ConcurrentAlloc(size_t size)
{
	//①当申请内存 size<=256KB 时，
	//	此时单个内存块大小未超过ThreadCache和CentralCache的管理上限，可以通过三层缓存获取内存
	if (size <= MAX_BYTES)
	{
		//通过TLS，每个线程都可以无锁的获取自己专属的ThreadCache对象
		if (pTLSThreadCache == nullptr)
		{
			//pTLSThreadCache = new ThreadCache;
			static ObjectPool<ThreadCache> tcPool;
			pTLSThreadCache = tcPool.New();
		}
		//cout << std::this_thread::get_id() << ':' << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}

	//②当申请内存 256KB<size<=128*8KB 时，即申请页数 32页<page<=128页，
	//	此时单个内存块超过ThreadCache和CentralCache的管理上限，但未超过PageCache的管理上限，可以直接向PageCache申请内存
	//③当申请内存 128*8KB<size 时，即申请页数 128页<page，
	//	此时单个内存块超过PageCache的管理上限，需要找系统堆申请
	else
	{
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;//申请内存需要的页数

		PageCache::GetInstance()->_pageMtx.lock();
		//②和③两种情况都交由PageCache去处理，PageCache中会记录页号与span的映射，方便释放操作
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = size;
		PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
}

static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjToSpan(ptr);
	size_t size = span->_objSize;
	if (size <= MAX_BYTES)
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);//TODO
	}
	else
	{
		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToSpanCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
}