#include "PageCache.h"

PageCache PageCache::_sInst;

Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	
	//③申请内存超过128页，专门从堆申请一个特定页数的span
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

	//①当申请内存不大于256KB（32页），则经由CentralCache::GetOneSpan申请span
	//②当申请内存介于32页到128页之间，则直接由ConcurrentAlloc申请span，不再被三层缓存的下两层管理
	//如果第k个桶中有span，则取出并返回
	//返回时需要注意将span中的每一页都在unordered_map中记录页号到span的映射关系
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

	//如果第k个桶没有span，则以此检查后续的桶
	//如果在第i个桶找到span，则取出并切分为k页span和i-k页span，前者返回，后者挂在第i-k个桶中
	for (size_t i = k + 1; i < NUM_PAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* iSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			//在iSpan的头部切出一个kSpan，实际就是新建一个Span对象，并初始化其页号和页数信息
			kSpan->_pageId = iSpan->_pageId;
			kSpan->_n = k;

			//iSpan剩余部分在调整其页号和页数信息后，挂在第i-k个桶中
			iSpan->_pageId += k;
			iSpan->_n -= k;
			_spanLists[i - k].PushFront(iSpan);

			//由于合并相邻页号的span需要对PageCache中的span完成给出页号，得到span的功能
			//所以对于挂在PageCache中的页也需要进行页号到Span*的映射
			//span是多个连续页的整体，对于页号到Span*的映射只需要记录首页号和尾页号
			//因为对于一个想合并的span，前一页就是前一个span的尾页，后一页就是后一个span的首页
			//_idSpanMap[iSpan->_pageId] = iSpan;
			//_idSpanMap[iSpan->_pageId + iSpan->_n - 1] = iSpan;
			_idSpanMap.set(iSpan->_pageId, iSpan);
			_idSpanMap.set(iSpan->_pageId + iSpan->_n - 1, iSpan);

			//VirtualAlloc申请的内存以8KB为单位对齐，通过页号交给span管理
			//一个k页的span，通过其页号_pageId*8*1024可以找到实际管理的内存的开始地址
			//		通过其页数_n可以得到其管理的总内存大小
			//对于任意一个分配出去小内存，只需将其地址除以8*1024，即可得到其内存所在的页号
			//		知道页号就可以继续找到管理这个页号的span，因此就可以对任意一个小内存进行span溯源
			//在CentralCache回收ThreadCache返回的小内存时，需要将这些内存块挂在其原本的span下
			//如果采取对返回的一串内存，每一个都对CentralCache的对于大小的桶中的span便利查找，时间复杂度为m*n，开销很大
			//为了提高效率，因而建立页号_pageId和span的映射
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				//_idSpanMap[kSpan->_pageId + i] = kSpan; //PAGE_ID-->Span*
				_idSpanMap.set(kSpan->_pageId + i, kSpan); //PAGE_ID-->Span*
			}

			return kSpan;
		}
	}

	//如果后续所有桶都没有span，则去向堆申请128页的span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NUM_PAGES - 1);//SystemAlloc会调用VirtualAlloc以页为单位申请内存空间，返回的地址是8KB对齐的
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;//span中存储的页号是实际内存地址的页号，由于VirtualAlloc的地址8KB对齐，所以页号相连实际上也说明内存相连
	bigSpan->_n = NUM_PAGES - 1;

	_spanLists[NUM_PAGES - 1].PushFront(bigSpan);

	return NewSpan(k);//递归调用复用函数逻辑，找到新申请的128页的span并切分
}

Span* PageCache::MapObjToSpan(void* obj)
{
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;

	/*
	std::unique_lock<std::mutex> lock(_pageMtx);
	//NewSpan中对map写操作，ReleaseSpanToSpanCache和ConcurrentFree对map对操作
	//会出现线程安全问题，所以需要加锁

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
	//不需要加锁，因为基数树的读写是分离的，两个线程不可能对同一个span进行操作，所以也就不可能在读写基数树时产生线程冲突
	//并且基数树在写之前结构就确定了，在后续的读写中不再改变结构
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}

void PageCache::ReleaseSpanToSpanCache(Span* span)
{
	//③专门释放因内存超过128页而专门申请一个特定页数的span，将其直接还给堆
	if (span->_n > NUM_PAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);

		return;
	}

	//向前合并
	while (true)
	{
		PAGE_ID prevId = span->_pageId - 1;
		/*
		auto record = _idSpanMap.find(prevId);

		if (record == _idSpanMap.end())//未找到前一个span
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

		if (mergeSpan->_isUse == true)//前一个span被使用
		{
			break;
		}
		if (mergeSpan->_n + span->_n > NUM_PAGES - 1)//超过最大可存储页数
		{
			break;
		}
		//mergeSpan span
		//mergeSpan合并到span，mergeSpan从SpanList中删除后delete
		span->_pageId = mergeSpan->_pageId;
		span->_n += mergeSpan->_n;
		
		_spanLists[mergeSpan->_n].Erase(mergeSpan);
		//delete mergeSpan;
		_spanPool.Delete(mergeSpan);
	}	
	//向后合并
	while (true)
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		/*
		auto record = _idSpanMap.find(nextId);

		if (record == _idSpanMap.end())//未找到后一个span
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

		if (mergeSpan->_isUse == true)//后一个span被使用
		{
			break;
		}
		if (mergeSpan->_n + span->_n > NUM_PAGES - 1)//超过最大可存储页数
		{
			break;
		}
		//span mergeSpan
		//mergeSpan合并到span，mergeSpan从SpanList中删除后delete
		span->_n += mergeSpan->_n;
		
		_spanLists[mergeSpan->_n].Erase(mergeSpan);
		//delete mergeSpan;
		_spanPool.Delete(mergeSpan);
	}
	//可能的合并操作结束后，将span挂入PageCache的指定页数的SpanList队列
	//每一个挂在PageCache中的span，都需要记录自己首页号和尾页号对span*的映射
	//对于合并前的span的首尾页号映射也存储在_idSpanMap中，它们在合并完后已然是无效的过期值
	//对于这些无效值无需处理，因为在接下来的合并中并不会被访问
	//如果对应页号一旦被CentralCache启用，那么就会有新的映射值覆盖旧的映射关系
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}
