#pragma once

#ifndef FILES_TOOLS_HPP
#define FILES_TOOLS_HPP
#include "ModLoader.h"
#include <HkDataManagerImplement.h>

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
		if (files.m_files.empty())
			return files.m_datasize;

		DataArchiveHolder* holder = files.m_dataholder;
		size_t holdersize = files.m_datasize;
		DataArchiveEntry* dataEntry = holder->asEntry();

		if (!dataEntry->m_nAmountOfFiles)
			return holdersize;

		size_t* positions = (size_t*)(holder->m_data + dataEntry->m_nPositionOffset);
		size_t* sizes = (size_t*)(holder->m_data + dataEntry->m_nSizesOffset);

		if (dataEntry->m_nAmountOfFiles > 1)
		{
			for (auto& file : files.m_files)
				holdersize += file.m_fileSize - sizes[file.m_fileindex];

			char* filedata = (char*)ModloaderHeap.AllocateMemory(holdersize, 0x1000, alignment, 0);

			if (filedata)
			{
				size_t* newpositions = (size_t*)(filedata + ((DataArchiveEntry*)filedata)->m_nPositionOffset);
				size_t* newsizes = (size_t*)(filedata + ((DataArchiveEntry*)filedata)->m_nSizesOffset);

				memcpy(filedata, holder->m_data, files.m_datasize);
				for (auto& file : files.m_files)
					newsizes[file.m_fileindex] = file.m_fileSize;

				auto GetFileByItsIndex = [&](int index) -> FileStructure*
					{
						for (auto& file : files.m_files)
							if (file.m_fileindex == index)
								return &file;

						return nullptr;
					};

				for (size_t i = 0; i < dataEntry->m_nAmountOfFiles - 1; i++)
				{
					newpositions[i + 1] = newpositions[i] + newsizes[i] + align(16, newpositions[i] + newsizes[i]);

					FileStructure* file = GetFileByItsIndex(i);

					if (file && file->m_file)
					{
						memcpy(filedata + newpositions[i], file->m_file, newsizes[i]);
					}
					else
					{
						memcpy(filedata + newpositions[i], holder->m_data + positions[i], sizes[i]);
					}
				}

				operator delete(holder->m_data, &ModloaderHeap);

				holder->m_data = filedata;
			}
		}
		else
		{
			holdersize += files.m_files[0].m_fileSize - sizes[0];

			char* filedata = (char*)ModloaderHeap.AllocateMemory(holdersize, 0x1000, alignment, 0);

			if (filedata)
			{
				memcpy(filedata, holder->m_data, files.m_datasize);

				((size_t*)(filedata + ((DataArchiveEntry*)filedata)->m_nSizesOffset))[0] = files.m_files[0].m_fileSize;

				memcpy(filedata + positions[0], files.m_files[0].m_file, files.m_files[0].m_fileSize);
			}
			
			operator delete(holder->m_data, &ModloaderHeap);
			
			holder->m_data = filedata;
		}

		files.m_datasize = holdersize;

		return holdersize;
	}

	inline void appendFile(const std::pair<DataArchiveHolder*, const size_t>& pr, const char* filename, const std::pair<const void*, const size_t>& filePair, Hw::cHeap* allocator, int alignment)
	{

	}
}

// Broken, just, don't touch it
inline size_t ReplaceDataArchiveFile(DataArchiveHolder* holder, size_t holder_size, const Utils::String& filename, Hw::cHeap* allocator, size_t alignment)
{
	return holder_size;

	if (strcmp(holder->m_data, "DAT\0"))
		return holder_size;

	Utils::String filCopy = filename;

	if (char* chr = filCopy.strrchr('.'); chr)
		*chr = 0;

	filCopy.shrink_to_fit();

	FileSystem::Directory* dir = nullptr;

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		if ((dir = prof->FindDirectory(filCopy)) != nullptr)
			break;
	}

	DataArchiveTools::FilesReplacer replacer(holder, holder_size);

	if (dir)
	{
		for (auto& file : dir->m_files)
		{
			if (size_t ind = holder->getFileIndexLoc(file.getName()); ind != -1)
			{
				void* fldata = file.read();
				if (fldata)
					replacer.m_files.push_back(DataArchiveTools::FileStructure(fldata, file.m_filesize, ind, file.getName()));
			}
		}
	}

	if (replacer.m_files.empty())
		return holder_size;

	holder_size = DataArchiveTools::replaceFiles(replacer, alignment);

	holder->m_data = replacer.m_dataholder->m_data;

	return holder_size;
}

#endif 