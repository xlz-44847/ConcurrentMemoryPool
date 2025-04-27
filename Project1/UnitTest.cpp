#include "ObjectPool.h"
#include "ConcurrentAlloc.h"

//采用树结构测试效率
struct TreeNode
{
	int _val;
	TreeNode* _left;
	TreeNode* _right;

	TreeNode()
		:_val(0)
		, _left(nullptr)
		, _right(nullptr)
	{}
};

void TestObjectPool()
{
	// 申请释放的轮次
	const size_t Rounds = 5;

	// 每轮申请释放多少次
	const size_t N = 100000;

	std::vector<TreeNode*> v1;
	v1.reserve(N);

	size_t begin1 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v1.push_back(new TreeNode);
		}
		for (int i = 0; i < N; ++i)
		{
			delete v1[i];
		}
		v1.clear();
	}

	size_t end1 = clock();

	std::vector<TreeNode*> v2;
	v2.reserve(N);

	ObjectPool<TreeNode> TNPool;
	size_t begin2 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v2.push_back(TNPool.New());
		}
		for (int i = 0; i < N; ++i)
		{
			TNPool.Delete(v2[i]);
		}
		v2.clear();
	}
	size_t end2 = clock();

	cout << "new cost time:" << end1 - begin1 << endl;
	cout << "object pool cost time:" << end2 - begin2 << endl;
}

void Alloc1()
{
	for (size_t i = 0; i < 5; i++)
	{
		void* ptr = ConcurrentAlloc(6);
	}
}
void Alloc2()
{
	for (size_t i = 0; i < 5; i++)
	{
		void* ptr = ConcurrentAlloc(7);
	}
}
void MultiAlloc1()
{
	std::vector<void*> v;
	for (size_t i = 0; i < 10; i++)
	{
		void* ptr = ConcurrentAlloc(7);
		v.push_back(ptr);
	}
	for (auto e : v)
	{
		ConcurrentFree(e);
	}
}
void MultiAlloc2()
{
	std::vector<void*> v;
	for (size_t i = 0; i < 15; i++)
	{
		void* ptr = ConcurrentAlloc(17);
		v.push_back(ptr);
	}
	for (auto e : v)
	{
		ConcurrentFree(e);
	}
}
void BigAlloc()
{
	void* p1 = ConcurrentAlloc(259 * 1024);
	ConcurrentFree(p1);
	void* p2 = ConcurrentAlloc(129 * 8 * 1024);
	ConcurrentFree(p2);
}
void TLSTest()
{
	std::thread t1(MultiAlloc1);
	//std::thread t2(MultiAlloc2);

	t1.join();
	//t2.join();
}
void TestConcurrentAlloc1()
{
	void* p1 = ConcurrentAlloc(6);
	void* p2 = ConcurrentAlloc(8);
	void* p3 = ConcurrentAlloc(3);
	void* p4 = ConcurrentAlloc(1);
	void* p5 = ConcurrentAlloc(6);
	void* p6 = ConcurrentAlloc(8);

	cout << p1 << endl;
	cout << p2 << endl;
	cout << p3 << endl;
	cout << p4 << endl;
	cout << p5 << endl;
	cout << p6 << endl;

	ConcurrentFree(p1);
	ConcurrentFree(p2);
	ConcurrentFree(p3);
	ConcurrentFree(p4);
	ConcurrentFree(p5);
	ConcurrentFree(p6);
}
void TestConcurrentAlloc2()
{
	for(int i =0;i<1024;i++)
	{
		void* p1 = ConcurrentAlloc(6);
		cout << p1 << endl;
	}
	void* p2 = ConcurrentAlloc(8);
	cout << p2 << endl;
}

//int main()
//{
//	//TestObjectPool();
//	//TLSTest();
//	TestConcurrentAlloc1();
//	//TestConcurrentAlloc2();
//	//BigAlloc();
//
//	return 0;
//}