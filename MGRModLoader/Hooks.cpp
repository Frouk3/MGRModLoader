#include "ModLoader.h"
#include "Hooks.h"
#include <FileRead.h>
#include <map>
#include <injector/injector.hpp>
#include "FilesTools.hpp"

CREATE_THISCALL(false, shared::base + 0xA9E170, int, FileRead_cWork_moveReadWait, FileRead::cWork*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadMods || !ModLoader::bLoadFiles)
		return original(pThis);

	if (pThis->m_Flag & 0x1000)
		return original(pThis);

	static std::map<FileRead::cWork*, FileSystem::eReadId> reqList;

	if (reqList[pThis] || pThis->m_Flag & 0x800)
	{
		if (FileSystem::IsReadComplete(reqList[pThis]))
		{
			// LOG("[FILEREAD] Finished here reading %s", pThis->m_Path.c_str());
			FileSystem::Release(reqList[pThis]); // Release reader
			
			if (FileSystem::IsReaderAlive(reqList[pThis]))
				LOGWARNING("[FILEREAD] Reader is still alive after release for %s", pThis->m_Path.c_str());
			
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
				pThis->m_pFileData = nullptr;
				FileSystem::CancelRead(reqList[pThis]); // Release reader
				reqList.erase(pThis);

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
				LOGINFO("[FILEREAD] Found %s[%s] in %s", pThis->m_Path.c_str(), Utils::getProperSize(file->m_filesize).c_str(), prof->m_name.c_str());
				// Hw::cHeap::free(pThis->m_pFileData);

				void* pData = pThis->m_pFileData;
				if (!pData)
				{
					LOGERROR("[FILEREAD] Could not allocate space for our modded file.[Old: %p, New: %p, Available: %p]", pThis->m_NeedSize, file->m_filesize, ModloaderHeap.getAllocatableSize());
					pThis->m_Flag |= 0x1000; // cannot alloc with our own heap
					pThis->m_pFileData = pThis->m_pHeap->alloc(pThis->m_NeedSize, 0x1000, pThis->m_Flag & pThis->FLAG_ALLOC_BACK ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL, 0);

					return original(pThis);
				}

				pThis->m_pFileData = pData;
				pThis->m_NeedSize = file->m_filesize;

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
				pThis->m_Flag |= 0x800;
				Hw::DvdReadManager::Cancel(pThis->m_DvdId);
				reqList[pThis] = reader;

				return 0; // Let other works to request
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

	if (*loaderStatus == 1 && stricmp(&filepath[strlen(filepath) - 4], ".usm") == 0)
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

				if (stricmp(filepath, file->m_path.c_str()) == 0)
					return original(loader);

				LOGINFO("[CRIWARE] Found %s[%s] in %s", filepath, Utils::getProperSize(file->m_filesize).c_str(), prof->m_name.c_str());
				strcpy_s(filepath, MAX_PATH, file->m_path.c_str());
				break;
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
			void* pData = pReadAddr;
			FileSystem::eReadId id = FileSystem::READID_INVALID;
			if (FileSystem::eReadId readId = FileSystem::GetActiveReader(file->m_path.c_str()); readId != FileSystem::READID_INVALID)
			{
				LOGWARNING("[DVDREAD] It reached here, so we should forcefully wait for the active reader to complete first...(%s)", file->m_path.c_str());
				id = readId;
				FileSystem::WaitForRead(readId);
				if (FileSystem::IsReadComplete(readId))
				{
					pThis->m_pReadAddr = pData;
					pThis->m_Size = file->m_filesize;

					buffSize = file->m_filesize;

					Hw::DvdEnv::DebugUnregistReader(pThis);
					Hw::DvdEnv::DebugEndCurrentReader(pThis);

					return 1;
				}
				goto READER_CHECK_LABEL;
			}
			LOGINFO("[DVDREAD] Found %s[%s] in %s", pFilePath, Utils::getProperSize(file->m_filesize).c_str(), prof->m_name.c_str());
			// pData = ModloaderHeap.alloc(file->m_filesize, 0x1000, Hw::HW_ALLOC_PHYSICAL, 0); // commented out since we already have allocated space with desired size
			if (!pData)
			{
				LOGERROR("[DVDREAD] Could not allocate space for our modded file.[Old: %p, New: %p, Available: %p]", buffSize, file->m_filesize, ModloaderHeap.getAllocatableSize());
				return original(pThis, pFilePath, pReadAddr, buffSize, prio);
			}
			id = FileSystem::ReadAsync(file->m_path.c_str(), pData, file->m_filesize);
			if (FileSystem::WaitForRead(id))
			{
				READER_CHECK_LABEL:
				if (FileSystem::IsReadComplete(id))
				{
					// Hw::cHeap::free(pReadAddr);
					pThis->m_pReadAddr = pData;
					pThis->m_Size = file->m_filesize;

					buffSize = file->m_filesize;

					FileSystem::Release(id); // Release reader

					Hw::DvdEnv::DebugUnregistReader(pThis);
					Hw::DvdEnv::DebugEndCurrentReader(pThis);

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

CREATE_THISCALL(false, shared::base + 0x9F1320, int, PgIoHookDeferredCRI_LoadSnd, char*, char* pData, char* pEnvData, char* pLoaderData)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return original(pThis, pData, pEnvData, pLoaderData);

	char* wemName = *(char**)(pData + 0x10);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		if (FileSystem::File* file = prof->FindFile(wemName); file)
		{
			LOGINFO("[LOADSND] Found %s[%s] in %s", wemName, Utils::getProperSize(file->m_filesize).c_str(), prof->m_name.c_str());
			strcpy_s(wemName, MAX_PATH, file->m_path.c_str());

			return original(pThis, pData, pEnvData, pLoaderData);
		}
	}
	return original(pThis, pData, pEnvData, pLoaderData);
}

CREATE_THISCALL(false, shared::base + 0xA9CBC0, void, FileRead_cWork_registerResource, FileRead::cWork*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadMods || !ModLoader::bLoadFiles)
		return original(pThis); // jump into original

	if (!strcmp((char*)pThis->m_pFileData, "DAT\0"))
	{
		Hw::cFmerge backupDat = Hw::cFmerge((char*)pThis->m_pFileData);

		Hw::cFmerge passDat = backupDat;

		Utils::String filename = Utils::formatPath(pThis->m_Path.c_str());

		if (char* dot = filename.strrchr('.'))
			*dot = '_'; // change extension to prevent issues

		size_t size = ReplaceDataArchiveFile(&passDat, pThis->m_NeedSize, filename, pThis->m_Flag & pThis->FLAG_ALLOC_BACK ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL);
		// if (size == -1) LOGERROR("[FILEREAD] Could not replace file %s in data archive!", pThis->m_Path.c_str());
		if (size != 0 && size != -1)
		{
			LOGINFO("[FILEREAD] Replaced file %s in data archive with size %s", pThis->m_Path.c_str(), Utils::getProperSize(size).c_str());

			pThis->m_pFileData = passDat.getData();
			pThis->m_NeedSize = (unsigned int)size;
		}
	}

	return original(pThis);
}

CREATE_THISCALL(false, shared::base + 0x9EA800, void, Hw_cDvdReader_update, Hw::cDvdReader*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return original(pThis);

	if (pThis->m_State == Hw::cDvdReader::STATE_COMPLETE)
	{
		if (pThis->m_pReadAddr && !strcmp((char*)pThis->m_pReadAddr, "DAT\0"))
		{
			Hw::cFmerge holder;
			holder.m_data = (Hw::FmergeHeader*)pThis->m_pReadAddr;

			Utils::String filepath = Utils::formatPath(pThis->m_pFilePath);
			if (char* dot = filepath.strrchr('.'))
				*dot = '_'; // change extension to prevent issues

			size_t newSize = ReplaceDataArchiveFile(&holder, pThis->m_Size, filepath, Hw::HW_ALLOC_PHYSICAL_BACK);
			if (newSize != 0 && newSize != -1)
			{
				LOGINFO("[DVDREAD] Replaced file %s in data archive with size %s", pThis->m_pFilePath, Utils::getProperSize(newSize).c_str());
				pThis->m_Size = (unsigned int)newSize;
				pThis->m_pReadAddr = holder.m_data;
			}
		}
		return;
	}
	return original(pThis);
}

CREATE_HOOK(false, shared::base + 0x5825B0, int, __cdecl, BindCpk)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return original();

	if (!CriWare::iAvailableCPKs)
		return original();

	if (CriWare::iAvailableCPKs)
	{
		for (int i = 0; i < CriWare::aBinders.getSize(); i++)
			CriWare::freeBinderWork(CriWare::aBinders[i]);

		CriWare::aBinders.clear();
	}

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		prof->FileWalk([&](FileSystem::File& file) -> void
			{
				if (const char* ext = strrchr(file.getName(), '.'); ext && !strcmp(ext, ".cpk"))
				{
					Hw::cDvdCriFsBinder* pWork = CriWare::getFreeBinderWork();

					if (!pWork->bindCpkFileAsync(file.m_path.c_str(), 0x10000))
					{
						LOGERROR("[CRIWARE] Failed to sync %s archive.", file.m_path.c_str());
						return;
					}

					while (pWork->m_BindStatus == CRIFSBINDER_STATUS_ANALYZE)
					{
						if (criFsBinder_GetStatus(pWork->m_BinderId, &pWork->m_BindStatus) || pWork->m_BindStatus != CRIFSBINDER_STATUS_ANALYZE && pWork->m_BindStatus != CRIFSBINDER_STATUS_COMPLETE)
						{
							CriWare::freeBinderWork(pWork);
							break;
						}
					}

					if (pWork->m_BindStatus != CRIFSBINDER_STATUS_COMPLETE)
					{
						LOGERROR("[CRIWARE] Failed to bind %s archive.", file.m_path.c_str());
						return;
					}

					LOGINFO("[CRIWARE] Bound %s archive successfully.", file.m_path.c_str());
					pWork->m_Type = Hw::cDvdCriFsBinder::TYPE_FILE;
				}
			});
	}

	return original();
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