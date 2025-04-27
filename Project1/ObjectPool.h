#pragma once

#include "Common.h"

//定长内存池：申请一块大内存，根据申请划分并提供所需要的小内存
//			内存实际上仍然是一大块内存，只是被视为不同内存块供给内存申请
//			使用完毕返还的内存交由自由链表组织，并准备再次分配
//定长：针对特定长度内存申请设置的内存池，所有内存申请都是定长的字节大小
template<class T>
class ObjectPool {
public:
	T* New()
	{
		T* obj = nullptr;
		//优先使用自由链表中的地址
		if (_freeList)
		{
			//自由链表的头删
			void* next = *(void**)_freeList;
			obj = (T*)_freeList;
			_freeList = next;
		} //因为类模板，所以一个类型拥有一个类，同一种对象申请的空间相同，所以可以直接将返还的内存复用而不考虑大小
		else
		{
			//剩余内存不足以满足当前的空间申请，则开辟新内存
			//此时说明以往的内存都以小内存的形式被自由链表管理，所以可以放心撒开原来的大内存不管
			if (_surplus < sizeof(T)) 
			{
				_surplus = 128 * 1024;
				//_memory = (char*)malloc(_surplus); //128KB
				_memory = (char*)SystemAlloc(_surplus >> 13);//128KB/(2^13)=16页
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}
			obj = (T*)_memory;
			//由于小内存头部void*大小的内存存储的是自由链表的next地址
			//所以对大小不足void*的内存申请扩充至void*
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_surplus -= objSize;
		}
		//定位new：在指定位置obj处构造对象T
		new(obj) T; //这样就构造了对象，并且调用了T的构造函数对其初始化
		//我们的New着眼于内存分配的逻辑，由于要返回一个构造好的对象，所以需要在我们设计好的内存上进行对象的构造
		
		if (this == nullptr)
		{
			int x = 1;
		}
		
		return obj;
	}

	void Delete(T* obj)
	{
		obj->~T();//显式调用T的析构函数，待清理完对象后再收回空间到自由链表

		//释放后的小内存头插到自由链表中
		*(void**)obj = _freeList;	//小内存空间的前4/8个字节存储链表next的地址
			//强转为二级指针再解引用，其类型为指针，即可兼顾32位和64位机器下不同的地址大小
		_freeList = obj;
	}

private:
	char* _memory = nullptr; //指向大块定长内存的可分配地址的指针
	size_t _surplus = 0; //大内存的剩余字节数

	void* _freeList = nullptr; //自由链表的头指针，管理使用完毕返还的小内存块的地址
};

