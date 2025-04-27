#pragma once

#include "Common.h"

//�����ڴ�أ�����һ����ڴ棬�������뻮�ֲ��ṩ����Ҫ��С�ڴ�
//			�ڴ�ʵ������Ȼ��һ����ڴ棬ֻ�Ǳ���Ϊ��ͬ�ڴ�鹩���ڴ�����
//			ʹ����Ϸ������ڴ潻������������֯����׼���ٴη���
//����������ض������ڴ��������õ��ڴ�أ������ڴ����붼�Ƕ������ֽڴ�С
template<class T>
class ObjectPool {
public:
	T* New()
	{
		T* obj = nullptr;
		//����ʹ�����������еĵ�ַ
		if (_freeList)
		{
			//���������ͷɾ
			void* next = *(void**)_freeList;
			obj = (T*)_freeList;
			_freeList = next;
		} //��Ϊ��ģ�壬����һ������ӵ��һ���࣬ͬһ�ֶ�������Ŀռ���ͬ�����Կ���ֱ�ӽ��������ڴ渴�ö������Ǵ�С
		else
		{
			//ʣ���ڴ治�������㵱ǰ�Ŀռ����룬�򿪱����ڴ�
			//��ʱ˵���������ڴ涼��С�ڴ����ʽ����������������Կ��Է�������ԭ���Ĵ��ڴ治��
			if (_surplus < sizeof(T)) 
			{
				_surplus = 128 * 1024;
				//_memory = (char*)malloc(_surplus); //128KB
				_memory = (char*)SystemAlloc(_surplus >> 13);//128KB/(2^13)=16ҳ
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}
			obj = (T*)_memory;
			//����С�ڴ�ͷ��void*��С���ڴ�洢�������������next��ַ
			//���ԶԴ�С����void*���ڴ�����������void*
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_surplus -= objSize;
		}
		//��λnew����ָ��λ��obj���������T
		new(obj) T; //�����͹����˶��󣬲��ҵ�����T�Ĺ��캯�������ʼ��
		//���ǵ�New�������ڴ������߼�������Ҫ����һ������õĶ���������Ҫ��������ƺõ��ڴ��Ͻ��ж���Ĺ���
		
		if (this == nullptr)
		{
			int x = 1;
		}
		
		return obj;
	}

	void Delete(T* obj)
	{
		obj->~T();//��ʽ����T���������������������������ջؿռ䵽��������

		//�ͷź��С�ڴ�ͷ�嵽����������
		*(void**)obj = _freeList;	//С�ڴ�ռ��ǰ4/8���ֽڴ洢����next�ĵ�ַ
			//ǿתΪ����ָ���ٽ����ã�������Ϊָ�룬���ɼ��32λ��64λ�����²�ͬ�ĵ�ַ��С
		_freeList = obj;
	}

private:
	char* _memory = nullptr; //ָ���鶨���ڴ�Ŀɷ����ַ��ָ��
	size_t _surplus = 0; //���ڴ��ʣ���ֽ���

	void* _freeList = nullptr; //���������ͷָ�룬����ʹ����Ϸ�����С�ڴ��ĵ�ַ
};

