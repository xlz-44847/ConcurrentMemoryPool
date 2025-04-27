#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include <thread>
#include <mutex>

#include <time.h>
#include <assert.h>

#ifdef _WIN32
#include<windows.h>
#else
// 
#endif

using std::cout;
using std::endl;

static const size_t MAX_BYTES = 256 * 1024;
static const size_t NUM_FREELISTS = 208;
static const size_t NUM_PAGES = 129;//PageCache不同页构成的哈希桶数量，n号哈希桶表示n页，0号被闲置
static const size_t PAGE_SHIFT = 13;//1 page = 2^13 Byte

#ifdef _WIN64 //x64配置下_WIN32和_WIN64都有定义，所以需要将_WIN64作为第一个
//64位机器，地址空间2^64，以8KB为一页，总页数为2^64/2^13=2^51
typedef unsigned long long PAGE_ID; 
#elif _WIN32 //x86配置下只有_WIN32的定义
//32位机器，地址空间2^32，以8KB为一页，总页数为2^32/2^13=2^19
typedef size_t PAGE_ID;
#endif

// VirtualAlloc是一个Windows API函数，该函数的功能是在调用进程的虚地址空间,预定或者提交一部分页
// 直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage) //1页=8KB
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	//VirtualAlloc返回的地址是64KB对齐的，所以对8KB也是对齐的
#else
	//Linux环境使用brk、mmap等
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

// 释放VirtualAlloc申请的空间
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//Linux环境使用sbrk、unmmap等
#endif
}

//传入小内存指针，返回该内存前指定机器架构（64位/32位）长度字节的值，即返回当前结点的next
static inline void*& NextObj(void* obj)
{
	return *(void**)obj;
}

//管理切分出来的小对象的自由链表
class FreeList
{
public:
	void Push(void* obj)//头插
	{
		assert(obj);

		//*(void**)obj = _freeList;
		NextObj(obj) = _freeList;
		_freeList = obj;

		++_size;
	}

	void PushRange(void* start, void* end, size_t n)//范围插入
	{
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	void* Pop()//头删，实际上是取出自由链表首个对象
	{
		assert(_freeList);

		void* obj = _freeList;
		_freeList = NextObj(obj);

		--_size;

		return obj;
	}

	//PopRange实际是从自由链表的头部取出n个对象
	//start和end为输出型参数，表示取出的一系列对象的第一个和最后一个的地址
	void PopRange(void*& start, void*& end, size_t n)//范围删除，头删n个对象
	{
		assert(n <= _size);

		start = _freeList;
		end = _freeList;

		for (size_t i = 1; i < n; i++)
		{
			end = NextObj(end);
		}
		_freeList = NextObj(end);
		NextObj(end) = nullptr;

		_size -= n;
	}

	bool Empty() const
	{
		return _freeList == nullptr;
	}

	size_t& MaxSize() 
	{
		return _maxSize;
	}

	size_t Size() const
	{
		return _size;
	}

private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0;
};

//对象大小对其映射规则
class SizeClass
{
	//申请字节数					对齐方式					占用freelist哈希桶号（如1~8字节内存归纳到0号桶，9~16字节内存归纳到1号桶，129~144字节内存归纳到16号桶）
	//(0,128]					8 Byte对齐				[0,16)
	//(128,1024]				16 Byte对齐				[16,72)
	//(1024,8*1024]				128 Byte对齐				[72,128)
	//(8*1024,64*1024]			1024 Byte对齐			[128,184)
	//(64*1024,256*1024]		8*1024 Byte对齐			[184,280)
	// 
	//内部碎片（分配超出需求的内存而产生的碎片）浪费比率：
	//8字节对齐		最大浪费字节比：7/8=87.5%				平均浪费字节数：（128/8）*（1+7）*7/2=448							总申请字节：8*8*(1+16)*16/2=78336			平均浪费占比：5.7%
	//16字节对齐		最大浪费字节比：15/144=10.4%			平均浪费字节数：（1024-128）/16 *（1+15）*15/2=6720					总申请字节：16*16(8+64)*56/2=516096		平均浪费占比：1.3%
	//128字节对齐		最大浪费字节比：127/1152=11.0%			平均浪费字节数：（64*1024-8*1024）/128 *（1+127）*127/2=6720
	//1024字节对齐	最大浪费字节比：1023/(9*1024)=11.1%	
	//8*1024字节对齐	最大浪费字节比：(8*1024-1)/(72*1024)=11.1%	
	//
	//最大浪费比率接近1/9
	//尽管小于8字节的申请浪费较大，但是由于内存块担任在自由链表中存储地址的功能，所以至少大小为8字节

private:
	static inline size_t _RoundUp(size_t size, size_t alignNum)
	{
		return ((size - 1) / alignNum + 1) * alignNum;
	}
	static inline size_t _getIndex(size_t size, size_t alignNum)
	{
		return (size - 1) / alignNum;
	}
public:
	//根据传入字节数确定对其后需要的内存大小
	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8*1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64*1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= 256*1024)
		{
			return _RoundUp(size, 8 * 1024);
		}
		else //申请内存超过256KB，则向PageCache或系统申请，按照页为单位对齐
		{
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}
	//根据传入字节数确定对其内存所在自由链表哈希桶号
	static inline size_t getIndex(size_t size)
	{
		static int group_array[4] = { 16,72,128,184 };
		if (size <= 128)
		{
			return _getIndex(size, 8);
		}
		else if (size <= 1024)
		{
			return _getIndex(size - 128, 16) + group_array[0];
		}
		else if (size <= 8 * 1024)
		{
			return _getIndex(size - 1024, 128) + group_array[1];
		}
		else if (size <= 64 * 1024)
		{
			return _getIndex(size - 8 * 1024, 1024) + group_array[2];
		}
		else if (size <= 256 * 1024)
		{
			return _getIndex(size - 64 * 1024, 8 * 1024) + group_array[3];
		}
		else
		{
			assert(false);
			return -1;
		}
	}

