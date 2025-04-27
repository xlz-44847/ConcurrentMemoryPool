#pragma once
#include"Common.h"
//基数树——分层索引
// 需求是给定一个页号，得到其所在的span地址，即PAGE_ID->Span*
// 基数树采取一种类似于直接地址映射的方法实现时间复杂度O(K)访问，k为基数树的层数
// 
//一层基数树
// 一层基数树仅有一层，即一个数组，可以通过vector[页号]的方式O(1)得到Span*
// 在32位系统下，计算机内存共有2^32/(8*1024)=2^19个页，为了可以直接映射，所以vector的长度就是2^19
// vector每个元素都是一个地址，所以总内存开销为2^19*4=2^21Byte大小，这仅是32位系统下，如果是64位则更加庞大，需要考虑更多层的基数树
// 模板中的BITS指的是页号的上限的2的对数，即页号范围为0~2^BITS
// 
//二层基数树
// 二层基数树有两层，第一层数组的元素是第二层的数组，第二层的数组元素才是Span*，需要通过vector[页号高5位][页号低14位]
// 此处是规定了第一层的长度为2^5，即将5个二进制位视为一个整体，所有高五位地址相同的页号被存在同一个二层数组中
// 这样做第二层的每个数组大小为2^14*4=2^16，因为一共有2^5个这样的二层数组，所以总的内存占用并没有比一层基数树有所下降
// 但是因为是多层，可以选择需要的时候再申请数组，如果不需要可以暂时不申请第二层数组，于是比一层基数树有一些空间暂时的节省
// 
//三层基数树
// 顾名思义有三层，结构就是二层基数树多了一层
// 
// Single-level array
template <int BITS>
class TCMalloc_PageMap1 {
private:
	static const int LENGTH = 1 << BITS;
	void** array_;

public:
	typedef uintptr_t Number;

	//explicit TCMalloc_PageMap1(void* (*allocator)(size_t)) {
	explicit TCMalloc_PageMap1() {
		//array_ = reinterpret_cast<void**>((*allocator)(sizeof(void*) << BITS));
		size_t size = sizeof(void*) << BITS;
		size_t alignSize = SizeClass::RoundUp(size);
		array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);
		memset(array_, 0, sizeof(void*) << BITS);
	}

	// Return the current value for KEY.  Returns NULL if not yet set,
	// or if k is out of range.
	void* get(Number k) const {
		if ((k >> BITS) > 0) {
			return NULL;
		}
		return array_[k];
	}

	// REQUIRES "k" is in range "[0,2^BITS-1]".
	// REQUIRES "k" has been ensured before.
	//
	// Sets the value 'v' for key 'k'.
	void set(Number k, void* v) {
		array_[k] = v;
	}
};

// Two-level radix tree
template <int BITS>
class TCMalloc_PageMap2 {
private:
	// Put 32 entries in the root and (2^BITS)/32 entries in each leaf.
	static const int ROOT_BITS = 5;
	static const int ROOT_LENGTH = 1 << ROOT_BITS;

	static const int LEAF_BITS = BITS - ROOT_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	// Leaf node
	struct Leaf {
		void* values[LEAF_LENGTH];
	};

	Leaf* root_[ROOT_LENGTH];             // Pointers to 32 child nodes
	void* (*allocator_)(size_t);          // Memory allocator

public:
	typedef uintptr_t Number;

	//explicit TCMalloc_PageMap2(void* (*allocator)(size_t)) {
	explicit TCMalloc_PageMap2() {
		//allocator_ = allocator;
		memset(root_, 0, sizeof(root_));

		PreallocateMoreMemory();
	}

	void* get(Number k) const {
		const Number i1 = k >> LEAF_BITS;
		const Number i2 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 || root_[i1] == NULL) {
			return NULL;
		}
		return root_[i1]->values[i2];
	}

	void set(Number k, void* v) {
		const Number i1 = k >> LEAF_BITS;
		const Number i2 = k & (LEAF_LENGTH - 1);
		ASSERT(i1 < ROOT_LENGTH);
		root_[i1]->values[i2] = v;
	}

	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> LEAF_BITS;

			// Check for overflow
			if (i1 >= ROOT_LENGTH)
				return false;

			// Make 2nd level node if necessary
			if (root_[i1] == NULL) {
				//Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));
				//if (leaf == NULL) return false;
				static ObjectPool<Leaf>	leafPool;
				Leaf* leaf = (Leaf*)leafPool.New();

				memset(leaf, 0, sizeof(*leaf));
				root_[i1] = leaf;
			}

			// Advance key past whatever is covered by this leaf node
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}

	void PreallocateMoreMemory() {
		// Allocate enough to keep track of all possible pages
		Ensure(0, 1 << BITS);
	}
};

// Three-level radix tree
template <int BITS>
class TCMalloc_PageMap3 {
private:
	// How many bits should we consume at each interior level
	static const int INTERIOR_BITS = (BITS + 2) / 3; // Round-up
	static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS;

	// How many bits should we consume at leaf level
	static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	// Interior node
	struct Node {
		Node* ptrs[INTERIOR_LENGTH];
	};

	// Leaf node
	struct Leaf {
		void* values[LEAF_LENGTH];
	};

	Node* root_;                          // Root of radix tree
	void* (*allocator_)(size_t);          // Memory allocator

	Node* NewNode() {
		Node* result = reinterpret_cast<Node*>((*allocator_)(sizeof(Node)));
		if (result != NULL) {
			memset(result, 0, sizeof(*result));
		}
		return result;
	}

public:
	typedef uintptr_t Number;

	explicit TCMalloc_PageMap3(void* (*allocator)(size_t)) {
		allocator_ = allocator;
		root_ = NewNode();
	}

	void* get(Number k) const {
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 ||
			root_->ptrs[i1] == NULL || root_->ptrs[i1]->ptrs[i2] == NULL) {
			return NULL;
		}
		return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3];
	}

	void set(Number k, void* v) {
		ASSERT(k >> BITS == 0);
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
	}

	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);
			const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);

			// Check for overflow
			if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH)
				return false;

			// Make 2nd level node if necessary
			if (root_->ptrs[i1] == NULL) {
				Node* n = NewNode();
				if (n == NULL) return false;
				root_->ptrs[i1] = n;
			}

			// Make leaf node if necessary
			if (root_->ptrs[i1]->ptrs[i2] == NULL) {
				Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));
				if (leaf == NULL) return false;
				memset(leaf, 0, sizeof(*leaf));
				root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);
			}

			// Advance key past whatever is covered by this leaf node
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}

	void PreallocateMoreMemory() {
	}
};