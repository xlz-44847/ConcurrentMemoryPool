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
static const size_t NUM_PAGES = 129;//PageCache��ͬҳ���ɵĹ�ϣͰ������n�Ź�ϣͰ��ʾnҳ��0�ű�����
static const size_t PAGE_SHIFT = 13;//1 page = 2^13 Byte

#ifdef _WIN64 //x64������_WIN32��_WIN64���ж��壬������Ҫ��_WIN64��Ϊ��һ��
//64λ��������ַ�ռ�2^64����8KBΪһҳ����ҳ��Ϊ2^64/2^13=2^51
typedef unsigned long long PAGE_ID; 
#elif _WIN32 //x86������ֻ��_WIN32�Ķ���
//32λ��������ַ�ռ�2^32����8KBΪһҳ����ҳ��Ϊ2^32/2^13=2^19
typedef size_t PAGE_ID;
#endif

// VirtualAlloc��һ��Windows API�������ú����Ĺ������ڵ��ý��̵����ַ�ռ�,Ԥ�������ύһ����ҳ
// ֱ��ȥ���ϰ�ҳ����ռ�
inline static void* SystemAlloc(size_t kpage) //1ҳ=8KB
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	//VirtualAlloc���صĵ�ַ��64KB����ģ����Զ�8KBҲ�Ƕ����
#else
	//Linux����ʹ��brk��mmap��
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

// �ͷ�VirtualAlloc����Ŀռ�
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//Linux����ʹ��sbrk��unmmap��
#endif
}

//����С�ڴ�ָ�룬���ظ��ڴ�ǰָ�������ܹ���64λ/32λ�������ֽڵ�ֵ�������ص�ǰ����next
static inline void*& NextObj(void* obj)
{
	return *(void**)obj;
}

//�����зֳ�����С�������������
class FreeList
{
public:
	void Push(void* obj)//ͷ��
	{
		assert(obj);

		//*(void**)obj = _freeList;
		NextObj(obj) = _freeList;
		_freeList = obj;

		++_size;
	}

	void PushRange(void* start, void* end, size_t n)//��Χ����
	{
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	void* Pop()//ͷɾ��ʵ������ȡ�����������׸�����
	{
		assert(_freeList);

		void* obj = _freeList;
		_freeList = NextObj(obj);

		--_size;

		return obj;
	}

	//PopRangeʵ���Ǵ����������ͷ��ȡ��n������
	//start��endΪ����Ͳ�������ʾȡ����һϵ�ж���ĵ�һ�������һ���ĵ�ַ
	void PopRange(void*& start, void*& end, size_t n)//��Χɾ����ͷɾn������
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

//�����С����ӳ�����
class SizeClass
{
	//�����ֽ���					���뷽ʽ					ռ��freelist��ϣͰ�ţ���1~8�ֽ��ڴ���ɵ�0��Ͱ��9~16�ֽ��ڴ���ɵ�1��Ͱ��129~144�ֽ��ڴ���ɵ�16��Ͱ��
	//(0,128]					8 Byte����				[0,16)
	//(128,1024]				16 Byte����				[16,72)
	//(1024,8*1024]				128 Byte����				[72,128)
	//(8*1024,64*1024]			1024 Byte����			[128,184)
	//(64*1024,256*1024]		8*1024 Byte����			[184,280)
	// 
	//�ڲ���Ƭ�����䳬��������ڴ����������Ƭ���˷ѱ��ʣ�
	//8�ֽڶ���		����˷��ֽڱȣ�7/8=87.5%				ƽ���˷��ֽ�������128/8��*��1+7��*7/2=448							�������ֽڣ�8*8*(1+16)*16/2=78336			ƽ���˷�ռ�ȣ�5.7%
	//16�ֽڶ���		����˷��ֽڱȣ�15/144=10.4%			ƽ���˷��ֽ�������1024-128��/16 *��1+15��*15/2=6720					�������ֽڣ�16*16(8+64)*56/2=516096		ƽ���˷�ռ�ȣ�1.3%
	//128�ֽڶ���		����˷��ֽڱȣ�127/1152=11.0%			ƽ���˷��ֽ�������64*1024-8*1024��/128 *��1+127��*127/2=6720
	//1024�ֽڶ���	����˷��ֽڱȣ�1023/(9*1024)=11.1%	
	//8*1024�ֽڶ���	����˷��ֽڱȣ�(8*1024-1)/(72*1024)=11.1%	
	//
	//����˷ѱ��ʽӽ�1/9
	//����С��8�ֽڵ������˷ѽϴ󣬵��������ڴ�鵣�������������д洢��ַ�Ĺ��ܣ��������ٴ�СΪ8�ֽ�

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
	//���ݴ����ֽ���ȷ���������Ҫ���ڴ��С
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
		else //�����ڴ泬��256KB������PageCache��ϵͳ���룬����ҳΪ��λ����
		{
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}
	//���ݴ����ֽ���ȷ�������ڴ��������������ϣͰ��
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

	//ThreadCacheÿһ�δ�CentralCache�л�õ��ڴ��������
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);

		int num = MAX_BYTES / size;
		//һ���Ի�ȡ���ڴ��������ֵ������[2,512]֮��
		//���ڴ��ڴ����룬һ����������������Խ���
		//����С�ڴ����룬һ����������������Խ϶�
		if (num < 2) num = 2;
		else if (num > 512) num = 512;
		return num;
	}

	//������ڲ�ͬ�Ķ���size��С��span��PageCache�����ҳ��
	//����8B�����룬��Ҫ512*8/(8*1024)=0.5ҳ��̧��Ϊ1ҳ
	//����16B�����룬��Ҫ512*16/(8*1024)=1ҳ
	//����256KB�����룬��Ҫ2*256*1024/(8*1024)=64ҳ
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size); //size��С������Ҫ���ڴ��������
		size_t npage = num * size; //����size��С������Ҫ�����ڴ��С

		npage >>= PAGE_SHIFT; //������Ҫ�����ڴ��С��������Ҫ��ҳ��
		if (npage == 0) npage = 1;

		return npage;
	}
};

//����������ҳ����ڴ�Ľṹ
struct Span
{
	PAGE_ID _pageId = 0;//����ڴ���ʼҳҳ��
	size_t _n = 0;//����ڴ��ҳ��

	//˫������ṹ���������ɾ�����
	Span* _next = nullptr;
	Span* _prev = nullptr;

	size_t _useCount = 0;//�зֺõ�С���ڴ汻�����thread cache�ļ���
	void* _freeList = nullptr;//��֯�����кõ�С���ڴ����������

	bool _isUse = false; //�Ƿ�ʹ�ã����ڸ���span��PageCache�еĺϲ�����ҳ����
	//��span���÷�����PageCache��
	//��span���ɲ�����CentralCache֮�󣬵�span������ʹ��֮ǰ
	// ��NewSpan֮��_pageMtx��������FetchRangeObj�ж�_useCount�޸�֮�䣬���ڼ�û��_pageMtx�������ڱ��ϲ����̷߳��գ�
	//�������������_useCount����0����Ҫ��һ����Ա��������������span�Ƿ�����

	size_t _objSize = 0;//�кõ�С����Ĵ�С�����ͷ��ڴ�ʱʹ��
};

//��ͷ˫��ѭ������
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
	std::mutex _mtx;//Ͱ����������߳�ͬʱ����CentralCache�Ĳ�ͬ��Ͱ����������߳�ͬʱ����ͬһ��Ͱ
};