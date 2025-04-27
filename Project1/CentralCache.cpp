#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	//��鵱ǰSpanList���������δ��������span���򷵻����span
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

	//�Ȱ�CentralCache��Ͱ���������ֹ���������߳�����Ͱ�ڷ��ڴ����
	list._mtx.unlock();

	PageCache::GetInstance()->_pageMtx.lock();//��PageCache����
	//���û�п���span������PageCache����
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;//��_pageMtx��֮ǰ��Ϊ��ʹ�ã���ֹ������ҳ�ϲ�
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();//��PageCache����

	//�����Ƕ�ȡ�õ�span�����з֣�����Ҫ��Ͱ������Ϊ���span��δ��������Ϸ���Ͱ�У������̷߳��ʲ���

	char* start = (char*)(span->_pageId << PAGE_SHIFT);//����span����ڴ����ʼ��ַ�������ַ��
	//��Ϊҳ����ͨ�������ַ/(8*1024)��������ģ����������ַ=ҳ��*8*1024
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;
	//���뵽span����Ҫ�Ѵ��ڴ��г�С�ڴ������span������������
	//ʹ��β��ķ�������֤С�ڴ�������������˳��������ڴ�֮���˳��һ��
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

	//������õ�span����Ͱ��ȥ��ʱ���ټ���
	list._mtx.lock();
	list.PushFront(span);

	return span;
}

size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::getIndex(size);

	_spanLists[index]._mtx.lock();//����

	//CentralCache���ڴ����ݴ�С��ͬ���ɶ��spanList��ϣͰ��֯
	//ÿ��spanList���Ƕ��Span��Span��������С�ڴ湹�ɵĵ���������_freeList
	//���Եõ��ڴ������Ҫ��һ����ǰ��ϣͰ�еķǿյ�Span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	//��span�л�ȡbatchNum���������������ȡ��������������
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

	_spanLists[index]._mtx.unlock();//����

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

		//��span�յ��ڴ����_useCountΪ0����ô˵�������ڴ�鶼���ջ�
		//����span���Ա�PageCache���գ�Ȼ��PageCache���䳢��ǰ��ҳ�ϲ�
		if (span->_useCount == 0)
		{
			//��span˫��������ȡ����span
			_spanLists[index].Erase(span);
			//��span�еĳ���ҳ�ź�ҳ����ҳ�����ڱ�ʶʵ�ʵ��ڴ��ַ��ҳ����ʶ�ڴ��С����������Ϣ�ÿ�
			//_freeList���ڴ���δ�����ȥ��С�ڴ棬������С�ڴ涼�ع��Ҫ��Ϊ���巵��PageCache��_freeList��û�������ˣ���Ϊͨ��ҳ�ž����ҵ��ڴ��ַ
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			//�ͷ�span��PageCacheʱ���Ϳ�����ʱ�⿪CentralCache��Ͱ��
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