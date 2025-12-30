#pragma once

#ifndef FILES_TOOLS_HPP
#define FILES_TOOLS_HPP
#include "ModLoader.h"
#include <Hw.h>
#include <HwDvd.h>
#include <bit>
#include <unordered_map>
#include <unordered_set>
#define TAKE_TIMER_SNAPSHOTS 0
#include "Timer.hpp"

namespace CriWare
{
	inline int iAvailableCPKs = 0;
	inline lib::StaticArray<Hw::cDvdCriFsBinder *, 128> aBinders;

	inline Hw::cDvdCriFsBinder* getFreeBinderWork()
	{
		Hw::cDvdCriFsBinder* pWork = new(ModloaderHeap) Hw::cDvdCriFsBinder();

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
			{
				aBinders.erase(&work);
				break;
			}
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
		size_t m_fileindex = (size_t)-1;
		Utils::String m_filename;
		bool m_owned = false;

		FileStructure()
		{
			m_file = nullptr;
			m_fileSize = 0;
		}

		FileStructure(void* filedata, size_t filesize, size_t fileindex, const char* filename, bool owned)
			: m_file(filedata), m_fileSize(filesize), m_fileindex(fileindex), m_filename(filename), m_owned(owned) {}
	};

	struct FilesReplacer
	{
		Hw::cFmerge m_dataholder;
		size_t m_datasize;
		std::vector<FileStructure> m_files;

		FilesReplacer() = default;

		FilesReplacer(Hw::cFmerge dataholder, size_t datasize) : m_dataholder(dataholder), m_datasize(datasize) { }

		~FilesReplacer()
		{
			for (FileStructure& file : m_files)
			{
				if (file.m_owned && file.m_file)
					operator delete(file.m_file, &ModloaderHeap);

				file.m_file = nullptr;
			}

			m_files.clear();
		}
	};

	inline size_t align(size_t alignment, size_t length)
	{
		if (alignment == 0)
			return length;

		return ~(alignment - 1u) & (length + alignment - 1u);
	}

	inline bool isFmergeBuffer(const void* data, size_t size)
	{
		if (!data || size < 4)
			return false;
		const unsigned char* p = (const unsigned char*)data;
		return (p[0] == 'D' && p[1] == 'A' && p[2] == 'T' && p[3] == 0);
	}

