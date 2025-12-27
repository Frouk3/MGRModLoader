#include "ModLoader.h"
#include "Hooks.h"
#include <FileRead.h>
#include <map>
#include <injector/injector.hpp>

CREATE_THISCALL(false, shared::base + 0xA9E170, int, FileRead_cWork_moveReadWait, FileRead::cWork*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadMods || !ModLoader::bLoadFiles)
		return original(pThis);

	if (pThis->m_Flag & 0x1000)
		return original(pThis);

	static std::unordered_map<FileRead::cWork*, FileSystem::eReadId> reqList;

	if (pThis->m_Flag & 0x800)
	{
		if (FileSystem::IsReadComplete(reqList[pThis]))
		{
			FileSystem::CancelRead(reqList[pThis]); // Release reader
			reqList.erase(pThis);
			pThis->registResource();
			pThis->m_WaitCount = 0;

			pThis->m_MoveRno = pThis->MOVE_FILE_VALID;
			return 1;
		}
		else
		{
			if (!FileSystem::IsReaderAlive(reqList[pThis]) || FileSystem::IsReadCanceled(reqList[pThis]))
			{
				Hw::cHeap::free(pThis->m_pFileData);

				pThis->m_MoveRno = pThis->MOVE_ALLOC;
				pThis->m_Flag |= 0x1000; // Flag it as untouchable by mods
				return 1;
			}
			return 0;
		}
	}
	else
	{
		for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			Utils::formatPath(pThis->m_Path.m_pStr);
			if (FileSystem::File* file = prof->FindFile(pThis->m_Path.m_pStr); file)
			{
				if (!reqList.contains(pThis))
				{
					LOGINFO("[FILEREAD] Found %s[%s] from %s", pThis->m_Path.c_str(), Utils::getProperSize(file->m_filesize).c_str(), prof->m_name.c_str());
					Hw::cHeap::free(pThis->m_pFileData);

					void* pData = ModloaderHeap.alloc(file->m_filesize, 0x1000, pThis->m_Flag & pThis->FLAG_ALLOC_BACK ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL, 0);
					if (!pData)
					{
						LOGERROR("[FILEREAD] Could not allocate space for our modded file.[Old: %p, New: %p, Available: %p]", pThis->m_NeedSize, file->m_filesize, ModloaderHeap.getAllocatableSize());
						pThis->m_Flag |= 0x1000; // cannot alloc with our own heap
						pThis->m_pFileData = pThis->m_pHeap->alloc(pThis->m_NeedSize, 0x1000, pThis->m_Flag & pThis->FLAG_ALLOC_BACK ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL, 0);
						return original(pThis);
					}

					pThis->m_pFileData = pData;
					pThis->m_NeedSize = file->m_filesize;

					pThis->m_Flag |= 0x800;

					FileSystem::eReadId reader = FileSystem::ReadAsync(file->m_path.c_str(), pData, file->m_filesize);
					if (reader == FileSystem::READID_INVALID)
					{
						LOGERROR("[FILEREAD] Invalid reader for reading. Aborting...");
						pThis->m_Flag |= 0x1000;
						pThis->m_MoveRno = pThis->MOVE_ALLOC;
						if (pThis->m_pFileData)
						{
							Hw::cHeap::free(pThis->m_pFileData);
							pThis->m_pFileData = nullptr;
						}
						return 1;
					}
					Hw::DvdReadManager::Cancel(pThis->m_DvdId);
					reqList.insert({ pThis, reader });
					
					return 0; // Let other works to request
				}
			}
		}
	}

	return original(pThis);
}

CREATE_HOOK(false, shared::base + 0x9EAF60, int, __cdecl, getFileSize, const char* filename)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return original(filename);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		if (FileSystem::File* file = prof->FindFile(filename); file)
			return file->m_filesize;
	}

	return original(filename);
}

CREATE_HOOK(false, shared::base + 0xE9C0F6, int, __cdecl, CriFsFileLoad, int loader) // for .usm, everything else will be replaced by Mod Loader's file load hooks
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return original(loader);

	if (*(int*)(loader + 0xB4) != 1u)
		return 2;

	char* filepath = *(char**)(loader + 0xEC);
	int* loaderStatus = (int*)(*(int*)(loader + 0xA8) + 0xC);

	Utils::formatPath(filepath);

	if (*loaderStatus == 1)
	{
		if (true)
		{
			for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
			{
				if (!prof->m_bEnabled)
					continue;

				FileSystem::File* file = prof->FindFile(filepath);

				if (!file)
					continue; // We haven't found any file? Skip to the next profile

				if (strcmp(filepath, file->m_path.c_str())) // Replace once and do not spam into the log
				{
					LOGINFO("[CRIWARE] Replacing %s -> %s...", filepath, file->m_path.c_str());
					strcpy_s(filepath, MAX_PATH, file->m_path.c_str());
					break;
				}
			}
		}
	}

	return original(loader);
}

CREATE_THISCALL(false, shared::base + 0x9EB160, BOOL, Hw_cDvdReader_read, Hw::cDvdReader*, const char* pFilePath, void* pReadAddr, unsigned int buffSize, Hw::DVD_PRIO prio)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return original(pThis, pFilePath, pReadAddr, buffSize, prio);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		if (FileSystem::File* file = prof->FindFile(pFilePath); file)
		{
			LOGINFO("[DVDREAD] Found %s[%s] in %s", pFilePath, Utils::getProperSize(file->m_filesize).c_str(), prof->m_name.c_str());
			void* pData = ModloaderHeap.alloc(file->m_filesize, 0x1000, Hw::HW_ALLOC_PHYSICAL, 0);
			if (!pData)
			{
				LOGERROR("[DVDREAD] Could not allocate space for our modded file.[Old: %p, New: %p, Available: %p]", buffSize, file->m_filesize, ModloaderHeap.getAllocatableSize());
				return original(pThis, pFilePath, pReadAddr, buffSize, prio);
			}
			FileSystem::eReadId id = FileSystem::ReadAsync(file->m_path.c_str(), pData, file->m_filesize);
			if (FileSystem::WaitForRead(id))
			{
				if (FileSystem::IsReadComplete(id))
				{
					Hw::cHeap::free(pReadAddr);
					pThis->m_pReadAddr = pData;
					pThis->m_Size = file->m_filesize;

					buffSize = file->m_filesize;
					pReadAddr = pData;

					FileSystem::CancelRead(id); // Release reader

					return 1;
				}
				else
				{
					Hw::cHeap::free(pData);
					LOGERROR("[DVDREAD] Could not read our modded file %s!", file->m_path.c_str());

					return original(pThis, pFilePath, pReadAddr, buffSize, prio);
				}
			}
			else
			{
				Hw::cHeap::free(pData);
				LOGERROR("[DVDREAD] Was the reader even requested? %s", file->m_path.c_str());

				return original(pThis, pFilePath, pReadAddr, buffSize, prio);
			}
		}
	}

	return original(pThis, pFilePath, pReadAddr, buffSize, prio);
}

DWORD fixDvdReadRequestExit = shared::base + 0x9EC18F + 8;
void __declspec(naked) fixDvdReadRequest()
{
	__asm
	{
		mov ecx, [esp + 10h]
		mov edx, [esp + 14h]
		mov ebx, [esp + 70h]
		mov edi, [esp + 74h]
		jmp fixDvdReadRequestExit
	}
}

void sHooks::Init()
{
	injector::MakeJMP(shared::base + 0x9EC18F, fixDvdReadRequest, true);
}

bool bDummy = (sHooks::Init(), true);