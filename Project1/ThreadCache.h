#pragma once

#include "Common.h"

class ThreadCache
{
public:
	//����һ��size��С���ڴ�
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	//��ThreadCache������������һ���ڴ�鶼û�У�����CentralCache����һ��size��С�ڴ��
	void* FetchFromCentralCache(size_t index, size_t size);
	
	//�����ڴ��ͷŲ���������Ⱥ󣬷������������������ڴ浽CentralCache
	void ListTooLong(FreeList& list, size_t size);

private:
	//���ö����ϣͰ��ÿ����ϣͰ���ɲ�ͬ�ֽڷ�Χ���ڴ��
	FreeList _freeLists[NUM_FREELISTS];
};

//TLS����thread local storage
//����ÿ���߳�ӵ�б����Ķ�������,����ζ����Ȼ�����߳̿����ı�������ͬ����ÿ���߳̿��Դ洢�ͷ����Լ�������ֵ
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;