	inline int rebuildFmerge(std::vector<FileStructure>& files, const char *whereToSave)
	{
		FILE* pFile = fopen(whereToSave, "wb");
		if (!pFile)
			return 1;

		size_t longestName = 0;
		for (const FileStructure& file : files)
			longestName = max(longestName, file.m_filename.length() + 1);

		auto bitWidth = [](size_t value) -> unsigned int
		{
			return value == 0 ? 0u : (unsigned int)std::bit_width(value);
		};

		unsigned int shift = min(31u, 32u - bitWidth(files.size()));
		unsigned int bucketSize = 1u << (31u - shift);

		std::vector<short> bucketTbl(bucketSize, -1);
		std::vector<std::pair<unsigned int, short>> hashTbl;
		hashTbl.reserve(files.size());
		for (size_t i = 0; i < files.size(); i++)
		{
			unsigned int hash = crc32lower(files[i].m_filename.c_str());
			hashTbl.push_back({ hash, (short)i });
		}

		std::sort(hashTbl.begin(), hashTbl.end(), [shift](const std::pair<unsigned int, short>& a, const std::pair<unsigned int, short>& b)
		{
			return (a.first >> shift) < (b.first >> shift);
		});

		for (size_t i = 0; i < hashTbl.size(); i++)
		{
			unsigned int bucketIndex = hashTbl[i].first >> shift;
			if (bucketTbl[bucketIndex] == -1)
				bucketTbl[bucketIndex] = (short)i;
		}

		struct BinHashMap
		{
			UINT32 shift;
			UINT32 bucketTblOffs;
			UINT32 hashTblOffs;
			UINT32 fileIndecesTblOffs;
		};

		BinHashMap binHashMap;
		binHashMap.shift = shift;
		binHashMap.bucketTblOffs = 0x10;
		binHashMap.hashTblOffs = binHashMap.bucketTblOffs + (sizeof(short) * bucketSize);
		binHashMap.fileIndecesTblOffs = binHashMap.hashTblOffs + (sizeof(UINT32) * hashTbl.size());

		Hw::FmergeHeader header;
		*(unsigned int*)header.magic = 0x00544144;
		header.m_FileNum = files.size();
		header.m_OffsetTblOffs = 0x20;
		header.m_ExtOffs = header.m_OffsetTblOffs + sizeof(size_t) * files.size();
		header.m_NamesOffs = header.m_ExtOffs + sizeof(size_t) * files.size();
		header.m_SizeOffs = header.m_NamesOffs + (longestName * files.size()) + 6;
		header.m_HashMapOffs = header.m_SizeOffs + sizeof(size_t) * files.size();

		auto WriteInt = [&](size_t value)
		{
			fwrite(&value, sizeof(size_t), 1, pFile);
		};

		auto WriteShort = [&](short value)
		{
			fwrite(&value, sizeof(short), 1, pFile);
		};

		auto WriteBytes = [&](const void* data, size_t size)
		{
			fwrite(data, 1, size, pFile);
		};

		auto WriteByte = [&](unsigned char value)
		{
			fwrite(&value, 1, 1, pFile);
		};

		auto WriteSetBytes = [&](unsigned char value, size_t size)
		{
			for (size_t i = 0; i < size; i++)
				fwrite(&value, 1, 1, pFile);
		};

		auto Seek = [&](size_t offset)
		{
			fseek(pFile, (long)offset, SEEK_SET);
		};

		auto Skip = [&](size_t size)
		{
			fseek(pFile, (long)size, SEEK_CUR);
		};

		auto WriteString = [&](const Utils::String& str, size_t maxLength)
		{
			WriteBytes(str.c_str(), maxLength == (size_t)-1 ? str.length() + 1 : maxLength);
		};

		WriteBytes(&header, sizeof(Hw::FmergeHeader));
		WriteInt(0);
		Seek(header.m_OffsetTblOffs);
		for (size_t i = 0; i < files.size(); i++)
			WriteInt(0);

		Seek(header.m_ExtOffs);
		for (size_t i = 0; i < files.size(); i++)
		{
			const char* ext = "";
			if (char* dot = files[i].m_filename.strrchr('.'))
				ext = dot + 1;

			if (strlen(ext) > 3) WriteString(ext, 4);
			else
			{
				WriteString(ext, 3);
				WriteByte(0);
			}
		}

		Seek(header.m_NamesOffs);
		WriteInt(longestName);
		for (size_t i = 0; i < files.size(); i++)
		{
			Seek(header.m_NamesOffs + sizeof(int) + i * longestName);
			WriteString(files[i].m_filename, (size_t)-1);
			WriteSetBytes(0, longestName - files[i].m_filename.length() + 1);
		}

		WriteShort(0);
		Seek(header.m_SizeOffs);
		for (size_t i = 0; i < files.size(); i++)
			WriteInt(files[i].m_fileSize);

		Seek(header.m_HashMapOffs);
		WriteBytes(&binHashMap, sizeof(BinHashMap));
		Seek(header.m_HashMapOffs + binHashMap.bucketTblOffs);
		for (size_t i = 0; i < bucketTbl.size(); i++)
			WriteShort(bucketTbl[i]);
		Seek(header.m_HashMapOffs + binHashMap.hashTblOffs);
		for (size_t i = 0; i < hashTbl.size(); i++)
			WriteInt(hashTbl[i].first);
		Seek(header.m_HashMapOffs + binHashMap.fileIndecesTblOffs);
		for (size_t i = 0; i < hashTbl.size(); i++)
			WriteShort(hashTbl[i].second);

		Seek(align(0x1000, (size_t)ftell(pFile)));
		for (size_t i = 0; i < files.size(); i++)
		{
			int ptr = ftell(pFile);
			Seek(header.m_OffsetTblOffs + sizeof(size_t) * i);
			WriteInt((size_t)ptr);
			Seek((size_t)ptr);
			WriteBytes(files[i].m_file, files[i].m_fileSize);
			Skip(align(0x100, files[i].m_fileSize) - files[i].m_fileSize);
		}

		fclose(pFile);

		return 0;
	}

