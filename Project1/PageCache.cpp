#include "PageCache.h"

PageCache PageCache::_sInst;

Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	
	//�������ڴ泬��128ҳ��ר�ŴӶ�����һ���ض�ҳ����span
	if (k > NUM_PAGES - 1)
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;

		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);

		return span;
	}

	//�ٵ������ڴ治����256KB��32ҳ��������CentralCache::GetOneSpan����span
	//�ڵ������ڴ����32ҳ��128ҳ֮�䣬��ֱ����ConcurrentAlloc����span�����ٱ����㻺������������
	//�����k��Ͱ����span����ȡ��������
	//����ʱ��Ҫע�⽫span�е�ÿһҳ����unordered_map�м�¼ҳ�ŵ�span��ӳ���ϵ
	if (!_spanLists[k].Empty())
	{
		Span* kSpan = _spanLists[k].PopFront();
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan; //PAGE_ID-->Span*
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}
		return kSpan;
	}

	//�����k��Ͱû��span�����Դ˼�������Ͱ
	//����ڵ�i��Ͱ�ҵ�span����ȡ�����з�Ϊkҳspan��i-kҳspan��ǰ�߷��أ����߹��ڵ�i-k��Ͱ��
	for (size_t i = k + 1; i < NUM_PAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* iSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			//��iSpan��ͷ���г�һ��kSpan��ʵ�ʾ����½�һ��Span���󣬲���ʼ����ҳ�ź�ҳ����Ϣ
			kSpan->_pageId = iSpan->_pageId;
			kSpan->_n = k;

			//iSpanʣ�ಿ���ڵ�����ҳ�ź�ҳ����Ϣ�󣬹��ڵ�i-k��Ͱ��
			iSpan->_pageId += k;
			iSpan->_n -= k;
			_spanLists[i - k].PushFront(iSpan);

			//���ںϲ�����ҳ�ŵ�span��Ҫ��PageCache�е�span��ɸ���ҳ�ţ��õ�span�Ĺ���
			//���Զ��ڹ���PageCache�е�ҳҲ��Ҫ����ҳ�ŵ�Span*��ӳ��
			//span�Ƕ������ҳ�����壬����ҳ�ŵ�Span*��ӳ��ֻ��Ҫ��¼��ҳ�ź�βҳ��
			//��Ϊ����һ����ϲ���span��ǰһҳ����ǰһ��span��βҳ����һҳ���Ǻ�һ��span����ҳ
			//_idSpanMap[iSpan->_pageId] = iSpan;
			//_idSpanMap[iSpan->_pageId + iSpan->_n - 1] = iSpan;
			_idSpanMap.set(iSpan->_pageId, iSpan);
			_idSpanMap.set(iSpan->_pageId + iSpan->_n - 1, iSpan);

			//VirtualAlloc������ڴ���8KBΪ��λ���룬ͨ��ҳ�Ž���span����
			//һ��kҳ��span��ͨ����ҳ��_pageId*8*1024�����ҵ�ʵ�ʹ�����ڴ�Ŀ�ʼ��ַ
			//		ͨ����ҳ��_n���Եõ����������ڴ��С
			//��������һ�������ȥС�ڴ棬ֻ�轫���ַ����8*1024�����ɵõ����ڴ����ڵ�ҳ��
			//		֪��ҳ�žͿ��Լ����ҵ��������ҳ�ŵ�span����˾Ϳ��Զ�����һ��С�ڴ����span��Դ
			//��CentralCache����ThreadCache���ص�С�ڴ�ʱ����Ҫ����Щ�ڴ�������ԭ����span��
			//�����ȡ�Է��ص�һ���ڴ棬ÿһ������CentralCache�Ķ��ڴ�С��Ͱ�е�span�������ң�ʱ�临�Ӷ�Ϊm*n�������ܴ�
			//Ϊ�����Ч�ʣ��������ҳ��_pageId��span��ӳ��
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				//_idSpanMap[kSpan->_pageId + i] = kSpan; //PAGE_ID-->Span*
				_idSpanMap.set(kSpan->_pageId + i, kSpan); //PAGE_ID-->Span*
			}

			return kSpan;
		}
	}

	//�����������Ͱ��û��span����ȥ�������128ҳ��span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NUM_PAGES - 1);//SystemAlloc�����VirtualAlloc��ҳΪ��λ�����ڴ�ռ䣬���صĵ�ַ��8KB�����
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;//span�д洢��ҳ����ʵ���ڴ��ַ��ҳ�ţ�����VirtualAlloc�ĵ�ַ8KB���룬����ҳ������ʵ����Ҳ˵���ڴ�����
	bigSpan->_n = NUM_PAGES - 1;

	_spanLists[NUM_PAGES - 1].PushFront(bigSpan);

	return NewSpan(k);//�ݹ���ø��ú����߼����ҵ��������128ҳ��span���з�
}

