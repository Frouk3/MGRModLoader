#pragma once
#include "lib.h"
#include <string>
#include <common.h>
#include <format>
#include <Hw.h>
#pragma warning(disable : 4996)

#define MAX_MODS_PROFILE 1024

struct cHeapManager
{
	struct HeapBlock
	{
		int m_nSize;
		cHeapManager* m_pAllocator;
		HeapBlock* m_pNext;
		HeapBlock* m_pPrev;

		HeapBlock()
		{
			m_nSize = 0;
			m_pAllocator = nullptr;
			m_pNext = nullptr;
			m_pPrev = nullptr;
		}

		void *getMemoryBlock() // where the allocation data stored
		{
			return (void*)((char*)this + sizeof(HeapBlock));
		}
	};

	HANDLE m_Heap;
	int m_nAllocated;
	char* m_TargetAllocation;
	HeapBlock* m_pFirst;
	HeapBlock* m_pPrev;
	HeapBlock* m_pLast;

	cHeapManager()
	{
		m_Heap = NULL;
		m_nAllocated = 0;
		m_pFirst = nullptr;
		m_pPrev = nullptr;
		m_pLast = nullptr;
		m_TargetAllocation = nullptr;
	}

	void *allocate(size_t size, bool bUseGameHeap = false)
	{
		if (bUseGameHeap)
			return AllocateMemory(size);

		if (!m_Heap)
			return nullptr;

		HeapBlock* block = (HeapBlock*)HeapAlloc(m_Heap, 1u, sizeof(*block) + size);

		if (block)
		{
			HeapBlock* prevBlock = m_pPrev;

			if (!m_pFirst)
				m_pFirst = m_pPrev;

			block->m_pAllocator = this;
			block->m_nSize = size + sizeof(HeapBlock);

			block->m_pNext = 0;
			block->m_pPrev = prevBlock ? prevBlock : 0;
			if (prevBlock)
				prevBlock->m_pNext = block;

			m_pPrev = block;

			m_pLast = block;

			m_nAllocated += block->m_nSize;

			memset(block->getMemoryBlock(), 0, size);

			return block->getMemoryBlock();
		}

		return nullptr;
	}

	template <typename tC>
	tC *allocate()
	{
		return (tC*)allocate(sizeof(tC));
	}

	void* allocateNoBlock(size_t size)
	{
		if (!m_Heap)
			return nullptr;

		return HeapAlloc(m_Heap, 1u, size);
	}

	void free(HeapBlock* block, bool bGameFreeMemory = false)
	{
		if (bGameFreeMemory)
		{
			FreeMemory((void*)block, 0u);
			return;
		}

		if (!m_Heap)
			return;

		if (block)
		{
			if (block->m_pPrev)
				block->m_pPrev->m_pNext = block->m_pNext;

			if (block->m_pNext)
				block->m_pNext->m_pPrev = block->m_pPrev;

			m_pPrev = block->m_pPrev;

			m_nAllocated -= block->m_nSize;

			if (block == m_pFirst)
				m_pFirst = block->m_pNext ? block->m_pNext : nullptr;

			if (m_pLast == block)
				m_pLast = block->m_pPrev ? block->m_pPrev : nullptr;

			HeapFree(m_Heap, 1u, block);
		}
	}

	void create(const char* targetAllocation)
	{
		if (m_Heap)
			return;

		m_nAllocated = 0;
		m_Heap = HeapCreate(0u, 0u, 0u);
		m_pFirst = nullptr;
		m_pPrev = nullptr;

		m_TargetAllocation = (char*)targetAllocation;
	}
};

inline cHeapManager* GetHeapManager();

extern inline void __cdecl dbgPrint(const char* fmt, ...);

#define LOG(x, ...) \
	dbgPrint("[Mod Loader] " x, __VA_ARGS__);

#define LOGERROR(x, ...) \
	LOG("[ERROR] " x, __VA_ARGS__)

#define LOGINFO(x, ...) \
	LOG("[INFO ] " x, __VA_ARGS__)

namespace ModLoader
{
	inline char path[MAX_PATH];
	inline bool bInitFailed = false;
	inline bool bInit = false;
	inline bool bIgnoreScripts = false;
	inline bool bIgnoreDATLoad = false;
	inline bool bEnableLogging = true;

	void startup();
	void SortProfiles();
	void Load();
	void Save();
	std::string getModFolder();

	struct ModProfile
	{
		char m_name[64];
		int m_nPriority;
		bool m_bEnabled;

		ModProfile()
		{
			this->m_name[0] = '\0';
			this->m_nPriority = 7; // def
			this->m_bEnabled = true;
		}

		ModProfile(const char* szName)
		{
			strcpy(this->m_name, szName);
			this->m_nPriority = 7;
			this->m_bEnabled = true;
		}

		~ModProfile()
		{
			this->m_name[0] = '\0';
			this->m_nPriority = -1;
			this->m_bEnabled = false;
		}

		void Save();
		void Load(const char* name);

		std::string getMyPath()
		{
			return std::format("{}\\{}", getModFolder().c_str(), m_name);
		};

	};

	inline lib::StaticArray<ModProfile*, MAX_MODS_PROFILE> Profiles;
}