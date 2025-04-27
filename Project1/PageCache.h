#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

//����ģʽ----����ģʽ
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	//��ȡһ��kҳ��span
	Span* NewSpan(size_t k);

	//�����ڴ���󣬸���unordered_mapӳ��������Span*
	Span* MapObjToSpan(void* obj);

	//CentralCache���ؿ���span��PageCache��PageCache���ղ��ϲ�ҳ�����ڵ�span
	void ReleaseSpanToSpanCache(Span* span);

private:
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

	SpanList _spanLists[NUM_PAGES];

	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;//��¼ҳ�źͶ�Ӧspan��map�����ͨ���ڴ��Ѱ��������span��Ч��
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	ObjectPool<Span> _spanPool;//��PageCache��sSpan������ͨ��new������
	//Ϊ�˺�new��malloc�Ƚӿھ�Ե�����ö����ڴ�صķ�ʽ����Span����

public:
	std::mutex _pageMtx;//��PageCache����
	//PageCache�������������Ǹ���Ͱ����
	//������ΪPageCache�Ĺ����߼������漰�ܶ�Ͱ������2ҳspan��������Ҫ˳�Ų鿴3��4��5����ҳ����span����
	//���ʼPageCache��spanListΪ�գ���Ҫ��ϵͳ�����ڴ棬���ǵõ�һ��128ҳ��span
	//�ڸ���CentralCache�����ҳ��������Ϊ2������128ҳ��span�ֳ������֣�2ҳspan��126ҳspan��һ�����أ���һ�����ڶ�Ӧ��Ͱ��
	//�۸���CentralCache��span��usecount���������Ϊ0����˵���ṩ��ThreadCache��С�ڴ�ȫ������
	//	��ʱCentralCache�Ϳ��Խ����span����PageCache
	//  PageCacheͨ�����ҳ��ҳ�ţ��鿴��ǰ�����ڵ�ҳ�Ƿ���У��Դ˺ϲ��������ҳ������ڴ���Ƭ����

	//ʹ��������������Ͱ������Ϊ����������һ�ѣ���Ͱ������ҪƵ���ļ�������Ч�ʽϵ�

private:
	static PageCache _sInst;
};