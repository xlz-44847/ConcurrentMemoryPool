#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

//单例模式----饿汉模式
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	//获取一个k页的span
	Span* NewSpan(size_t k);

	//传入内存对象，根据unordered_map映射获得所属Span*
	Span* MapObjToSpan(void* obj);

	//CentralCache返回空闲span到PageCache，PageCache接收并合并页号相邻的span
	void ReleaseSpanToSpanCache(Span* span);

private:
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

	SpanList _spanLists[NUM_PAGES];

	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;//记录页号和对应span的map，提高通过内存块寻找其所在span的效率
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	ObjectPool<Span> _spanPool;//在PageCache中sSpan对象是通过new创建的
	//为了和new、malloc等接口绝缘，采用定长内存池的方式建立Span对象

public:
	std::mutex _pageMtx;//对PageCache的锁
	//PageCache整体上锁，而非个别桶上锁
	//这是因为PageCache的工作逻辑可能涉及很多桶（申请2页span，可能需要顺着查看3、4、5……页数的span）：
	//①最开始PageCache的spanList为空，需要向系统申请内存，于是得到一个128页的span
	//②根据CentralCache申请的页数（假设为2），将128页的span分成两部分（2页span和126页span）一个返回，另一个挂在对应的桶中
	//③根据CentralCache中span的usecount计数，如果为0，则说明提供给ThreadCache的小内存全部返还
	//	此时CentralCache就可以将这个span还给PageCache
	//  PageCache通过这个页的页号，查看其前后相邻的页是否空闲，以此合并出更大的页，解决内存碎片问题

	//使用整体锁而不是桶锁还因为整体锁仅需一把，而桶锁可能要频繁的加锁解锁效率较低

private:
	static PageCache _sInst;
};