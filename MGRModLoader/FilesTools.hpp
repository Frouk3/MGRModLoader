#pragma once

#ifndef FILES_TOOLS_HPP
#define FILES_TOOLS_HPP
#include "ModLoader.h"
#include <Hw.h>
#include <HwDvd.h>
#define TAKE_TIMER_SNAPSHOTS 0
#include "Timer.hpp"

namespace CriWare
{
	inline int iAvailableCPKs = 0;
	inline lib::StaticArray<Hw::cDvdCriFsBinder *, 128> aBinders;

	inline Hw::cDvdCriFsBinder* getFreeBinderWork()
	{
		Hw::cDvdCriFsBinder* pWork = new(&ModloaderHeap) Hw::cDvdCriFsBinder();

		if (!pWork)
			return nullptr;

		pWork->m_Type = Hw::cDvdCriFsBinder::TYPE_INVALID;
		pWork->m_BinderHandle = nullptr;

		pWork->m_BindStatus = CRIFSBINDER_STATUS_NONE;
		pWork->m_Prio = -1;

		aBinders.pushBack(pWork);

		return pWork;
	}

	inline void freeBinderWork(Hw::cDvdCriFsBinder *binderWork)
	{
		if (binderWork->m_BinderHandle)
		{
			CriFsBinderHn_free(binderWork->m_BinderHandle);
			binderWork->m_BinderHandle = nullptr;
		}

		binderWork->m_BindStatus = CRIFSBINDER_STATUS_NONE;
		binderWork->m_Prio = -1;

		for (auto& work : aBinders)
		{
			if (work == binderWork)
				aBinders.erase(&work);
		}

		operator delete(binderWork, &ModloaderHeap);
	}
}

namespace DataArchiveTools
{
	struct FileStructure
	{
		void* m_file;
		size_t m_fileSize;
		size_t m_fileindex = -1;
		Utils::String m_filename;

		FileStructure()
		{
			m_file = nullptr;
			m_fileSize = 0;
		}

		FileStructure(void* filedata, size_t filesize, size_t fileindex, const char* filename) : m_file(filedata), m_fileSize(filesize), m_fileindex(fileindex), m_filename(filename) {};
	};

	struct FilesReplacer
	{
		Hw::cFmerge* m_dataholder;
		size_t m_datasize;
		std::vector<FileStructure> m_files;

		FilesReplacer()
		{
			m_dataholder = nullptr;
		}

		FilesReplacer(Hw::cFmerge* dataholder, size_t datasize) : m_dataholder(dataholder), m_datasize(datasize)
		{
			
		}

		~FilesReplacer()
		{
			for (FileStructure& file : m_files)
				operator delete(file.m_file, &ModloaderHeap);

			m_files.clear();
		}
	};

	inline unsigned int align(unsigned int align, unsigned int length)
	{
		return ~(align - 1) & (length + align - 1);
	}

	inline size_t replaceFiles(FilesReplacer& files, Hw::HW_ALLOC_MODE mode)
	{
		
	}

	inline void appendFile(const std::pair<Hw::cFmerge*, const size_t>& pr, const char* filename, const std::pair<const void*, const size_t>& filePair, Hw::cHeap* allocator, int alignment)
	{

	}
}

inline size_t ReplaceDataArchiveFile(Hw::cFmerge* holder, size_t holder_size, const Utils::String& filename, Hw::cHeap* allocator, Hw::HW_ALLOC_MODE mode)
{
	
}

#endif 