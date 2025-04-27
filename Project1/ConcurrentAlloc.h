#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

static void* ConcurrentAlloc(size_t size)
{
	//�ٵ������ڴ� size<=256KB ʱ��
	//	��ʱ�����ڴ���Сδ����ThreadCache��CentralCache�Ĺ������ޣ�����ͨ�����㻺���ȡ�ڴ�
	if (size <= MAX_BYTES)
	{
		//ͨ��TLS��ÿ���̶߳����������Ļ�ȡ�Լ�ר����ThreadCache����
		if (pTLSThreadCache == nullptr)
		{
			//pTLSThreadCache = new ThreadCache;
			static ObjectPool<ThreadCache> tcPool;
			pTLSThreadCache = tcPool.New();
		}
		//cout << std::this_thread::get_id() << ':' << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}

	//�ڵ������ڴ� 256KB<size<=128*8KB ʱ��������ҳ�� 32ҳ<page<=128ҳ��
	//	��ʱ�����ڴ�鳬��ThreadCache��CentralCache�Ĺ������ޣ���δ����PageCache�Ĺ������ޣ�����ֱ����PageCache�����ڴ�
	//�۵������ڴ� 128*8KB<size ʱ��������ҳ�� 128ҳ<page��
	//	��ʱ�����ڴ�鳬��PageCache�Ĺ������ޣ���Ҫ��ϵͳ������
	else
	{
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;//�����ڴ���Ҫ��ҳ��

		PageCache::GetInstance()->_pageMtx.lock();
		//�ں͢��������������PageCacheȥ����PageCache�л��¼ҳ����span��ӳ�䣬�����ͷŲ���
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