	inline size_t replaceFiles(FilesReplacer& files, Hw::HW_ALLOC_MODE mode, const Utils::String& path)
	{
		if (files.m_files.empty() || files.m_dataholder.m_data->m_FileNum == 0)
			return (size_t)-1;

		std::vector<FileStructure> newFiles;
		newFiles.reserve(files.m_dataholder.getFileAmount() + files.m_files.size());

		auto cleanupAllFiles = [&]()
		{
			for (FileStructure& file : newFiles)
			{
				if (file.m_owned && file.m_file)
					operator delete(file.m_file, &ModloaderHeap);
				file.m_file = nullptr;
			}
			newFiles.clear();
		};

		std::unordered_map<std::string, size_t> replacementLookup;

		auto toLowerCopy = [](const char* s) -> std::string
		{
			return Utils::String(s).lower();
		};

		replacementLookup.reserve(files.m_files.size());
		for (size_t idx = 0; idx < files.m_files.size(); ++idx)
			replacementLookup[toLowerCopy(files.m_files[idx].m_filename.c_str())] = idx;

		std::vector<bool> consumed(files.m_files.size(), false);

		for (size_t i = 0; i < files.m_dataholder.getFileAmount(); i++)
		{
			const char* filename = files.m_dataholder.getFileIndexFileName(i);
			size_t filesize = files.m_dataholder.getFileIndexSize(i);
			void* filedata = files.m_dataholder.getFileIndexData(i);

			std::string filenameLower = toLowerCopy(filename);
			auto it = replacementLookup.find(filenameLower);
			if (it != replacementLookup.end())
			{
				const auto& rep = files.m_files[it->second];
				newFiles.emplace_back(rep.m_file, rep.m_fileSize, i, filename, false);
				consumed[it->second] = true;
				continue;
			}

			void* newData = ModloaderHeap.alloc(filesize, 0x1000, mode, 0);
			if (!newData)
			{
				LOGERROR("[DAT_TOOLS] Failed to allocate memory for file during replacement: %s", filename);
				cleanupAllFiles();
				return (size_t)-1;
			}
			memcpy(newData, filedata, filesize);
			newFiles.emplace_back(newData, filesize, i, filename, true);
		}

		for (size_t j = 0; j < files.m_files.size(); ++j)
		{
			if (consumed[j])
				continue;

			const FileStructure& rep = files.m_files[j];
			newFiles.emplace_back(rep.m_file, rep.m_fileSize, (size_t)-1, rep.m_filename.c_str(), false);
			LOGINFO("[DAT_TOOLS] Appended new file into archive: %s", rep.m_filename.c_str());
		}

		std::sort(newFiles.begin(), newFiles.end(), [](const FileStructure& a, const FileStructure& b)
		{
			return strcmp(a.m_filename.c_str(), b.m_filename.c_str()) < 0;
		});

		if (rebuildFmerge(newFiles, path.c_str()) == 0)
		{
			void* newData = nullptr;
			if (!FileSystem::ReadSyncAlloc(path.c_str(), &newData, &files.m_datasize, mode == Hw::HW_ALLOC_PHYSICAL_BACK ? FileSystem::cReader::READFLAG_ALLOC_BACK : 0))
			{
				LOGERROR("[DAT_TOOLS] Failed to read rebuild data archive file: %s", path.c_str());
				cleanupAllFiles();
				return (size_t)-1;
			}

			files.m_dataholder.setData((char*)newData, nullptr);
			cleanupAllFiles();
			return files.m_datasize;
		}

		cleanupAllFiles();
		return (size_t)-1;
	}
}

