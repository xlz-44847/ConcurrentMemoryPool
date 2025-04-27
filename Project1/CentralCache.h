#pragma once

#include "Common.h"

//����ģʽ--����ģʽ
class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	//��һ��CentralCache��ָ����SpanList��ϣͰ�У���ȡһ���ǿյ�Span
	Span* GetOneSpan(SpanList& list, size_t size);

	//ϣ�������Ļ����ȡbatchNum��size��С�Ķ�����̻߳���
	//start��end������Ͳ�����start���ȡ���������ڴ���е�һ������ĵ�ַ��end�������һ������ĵ�ַ
	//�������Ļ��������������û���㹻���ڴ�飬����Ҫ��ȡ�������Ժ�������ֵΪʵ�ʻ�õ��ڴ����
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	//��ThreadCache��һ�������Ķ��󷵻���span
	void ReleaseListToSpans(void* start, size_t size);

private:
	CentralCache()
	{}
	CentralCache(const CentralCache&) = delete;
	CentralCache& operator=(const CentralCache&) = delete;

	//CentralCache�Ķ���ӳ������ThreadCache��ͬ����ϣͰ����һ��
	SpanList _spanLists[NUM_FREELISTS];

	static CentralCache _sInst;
};