	//ThreadCache每一次从CentralCache中获得的内存块数上限
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);

		int num = MAX_BYTES / size;
		//一次性获取的内存块数上限值控制在[2,512]之间
		//对于大内存申请，一次批量给予数量相对较少
		//对于小内存申请，一次批量给予数量相对较多
		if (num < 2) num = 2;
		else if (num > 512) num = 512;
		return num;
	}

	//计算对于不同的对象size大小的span向PageCache申请的页数
	//对于8B的申请，需要512*8/(8*1024)=0.5页，抬升为1页
	//对于16B的申请，需要512*16/(8*1024)=1页
	//对于256KB的申请，需要2*256*1024/(8*1024)=64页
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size); //size大小对象需要的内存块数上限
		size_t npage = num * size; //申请size大小对象需要的总内存大小

		npage >>= PAGE_SHIFT; //根据需要的总内存大小，计算需要的页数
		if (npage == 0) npage = 1;

		return npage;
	}
};

//管理多个连续页大块内存的结构
struct Span
{
	PAGE_ID _pageId = 0;//大块内存起始页页号
	size_t _n = 0;//大块内存的页数

	//双向链表结构，便于随机删除结点
	Span* _next = nullptr;
	Span* _prev = nullptr;

	size_t _useCount = 0;//切分好的小块内存被分配给thread cache的计数
	void* _freeList = nullptr;//组织管理切好的小块内存的自由链表

	bool _isUse = false; //是否被使用，用于辅助span在PageCache中的合并相邻页操作
	//①span闲置返还给PageCache后
	//②span生成并给予CentralCache之后，到span真正被使用之前
	// （NewSpan之后_pageMtx解锁，到FetchRangeObj中对_useCount修改之间，这期间没有_pageMtx锁，存在被合并的线程风险）
	//以上这两种情况_useCount都是0，需要另一个成员变量来帮助区分span是否闲置

	size_t _objSize = 0;//切好的小对象的大小，在释放内存时使用
};

//带头双向循环链表
class SpanList
{
public:
	SpanList()
		:_head(new Span)
	{
		_head->_next = _head;
		_head->_prev = _head;
	}
	Span* Begin()
	{
		return _head->_next;
	}
	Span* End()
	{
		return _head;
	}
	bool Empty()
	{
		return _head->_next == _head;
	}
	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		//prev newSpan pos
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}
	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);

		Span* prev = pos->_prev;
		Span* next = pos->_next;
		//prev (pos) next
		prev->_next = next;
		next->_prev = prev;
	}
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

private:
	Span* _head;
public:
	std::mutex _mtx;//桶锁，允许多线程同时访问CentralCache的不同的桶，不允许多线程同时访问同一个桶
};