inline size_t ReplaceDataArchiveFile(Hw::cFmerge* holder, size_t holder_size, const Utils::String& filename, Hw::HW_ALLOC_MODE mode)
{
	if (strcmp(holder->m_data->magic, "DAT\0") != 0)
		return (size_t)-1;

	DataArchiveTools::FilesReplacer replacer(*holder, holder_size);

	auto sanitizeDots = [](const Utils::String& in) -> Utils::String
	{
		Utils::String out = in;
		for (size_t i = 0; i < out.length(); ++i)
			if (out[i] == '.')
				out[i] = '_';
		return out;
	};

	auto toLowerCopy = [](const char* s) -> Utils::String
	{
		return Utils::String(s).lower();
	};

	auto sanitizeDotsLower = [&](const char* s) -> Utils::String
	{
		Utils::String r = toLowerCopy(s);
		for (char& c : r)
			if (c == '.')
				c = '_';
		return r;
	};

	auto firstPathSegment = [](const Utils::String& rel) -> Utils::String
	{
		const char* b = rel.c_str();
		const char* p1 = strchr(b, '/');
		const char* p2 = strchr(b, '\\');
		const char* p = (p1 && p2) ? ((p1 < p2) ? p1 : p2) : (p1 ? p1 : p2);
		if (!p) return Utils::String(b);
		return Utils::String(b, (size_t)(p - b));
	};

	std::vector<Utils::String> nestedNamesLower;
	nestedNamesLower.reserve(holder->getFileAmount());
	std::vector<Utils::String> nestedNamesLowerSanitized;
	nestedNamesLowerSanitized.reserve(holder->getFileAmount());
	for (size_t i = 0; i < holder->getFileAmount(); ++i)
	{
		void* innerData = holder->getFileIndexData(i);
		size_t innerSize = holder->getFileIndexSize(i);
		if (DataArchiveTools::isFmergeBuffer(innerData, innerSize))
		{
			const char* innerName = holder->getFileIndexFileName(i);
			nestedNamesLower.push_back(toLowerCopy(innerName));
			nestedNamesLowerSanitized.push_back(sanitizeDotsLower(innerName));
		}
	}

	auto isNestedTopSegment = [&](const Utils::String& rel) -> bool
	{
		Utils::String segLower = toLowerCopy(firstPathSegment(rel).c_str());
		Utils::String segLowerSan = sanitizeDotsLower(segLower.c_str());
		for (size_t idx = 0; idx < nestedNamesLower.size(); ++idx)
		{
			const Utils::String& base = nestedNamesLower[idx];
			const Utils::String& baseSan = nestedNamesLowerSanitized[idx];

			if (base == segLower || base == segLowerSan || baseSan == segLower || baseSan == segLowerSan)
				return true;

			Utils::String baseDat = base + "_dat";
			Utils::String baseSanDat = baseSan + "_dat";
			if (baseDat == segLower || baseDat == segLowerSan || baseSanDat == segLower || baseSanDat == segLowerSan)
				return true;
		}
		return false;
	};

	auto getDirLeafName = [](const FileSystem::Directory* dir) -> Utils::String
	{
		const char* slash = dir->m_path.strrchr('\\');
		if (!slash) slash = dir->m_path.strrchr('/');
		return slash ? Utils::String(slash + 1) : dir->m_path;
	};

	auto hasMatchingNestedDirectory = [&](FileSystem::Directory* dir, const Utils::String& topSeg) -> bool
	{
		Utils::String targetLower = toLowerCopy(topSeg.c_str());
		Utils::String targetLowerSan = sanitizeDotsLower(topSeg.c_str());
		for (auto* sub : dir->m_subdirs)
		{
			Utils::String leaf = getDirLeafName(sub);
			Utils::String leafLower = toLowerCopy(leaf.c_str());
			Utils::String leafLowerSan = sanitizeDotsLower(leaf.c_str());
			if (leafLower == targetLower || leafLower == targetLowerSan || leafLowerSan == targetLower || leafLowerSan == targetLowerSan)
				return true;
		}
		return false;
	};

	auto queueDirectoryFiles = [&](FileSystem::Directory* directory, FileSystem::Directory* rootDir, const Utils::String& relativePath, const auto& self) -> void
	{
		if (!directory)
			return;

		for (auto& file : directory->m_files)
		{
			Utils::String archiveName = relativePath.empty() ? file.getName() : relativePath / file.getName();
			Utils::String topSeg = firstPathSegment(archiveName);

			if (isNestedTopSegment(archiveName) && hasMatchingNestedDirectory(rootDir, topSeg))
			{
				LOGINFO("[DAT_TOOLS] Skipped nested-level file: %s", file.m_path.c_str());
				continue;
			}

			void* filedata = ModloaderHeap.alloc(file.m_filesize, 0x1000, mode, 0);
			if (!filedata)
			{
				LOGERROR("[DAT_TOOLS] Failed to allocate memory for file replacement: %s", file.m_path.c_str());
				continue;
			}
			if (!file.read(filedata))
			{
				LOGERROR("[DAT_TOOLS] Failed to read file for replacement: %s", file.m_path.c_str());
				operator delete(filedata, &ModloaderHeap);
				continue;
			}
			replacer.m_files.emplace_back(filedata, file.m_filesize, (size_t)-1, archiveName.c_str(), true);
			LOGINFO("[DAT_TOOLS] Queued file for replacement: %s", file.m_path.c_str());
		}

		for (auto& subdir : directory->m_subdirs)
		{
			const char* slash = subdir->m_path.strrchr('\\');
			if (!slash) slash = subdir->m_path.strrchr('/');
			Utils::String subdirName = slash ? Utils::String(slash + 1) : subdir->m_path;

			if (isNestedTopSegment(subdirName) && hasMatchingNestedDirectory(rootDir, firstPathSegment(subdirName)))
			{
				LOGINFO("[DAT_TOOLS] Skipped nested-level directory: %s", subdir->m_path.c_str());
				continue;
			}

			Utils::String nextRelative = relativePath.empty() ? subdirName : relativePath / subdirName;
			self(subdir, rootDir, nextRelative, self);
		}
	};

	std::vector<Utils::String> lookupCandidates;
	Utils::String lookupBase = sanitizeDots(filename);
	lookupCandidates.push_back(lookupBase);
	lookupCandidates.push_back(lookupBase + "_dat");
	lookupCandidates.push_back(filename);
	lookupCandidates.push_back(filename + "_dat");

	std::unordered_set<std::string> processedPaths;
	processedPaths.reserve(ModLoader::Profiles.getSize());

	auto alreadyProcessed = [&](FileSystem::Directory* dir) -> bool
	{
		Utils::String lower = toLowerCopy(dir->m_path.c_str());
		std::string key(lower.c_str());
		auto [it, inserted] = processedPaths.insert(key);
		return !inserted;
	};

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		for (const auto& candidate : lookupCandidates)
		{
			FileSystem::Directory* dir = prof->FindDirectory(candidate.c_str());
			if (!dir)
				continue;

			if (alreadyProcessed(dir))
				continue;

			queueDirectoryFiles(dir, dir, Utils::String(), queueDirectoryFiles);
		}
	}

	std::unordered_set<std::string> replacementNamesLower;
	replacementNamesLower.reserve(replacer.m_files.size());
	for (const auto& f : replacer.m_files)
		replacementNamesLower.insert(std::string(toLowerCopy(f.m_filename.c_str()).c_str()));

	auto hasDirectReplacement = [&](const char* innerName) -> bool
	{
		return replacementNamesLower.find(std::string(toLowerCopy(innerName).c_str())) != replacementNamesLower.end();
	};

	for (size_t i = 0; i < holder->getFileAmount(); ++i)
	{
		const char* innerName = holder->getFileIndexFileName(i);
		if (hasDirectReplacement(innerName))
			continue;

		void* innerData = holder->getFileIndexData(i);
		size_t innerSize = holder->getFileIndexSize(i);
		if (!DataArchiveTools::isFmergeBuffer(innerData, innerSize))
			continue;

		Hw::cFmerge nestedHolder;
		nestedHolder.setData((char*)innerData, nullptr);

		Utils::String nestedPath = filename / innerName;
		size_t nestedNewSize = ReplaceDataArchiveFile(&nestedHolder, innerSize, nestedPath, mode);
		if (nestedNewSize != (size_t)-1)
		{
			void* copyBuf = ModloaderHeap.alloc(nestedNewSize, 0x1000, mode, 0);
			if (!copyBuf)
			{
				LOGERROR("[DAT_TOOLS] Failed to allocate memory for nested archive copy: %s", nestedPath.c_str());
				continue;
			}
			memcpy(copyBuf, (void*)nestedHolder.m_data, nestedNewSize);
			Hw::cHeap::free(nestedHolder.m_data);
			replacer.m_files.emplace_back(copyBuf, nestedNewSize, (size_t)-1, innerName, true);
			LOGINFO("[DAT_TOOLS] Recursively queued rebuilt nested archive: %s", nestedPath.c_str());
		}
	}

	if (replacer.m_files.empty())
		return (size_t)-1;

	Utils::String path = Utils::String(ModLoader::ModLoaderPath) / "repacked";
	Utils::String subDir = filename;
	const char* begin = subDir.begin();
	const char* end = subDir.end();

	while (begin < end)
	{
		const char* slashBack = strchr(begin, '\\');
		const char* slashFwd = strchr(begin, '/');
		const char* slash = nullptr;
		if (slashBack && slashFwd)
			slash = (slashBack < slashFwd) ? slashBack : slashFwd;
		else
			slash = slashBack ? slashBack : slashFwd;

		if (!slash)
			slash = end;

		Utils::String dirPart = Utils::String(begin, (size_t)(slash - begin));
		dirPart = sanitizeDots(dirPart);

		if (!dirPart.empty())
		{
			path = path / dirPart;
			if (!FileSystem::PathExists(path.c_str()))
				CreateDirectoryA(path.c_str(), nullptr);
		}

		begin = slash + 1;
	}

	Utils::String outFile = path / "fmerge.dat";
	size_t newSize = DataArchiveTools::replaceFiles(replacer, mode, outFile);
	if (newSize != (size_t)-1)
	{
		if (holder->m_data && ((Hw::cHeap**)holder->m_data)[-1])
			ModloaderHeap.free(holder->m_data);

		holder->setData((char*)replacer.m_dataholder.m_data, nullptr);
		LOGINFO("[DAT_TOOLS] Successfully replaced files in data archive: %s", filename.c_str());
		return newSize;
	}

	LOGERROR("[DAT_TOOLS] Failed to replace files in data archive: %s", filename.c_str());
	return (size_t)-1;
}

#endif