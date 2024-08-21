#pragma once
#include "lib.h"
#include <string>
#include <common.h>
#include <format>
#include <Hw.h>
#include <functional>
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

		void free()
		{
			if (m_pAllocator)
				m_pAllocator->free(this);
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
			memset(block, 0, sizeof(HeapBlock) + size);

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

			return block->getMemoryBlock();
		}

		return nullptr;
	}

	HeapBlock* getBlock(void* mem)
	{
		return (HeapBlock*)((char*)mem - 0x10);
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

	void free(void* block)
	{
		auto heapBlock = getBlock(block);
		if (heapBlock)
			heapBlock->free();
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

	void shutdown()
	{
		if (!m_Heap)
			return;

		for (auto block = m_pFirst; block; block = block->m_pNext)
		{
			block->free();
		}

		HeapDestroy(m_Heap);

		m_pFirst = nullptr;
		m_pLast = nullptr;

		m_nAllocated = 0;
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

namespace Utils
{
	inline char* formatPath(char* buffer)
	{
		while (auto chr = strchr(buffer, '/'))
			*chr = '\\';

		return buffer;
	}

	inline char* formatPath(const char* buffer)
	{
		static char buff[MAX_PATH];
		memset(buff, 0, MAX_PATH);

		strcpy(buff, buffer);

		while (auto chr = strchr(buff, '/'))
			*chr = '\\';

		return buff;
	}

	/// <summary>
	/// Game implementation of format path
	/// </summary>
	/// <param name="buffer">Pointer to buffer</param>
	/// <param name="bufferSize">Size of buffer</param>
	/// <param name="data">Contains data about path</param>
	inline void formatPathGI(char* buffer, size_t bufferSize, const char* data)
	{
		((void(__cdecl*)(char*, size_t, const char*))(shared::base + 0x9F8090))(buffer, bufferSize, data);
	}

	inline std::string getProperSize(uint64_t fileSize)
	{
		static char buff[16];

		// do double since we don't know where the float will not display accurately

		auto sizeKB = (double)fileSize / 1024.0;
		auto sizeMB = sizeKB / 1024.0;
		auto sizeGB = float(sizeMB / 1024.0); // I don't think that it will ever reach 256GB or so on

		sprintf(buff, "%.1f%s", (double)fileSize > 1024.0 ? ((sizeKB > 1024.0) ? (sizeMB > 1024.0 ? sizeGB : sizeMB) : sizeKB) : fileSize, (double)fileSize > 1024.0 ? ((sizeKB > 1024.0) ? (sizeMB > 1024.0 ? "GB" : "MB") : "KB") : " Bytes");

		return std::format<char*>("{}", buff);
	}

	inline char* strlow(const char* buffer)
	{
		static char buff[MAX_PATH];

		memset(buff, 0, MAX_PATH);

		strcpy(buff, buffer); // Make a copy of string so we don't modify the buffer

		for (int i = strlen(buff); i > 0; i--)
			buff[i] = tolower(buff[i]);

		return buff;
	}

	inline char* strlow(char* buffer)
	{
		for (int i = strlen(buffer); i > 0; i--)
			buffer[i] = tolower(buffer[i]); // here we modify the buffer

		return buffer;
	}
}

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
		struct File;

		char m_name[64];
		int m_nPriority;
		bool m_bEnabled;
		bool m_bStarted;
		Hw::cFixedVector<struct File*> m_files;
		uint64_t m_nTotalSize;

		ModProfile()
		{
			this->m_name[0] = '\0';
			this->m_nPriority = 7; // def
			this->m_bEnabled = true;
			this->m_bStarted = false;
			this->m_nTotalSize = 0;

			memset(&this->m_files, 0, sizeof(this->m_files));
		}

		ModProfile(const char* szName)
		{
			strcpy(this->m_name, szName);
			this->m_nPriority = 7;
			this->m_bEnabled = true;
			this->m_bStarted = false;

			memset(&this->m_files, 0, sizeof(this->m_files));
		}

		~ModProfile()
		{
			this->m_name[0] = '\0';
			this->m_nPriority = -1;
			this->m_bEnabled = false;

			Shutdown();
		}

		void Shutdown()
		{
			if (m_files.m_pBegin)
				GetHeapManager()->free(m_files.m_pBegin);

			m_files.m_pBegin = nullptr;
			m_files.m_nSize = 0;
			m_files.m_nCapacity = 0;

			m_nTotalSize = 0;

			m_bStarted = false;
		}

		struct File
		{
			uint64_t m_nSize;
			char* m_path;
			bool m_bInSubFolder;

			File()
			{
				m_nSize = 0;
				m_path = nullptr;
				m_bInSubFolder = false;
			}

			File(uint64_t size, const char* path) : m_nSize(size)
			{
				m_path = nullptr;

				SetFilePath(path);
			}

			File(uint64_t size, char* path) : m_nSize(size)
			{
				m_path = nullptr;

				SetFilePath(path);
			}

			void SetFilePath(const char* path)
			{
				if (m_path)
				{
					GetHeapManager()->free(m_path);
					m_path = nullptr;
				}

				m_path = (char*)GetHeapManager()->allocate(strlen(path) + 1);

				strcpy(m_path, path);

				Utils::formatPath(m_path);
			}
		};
		
		// Can find file, or any file with extension
		File* FindAnyFile(const char* filename)
		{
			if (!filename || filename[0] == '\0' || !strcmp(filename, ""))
				return nullptr;

			for (auto& file : m_files)
			{
				if (strstr(file->m_path, Utils::formatPath(filename)))
					return file;
			}
			return nullptr;
		}

		// Finds file by the end of the filename(like: pl\\pl0010.dat -> pathToGame\\mods\\ModName\\pl\\pl0010.dat)
		File* FindFile(const char* filename)
		{
			if (!filename || filename[0] == '\0' || !strcmp(filename, ""))
				return nullptr;

			for (auto& file : m_files)
			{
				if (!strcmp(&file->m_path[strlen(file->m_path) - strlen(filename)], Utils::formatPath(filename)))
					return file;
			}
			return nullptr;
		}

		// Go through each file and callback
		void FileWalk(const std::function<void(File*)>& cb)
		{
			for (auto& file : m_files)
				cb(file);
		}

		void Startup();
		void Restart();
		void ReadFiles();
		void Save();
		void Load(const char* name);

		std::string getMyPath()
		{
			return std::format("{}\\{}", getModFolder().c_str(), m_name);
		};

	};

	inline lib::StaticArray<ModProfile*, MAX_MODS_PROFILE> Profiles;
}