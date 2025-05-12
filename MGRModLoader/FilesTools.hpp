#pragma once

#ifndef FILES_TOOLS_HPP
#define FILES_TOOLS_HPP
#include "ModLoader.h"
#include <HkDataManagerImplement.h>
#define TAKE_TIMER_SNAPSHOTS 0
#include "Timer.hpp"

namespace CriWare
{
	inline int iAvailableCPKs = 0;
	inline lib::StaticArray<CriFsBinderWork *, 128> aBinders;

	inline CriFsBinderWork* getFreeBinderWork()
	{
		CriFsBinderWork* pWork = new(&ModloaderHeap) CriFsBinderWork();

		if (!pWork)
			return nullptr;

		pWork->m_nStatus = 0;
		pWork->m_CriBinderHn = nullptr;

		pWork->m_BindStatus = 6;
		pWork->m_nPriority = -1;

		aBinders.push_back(pWork);

		return pWork;
	}

	inline void freeBinderWork(CriFsBinderWork *binderWork)
	{
		if (binderWork->m_CriBinderHn)
		{
			CriFsBinderHn_free(binderWork->m_CriBinderHn);
			binderWork->m_CriBinderHn = nullptr;
		}

		binderWork->m_nStatus = 0;
		binderWork->m_nPriority = -1;

		for (auto& work : aBinders)
		{
			if (work == binderWork)
				aBinders.remove(work);
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
		DataArchiveHolder* m_dataholder;
		size_t m_datasize;
		std::vector<FileStructure> m_files;

		FilesReplacer()
		{
			m_dataholder = nullptr;
		}

		FilesReplacer(DataArchiveHolder* dataholder, size_t datasize) : m_dataholder(dataholder), m_datasize(datasize)
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
		return align - (length % align);
	}

	inline size_t replaceFiles(FilesReplacer& files, size_t alignment)
	{
		if (files.m_files.empty() || files.m_dataholder->asEntry()->m_nAmountOfFiles <= 0)
			return files.m_datasize;

		size_t holdersize = files.m_datasize;

		DataArchiveHolder holder;

		holder.m_data = (char*)ModloaderHeap.AllocateMemory(holdersize, 0x1000, alignment, 0);
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
		
			size_t* sizes = (size_t*)(holder.m_data + holder.asEntry()->m_nSizesOffset);

			fileSize += align(16, fileSize) - sizes[fileIndex];

			sizes[fileIndex] = file.m_fileSize;

			holdersize += fileSize;
		}

		if (holder.asEntry()->m_nAmountOfFiles > 1)
		{
			for (int i = 1; i < holder.asEntry()->m_nAmountOfFiles; i++)
			{
				size_t* sizes = (size_t*)(holder.m_data + holder.asEntry()->m_nSizesOffset);
				size_t* positions = (size_t*)(holder.m_data + holder.asEntry()->m_nPositionOffset);

				positions[i] = positions[i - 1] + sizes[i - 1] + align(16, positions[i - 1] + sizes[i - 1]);

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
					memcpy(holder.m_data + positions[i], files.m_dataholder->getFiledata(i), files.m_dataholder->getSize(i));
				else
					memcpy(holder.m_data + positions[i], file->m_file, file->m_fileSize);
			}
		}
		else // just one file
		{
			size_t* sizes = (size_t*)(holder.m_data + holder.asEntry()->m_nSizesOffset);
			size_t* positions = (size_t*)(holder.m_data + holder.asEntry()->m_nPositionOffset);
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
				memcpy(holder.m_data + positions[0], files.m_dataholder->getFiledata(0), files.m_dataholder->getSize(0));
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

	inline void appendFile(const std::pair<DataArchiveHolder*, const size_t>& pr, const char* filename, const std::pair<const void*, const size_t>& filePair, Hw::cHeap* allocator, int alignment)
	{

	}
}

inline size_t ReplaceDataArchiveFile(DataArchiveHolder* holder, size_t holder_size, const Utils::String& filename, Hw::cHeap* allocator, size_t alignment)
{
	using namespace DataArchiveTools;
	size_t holdersize = holder_size;
	Utils::String foldername = filename;

	if (char* ext = foldername.strrchr('.'); ext)
		*ext = 0;

	foldername.shrink_to_fit();

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		FileSystem::Directory* dir = prof->FindDirectory(foldername);

		if (!dir)
			continue;

		int it = 0;
		CTimer timer;
		FilesReplacer filesReplacer(holder, holder_size);
		for (const char* fileName = holder->getNameByFileIndex(0); it < holder->asEntry()->m_nAmountOfFiles; fileName = holder->getNameByFileIndex(++it))
		{
			FileSystem::File* file = dir->FindFile(fileName);

			Utils::String fld = foldername / fileName;
			if (char* chr = fld.strrchr('.'); chr)
				*chr = 0;

			fld.shrink_to_fit();
			if (!strcmp((char*)holder->getFiledata(it), "DAT\0"))
			{
				if (FileSystem::Directory* dir = prof->FindDirectory(fld); !file && dir)
				{
					size_t size = holder->getSize(it);
					LOGINFO("[DATAWORK] We have a DAT here %s, fld = %s", fileName, fld.c_str());
					void* fileData = ModloaderHeap.AllocateMemory(size + align(16, size), 0x1000, 1, 0);
					if (fileData)
					{
						memcpy(fileData, holder->getFiledata(it), size);
						DataArchiveHolder holder;
						holder.m_data = (char*)fileData;
						size = ReplaceDataArchiveFile(&holder, size, foldername / fileName, &ModloaderHeap, 1);
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
			void* fileData = ModloaderHeap.AllocateMemory(file->m_filesize + align(16, file->m_filesize), 0x1000, 1, 0);
			if (fileData && file->read(fileData))
			{
				filesReplacer.m_files.push_back(FileStructure(fileData, file->m_filesize, it, fileName));
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
		if (!filesReplacer.m_files.empty())
		{
			holdersize = replaceFiles(filesReplacer, alignment);
			timer.stop();
			LOG_TIMER(timer, "Took %.3f seconds while repacking");
		}
		else
		{
			LOGWARNING("[DATAWORK] There are no files to replace, but folder %s was found", foldername.c_str());
		}
	}
	return holdersize;
}

#endif 