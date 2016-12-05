/** \file
 * \brief Implementation of memory manager for more efficiently
 *        allocating small pieces of memory
 *
 * \author Carsten Gutwenger
 *
 * \par License:
 * This file is part of the Open Graph Drawing Framework (OGDF).
 *
 * \par
 * Copyright (C)<br>
 * See README.md in the OGDF root directory for details.
 *
 * \par
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 or 3 as published by the Free Software Foundation;
 * see the file LICENSE.txt included in the packaging of this file
 * for details.
 *
 * \par
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * \par
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see
 * http://www.gnu.org/copyleft/gpl.html
 */


#include <ogdf/basic/memory.h>


namespace ogdf {


struct PoolMemoryAllocator::PoolElement
{
	MemElemPtr m_gp;
	int        m_size;
};

struct PoolMemoryAllocator::BlockChain
{
	char m_fill[eBlockSize-sizeof(void*)];
	BlockChain *m_next;
};


PoolMemoryAllocator::PoolElement PoolMemoryAllocator::s_pool[eTableSize];
PoolMemoryAllocator::BlockChainPtr PoolMemoryAllocator::s_blocks;

#ifdef OGDF_DEBUG
size_t PoolMemoryAllocator::s_nettoAlloc;
#endif

#ifdef OGDF_MEMORY_POOL_NTS
PoolMemoryAllocator::MemElemPtr PoolMemoryAllocator::s_tp[eTableSize];
#else
std::mutex PoolMemoryAllocator::s_mutex;
thread_local PoolMemoryAllocator::MemElemPtr PoolMemoryAllocator::s_tp[eTableSize];
#endif

void PoolMemoryAllocator::cleanup()
{
#ifdef OGDF_DEBUG
	size_t memGlobalFreelist = unguardedMemGlobalFreelist();
	size_t memThreadFreelist = memoryInThreadFreeList();

	// check if all memory is correctly freed (if not we have a memory leak)
	OGDF_ASSERT(memGlobalFreelist + memThreadFreelist == s_nettoAlloc * OGDF_SIZEOF_POINTER);
#endif

	//----------
	BlockChainPtr p = s_blocks;
	while(p != nullptr) {
		BlockChainPtr pNext = p->m_next;
		free(p);
		p = pNext;
	}
}


void *PoolMemoryAllocator::allocate(size_t nBytes) {
	MemElemPtr &pFreeBytes = s_tp[nBytes];
	if (OGDF_LIKELY(pFreeBytes != nullptr)) {
		MemElemPtr p = pFreeBytes;
		pFreeBytes = p->m_next;
		p->m_next = nullptr;
		return p;
	} else {
		return fillPool(pFreeBytes,uint16_t(nBytes));
	}
}


void PoolMemoryAllocator::deallocate(size_t nBytes, void *p) {
	MemElemPtr &pFreeBytes = s_tp[nBytes];
	MemElemPtr(p)->m_next = pFreeBytes;
	pFreeBytes = MemElemPtr(p);
}


void PoolMemoryAllocator::deallocateList(size_t nBytes, void *pHead, void *pTail) {
	MemElemPtr &pFreeBytes = s_tp[nBytes];
	MemElemPtr(pTail)->m_next = pFreeBytes;
	pFreeBytes = MemElemPtr(pHead);
}



void PoolMemoryAllocator::flushPool()
{
#ifndef OGDF_MEMORY_POOL_NTS
	for(uint16_t nBytes = 1; nBytes < eTableSize; ++nBytes) {
		MemElemPtr &pHead = s_tp[nBytes];
		if(pHead != nullptr) {
			MemElemPtr pTail = pHead;
			int n = 1;

			while(pTail->m_next != nullptr) {
				pTail = pTail->m_next;
				++n;
			}

			MemElemPtr pOldHead = pHead;
			pHead = nullptr;

			enterCS();

			PoolElement &pe = s_pool[nBytes];

			pTail->m_next = pe.m_gp;
			pe.m_gp = pOldHead;
			pe.m_size += n;

			leaveCS();
		}
	}
#endif
}


void *PoolMemoryAllocator::fillPool(MemElemPtr &pFreeBytes, uint16_t nBytes)
{
	int nWords;
	int nSlices = slicesPerBlock(max(nBytes,(uint16_t)eMinBytes),nWords);

#ifdef OGDF_MEMORY_POOL_NTS
	pFreeBytes = allocateBlock();
#ifdef OGDF_DEBUG
	s_nettoAlloc += nWords * nSlices;
#endif
	makeSlices(pFreeBytes, nWords, nSlices);

#else
	enterCS();

	PoolElement &pe = s_pool[nBytes];
	if(pe.m_size >= nSlices) {
		MemElemPtr p = pFreeBytes = pe.m_gp;
		for(int i = 1; i < nSlices; ++i)
			p = p->m_next;

		pe.m_gp = p->m_next;
		pe.m_size -= nSlices;

		leaveCS();

		p->m_next = nullptr;

	} else {
		pFreeBytes = allocateBlock();
#ifdef OGDF_DEBUG
		s_nettoAlloc += nWords * nSlices;
#endif
		leaveCS();

		makeSlices(pFreeBytes, nWords, nSlices);
	}
#endif

	MemElemPtr p = pFreeBytes;
	pFreeBytes = p->m_next;
	return p;
}


PoolMemoryAllocator::MemElemPtr
PoolMemoryAllocator::allocateBlock()
{
	BlockChainPtr pBlock = static_cast<BlockChainPtr>( malloc(eBlockSize) );

	pBlock->m_next = s_blocks;
	s_blocks = pBlock;

	return reinterpret_cast<MemElemPtr>(pBlock);
}


void PoolMemoryAllocator::makeSlices(MemElemPtr pBlock, int nWords, int nSlices)
{
	do {
		pBlock = pBlock->m_next = pBlock+nWords;
	} while(--nSlices > 1);
	pBlock->m_next = nullptr;
}



size_t PoolMemoryAllocator::memoryAllocatedInBlocks()
{
	enterCS();

	size_t nBlocks = 0;
	for (BlockChainPtr p = s_blocks; p != nullptr; p = p->m_next)
		++nBlocks;

	leaveCS();

	return nBlocks * eBlockSize;
}


size_t PoolMemoryAllocator::unguardedMemGlobalFreelist()
{
	size_t bytesFree = 0;
	for (int sz = 1; sz < eTableSize; ++sz)
	{
		const PoolElement &pe = s_pool[sz];
		bytesFree += pe.m_size * sz;
	}

	return bytesFree;
}


size_t PoolMemoryAllocator::memoryInGlobalFreeList()
{
	enterCS();
	size_t bytesFree = unguardedMemGlobalFreelist();
	leaveCS();

	return bytesFree;
}


size_t PoolMemoryAllocator::memoryInThreadFreeList()
{
	size_t bytesFree = 0;
	for (int sz = 1; sz < eTableSize; ++sz)
	{
		MemElemPtr &p = s_tp[sz];
		for(; p != nullptr; p = p->m_next)
			bytesFree += sz;
	}

	return bytesFree;
}


void PoolMemoryAllocator::defrag()
{
	enterCS();

	int maxSize = 0;
	for(int sz = 1; sz < eTableSize; ++sz) {
		int size = s_pool[sz].m_size;
		maxSize = max(maxSize, size);
	}

	if(maxSize > 1) {
		MemElemPtr *a = new MemElemPtr[maxSize];

		for(int sz = 1; sz < eTableSize; ++sz)
		{
			PoolElement &pe = s_pool[sz];
			int n = pe.m_size;
			if(n > 1)
			{
				int i = 0;
				for(MemElemPtr p = pe.m_gp; p != nullptr; p = p->m_next)
					a[i++] = p;
				OGDF_ASSERT(i == n);
				std::sort(a, a+n);
				pe.m_gp = a[0];
				for(int i = 0; i < n-1; ++i) {
					a[i]->m_next = a[i+1];
				}
				a[n-1]->m_next = nullptr;
			}
		}
		delete [] a;
	}

	leaveCS();
}

}
