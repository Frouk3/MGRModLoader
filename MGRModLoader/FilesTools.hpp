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
				aBinders.erase(work);
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
		if (files.m_files.empty() || files.m_dataholder->m_data->m_FileNum <= 0)
			return files.m_datasize;

		size_t holdersize = files.m_datasize;

		Hw::cFmerge holder;

		holder.m_data = (Hw::FmergeHeader*)ModloaderHeap.alloc(holdersize, 0x1000, mode, 0);
		if (!holder.m_data)
			return holdersize;

		memcpy(holder.m_data, files.m_dataholder->m_data, files.m_datasize);

		for (FileStructure& file : files.m_files)
		{
			if (!file.m_file)
				continue;

			size_t fileSize = file.m_fileSize;
			size_t fileIndex = file.m_fileindex;

			if (fileIndex == -1)
				continue;
		
			size_t* sizes = (size_t*)(holder.m_data + holder.m_data->m_SizeOffs);

			fileSize = align(16, fileSize) - sizes[fileIndex];

			sizes[fileIndex] = file.m_fileSize;

			holdersize += fileSize;
		}

		if (holder.m_data->m_FileNum > 1)
		{
			for (size_t i = 1; i < holder.m_data->m_FileNum; i++)
			{
				size_t* sizes = (size_t*)(holder.m_data + holder.m_data->m_SizeOffs);
				size_t* positions = (size_t*)(holder.m_data + holder.m_data->m_OffsetTblOffs);

				positions[i] = align(16, positions[i - 1] + align(16, sizes[i - 1])); // double align to be sure

				FileStructure* file = nullptr;

				for (FileStructure& f : files.m_files)
				{
					if (f.m_fileindex == i)
					{
						file = &f;
						break;
					}
				}

				if (!file)
					memcpy(holder.m_data + positions[i], files.m_dataholder->getFileIndexData(i), files.m_dataholder->getFileIndexSize(i));
				else
					memcpy(holder.m_data + positions[i], file->m_file, file->m_fileSize);
			}
		}
		else // just one file
		{
			size_t* sizes = (size_t*)(holder.m_data + holder.m_data->m_SizeOffs);
			size_t* positions = (size_t*)(holder.m_data + holder.m_data->m_OffsetTblOffs);
			FileStructure* file = nullptr;
			for (FileStructure& f : files.m_files)
			{
				if (f.m_fileindex == 0)
				{
					file = &f;
					break;
				}
			}
			if (!file)
				memcpy(holder.m_data + positions[0], files.m_dataholder->getFileIndexData(0), files.m_dataholder->getFileIndexSize(0));
			else
				memcpy(holder.m_data + positions[0], file->m_file, file->m_fileSize);
		}

		if (holder.m_data)
		{
			operator delete(files.m_dataholder->m_data, &ModloaderHeap);
			files.m_dataholder->m_data = holder.m_data;
		}

		return holdersize;
	}

	inline void appendFile(const std::pair<Hw::cFmerge*, const size_t>& pr, const char* filename, const std::pair<const void*, const size_t>& filePair, Hw::cHeap* allocator, int alignment)
	{

	}
}

inline size_t ReplaceDataArchiveFile(Hw::cFmerge* holder, size_t holder_size, const Utils::String& filename, Hw::cHeap* allocator, Hw::HW_ALLOC_MODE mode)
{
	using namespace DataArchiveTools;
	size_t holdersize = holder_size;
	Utils::String foldername = filename;

	if (char* ext = foldername.strrchr('.'); ext)
		*ext = 0;

	foldername.shrink_to_fit();

	FilesReplacer filesReplacer(holder, holder_size);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		FileSystem::Directory* dir = prof->FindDirectory(foldername);

		if (!dir)
			continue;

		size_t it = 0;
		CTimer timer;
		for (const char* fileName = holder->getFileIndexFileName(0); it < holder->m_data->m_FileNum; fileName = holder->getFileIndexFileName(++it))
		{
			FileSystem::File* file = dir->FindFile(fileName);

			Utils::String fld = foldername / fileName;
			if (char* chr = fld.strrchr('.'); chr)
				*chr = 0;

			fld.shrink_to_fit();
			if (!strcmp((char*)holder->getFileIndexData(it), "DAT\0"))
			{
				if (FileSystem::Directory* dir = prof->FindDirectory(fld); !file && dir)
				{
					size_t size = holder->getFileIndexSize(it);
					LOGINFO("[DATAWORK] We have a DAT here %s, fld = %s", fileName, fld.c_str());
					void* fileData = ModloaderHeap.alloc(align(16, size), 0x1000, Hw::HW_ALLOC_PHYSICAL, 0);
					if (fileData)
					{
						memcpy(fileData, holder->getFileIndexData(it), size);
						Hw::cFmerge holder;
						holder.m_data = (Hw::FmergeHeader*)fileData;
						size = ReplaceDataArchiveFile(&holder, size, foldername / fileName, &ModloaderHeap, Hw::HW_ALLOC_PHYSICAL);
						fileData = holder.m_data;
						filesReplacer.m_files.push_back(FileStructure(fileData, size, it, fileName));
					}
				}
				else if (file && dir)
				{
					LOGWARNING("[DATAWORK] There's priority for packed files, skipping to file %s without repacking", fileName);
				}
			}
			// LOGINFO("iterating through %s", fileName);

			if (!file || !file->m_filesize)
				continue;

			LOGINFO("[DATAWORK] Found unpacked %s[%s] in %s", fileName, Utils::getProperSize(file->m_filesize).c_str(), prof->m_name.c_str());
			void* fileData = ModloaderHeap.alloc(align(16, file->m_filesize), 0x1000, Hw::HW_ALLOC_PHYSICAL, 0);
			if (fileData && file->read(fileData))
			{
				FileStructure* flstr = nullptr;
				for (FileStructure& str : filesReplacer.m_files)
					if (str.m_fileindex == it) flstr = &str;

				if (flstr)
				{
					LOGERROR("%s file was already added by another mod, ignoring %s...", fileName, fileName);
					operator delete(fileData, &ModloaderHeap);
					fileData = nullptr;
				}
				else
				{
					filesReplacer.m_files.push_back(FileStructure(fileData, file->m_filesize, it, fileName));
				}
			}
			else
			{
				if (fileData)
				{
					operator delete(fileData, &ModloaderHeap);
					fileData = nullptr;
				}
			}
		}
	}
	if (!filesReplacer.m_files.empty())
		holdersize = replaceFiles(filesReplacer, mode);

	return holdersize;
}

#endif 