Span* PageCache::MapObjToSpan(void* obj)
{
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;

	/*
	std::unique_lock<std::mutex> lock(_pageMtx);
	//NewSpan�ж�mapд������ReleaseSpanToSpanCache��ConcurrentFree��map�Բ���
	//������̰߳�ȫ���⣬������Ҫ����

	auto retPair = _idSpanMap.find(id);
	if (retPair != _idSpanMap.end())
	{
		return retPair->second;
	}
	else
	{
		assert(false);
		return nullptr;
	}
	*/
	//����Ҫ��������Ϊ�������Ķ�д�Ƿ���ģ������̲߳����ܶ�ͬһ��span���в���������Ҳ�Ͳ������ڶ�д������ʱ�����̳߳�ͻ
	//���һ�������д֮ǰ�ṹ��ȷ���ˣ��ں����Ķ�д�в��ٸı�ṹ
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}

void PageCache::ReleaseSpanToSpanCache(Span* span)
{
	//��ר���ͷ����ڴ泬��128ҳ��ר������һ���ض�ҳ����span������ֱ�ӻ�����
	if (span->_n > NUM_PAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);

		return;
	}

	//��ǰ�ϲ�
	while (true)
	{
		PAGE_ID prevId = span->_pageId - 1;
		/*
		auto record = _idSpanMap.find(prevId);

		if (record == _idSpanMap.end())//δ�ҵ�ǰһ��span
		{
			break;
		}

		Span* mergeSpan = record->second;
		*/
		auto record = (Span*)_idSpanMap.get(prevId);
		if (record == nullptr)
		{
			break;
		}

		Span* mergeSpan = record;

		if (mergeSpan->_isUse == true)//ǰһ��span��ʹ��
		{
			break;
		}
		if (mergeSpan->_n + span->_n > NUM_PAGES - 1)//�������ɴ洢ҳ��
		{
			break;
		}
		//mergeSpan span
		//mergeSpan�ϲ���span��mergeSpan��SpanList��ɾ����delete
		span->_pageId = mergeSpan->_pageId;
		span->_n += mergeSpan->_n;
		
		_spanLists[mergeSpan->_n].Erase(mergeSpan);
		//delete mergeSpan;
		_spanPool.Delete(mergeSpan);
	}	
	//���ϲ�
	while (true)
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		/*
		auto record = _idSpanMap.find(nextId);

		if (record == _idSpanMap.end())//δ�ҵ���һ��span
		{
			break;
		}

		Span* mergeSpan = record->second;
		*/
		auto record = (Span*)_idSpanMap.get(nextId);
		if (record == nullptr)
		{
			break;
		}

		Span* mergeSpan = record;

		if (mergeSpan->_isUse == true)//��һ��span��ʹ��
		{
			break;
		}
		if (mergeSpan->_n + span->_n > NUM_PAGES - 1)//�������ɴ洢ҳ��
		{
			break;
		}
		//span mergeSpan
		//mergeSpan�ϲ���span��mergeSpan��SpanList��ɾ����delete
		span->_n += mergeSpan->_n;
		
		_spanLists[mergeSpan->_n].Erase(mergeSpan);
		//delete mergeSpan;
		_spanPool.Delete(mergeSpan);
	}
	//���ܵĺϲ����������󣬽�span����PageCache��ָ��ҳ����SpanList����
	//ÿһ������PageCache�е�span������Ҫ��¼�Լ���ҳ�ź�βҳ�Ŷ�span*��ӳ��
	//���ںϲ�ǰ��span����βҳ��ӳ��Ҳ�洢��_idSpanMap�У������ںϲ������Ȼ����Ч�Ĺ���ֵ
	//������Щ��Чֵ���账����Ϊ�ڽ������ĺϲ��в����ᱻ����
	//�����Ӧҳ��һ����CentralCache���ã���ô�ͻ����µ�ӳ��ֵ���Ǿɵ�ӳ���ϵ
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}
