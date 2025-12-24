#include "Hooks.h"
#include <FileRead.h>
#include "ModLoader.h"
#include "gui.h"
#include "FilesTools.hpp"
#include <injector/injector.hpp>
#include <HwDvd.h>

CREATE_HOOK(false, shared::base + 0x61E8A0, void, __cdecl, InputUpdate)
{
	if (gui::GUIHotkey.m_bToggle)
		return;

	return oInputUpdate();
}

CREATE_HOOK(false, shared::base + 0x9EAF60, size_t, __cdecl, GetFilesize, const char* file)
{
	size_t res = oGetFilesize(file);
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return res;

	if (const char* ext = strrchr(file, '.'); !res || (ext && (!strcmp(ext, ".wem") || !strcmp(ext, ".usm"))))
	{
		for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			if (FileSystem::File* pFile = prof->FindFile(file); pFile)
				return pFile->m_filesize;
		}
	}

	return res;
}

CREATE_HOOK(false, shared::base + 0xE9C0F6, int, __cdecl, CriFsFileLoad, int loader)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oCriFsFileLoad(loader);

	if (*(int*)(loader + 0xB4) != 1u)
		return 2;

	char* filepath = *(char**)(loader + 0xEC);
	int* loaderStatus = (int*)(*(int*)(loader + 0xA8) + 0xC);

	Utils::formatPath(filepath);

	if (!strcmp(&filepath[strlen(filepath) - 3], "usm"))
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
				strcpy(filepath, file->m_path.c_str());
				break;
			}
		}
	}

	return oCriFsFileLoad(loader);
}

CREATE_THISCALL(false, shared::base + 0xA9CBC0, void, FileRead_RegisterFile, FileRead::cWork*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
	{
		oFileRead_RegisterFile(pThis);
		return;
	}

	if (strcmp((char*)pThis->m_pFileData, "DAT\0"))
	{
		oFileRead_RegisterFile(pThis);
		return;
	}

	Hw::cFmerge holder;
	holder.m_data = (Hw::FmergeHeader*)pThis->m_pFileData;

	pThis->m_NeedSize = ReplaceDataArchiveFile(&holder, pThis->m_NeedSize, pThis->m_Path.c_str(), pThis->m_pHeap, pThis->m_Flag & 0x20 ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL);

	pThis->m_pFileData = holder.m_data;

	oFileRead_RegisterFile(pThis);
};

CREATE_THISCALL(false, shared::base + 0xA9E170, int, FileRead_Load, FileRead::cWork*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oFileRead_Load(pThis);

	if (pThis->m_Flag & 0x10000 || pThis->m_Flag & 0x8000) // Load original or load modified one or abort if the file was loaded
		return oFileRead_Load(pThis);

	Utils::formatPath(pThis->m_Path.m_pStr);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		FileSystem::File* file = prof->FindFile(pThis->m_Path.c_str());

		if (!file)
			continue;

		if (!file->m_filesize)
		{
			LOGWARNING("[FILEREAD] %s has no size, aborting and warning the manager...", file->m_path.c_str());

			FreeMemory(pThis->m_pFileData, 0);
			pThis->m_pFileData = nullptr;

			pThis->m_NeedSize = 0;

			pThis->m_MoveRno = pThis->MOVE_FILE_NONE;
			pThis->m_WaitCount = 0;

			return 0;
		}

		FreeMemory(pThis->m_pFileData, 0);
		pThis->m_pFileData = nullptr;

		pThis->m_pFileData = ModloaderHeap.alloc(file->m_filesize, 0x1000, pThis->m_Flag & 0x20 ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL, 0);

		if (!pThis->m_pFileData)
		{
			// Panic, we can't allocate file data for modified file

			LOGERROR("[FILEREAD] Tried to allocate memory for modified file, but failed! [%s, %s]", file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
			pThis->moveFileNone();
			pThis->m_MoveRno = pThis->MOVE_ALLOC;
			pThis->m_Flag |= 0x10000;
			pThis->m_WaitCount = 0;
			return 1;
		}

		if (file->m_filesize == pThis->m_NeedSize)
			LOGINFO("[FILEREAD] Loading %s[%s]...", file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
		else
			LOGINFO("[FILEREAD] Replacing %s[%s] with %s[%s]", pThis->m_Path.c_str(), Utils::getProperSize(pThis->m_NeedSize).c_str(), file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());

		pThis->m_NeedSize = file->m_filesize;

		pThis->m_Flag |= 0x8000;

		file->read(pThis->m_pFileData);

		pThis->registResource();

		pThis->m_MoveRno = pThis->MOVE_FILE_VALID;
		pThis->m_WaitCount = 0;

		return 1;
	}

	return oFileRead_Load(pThis);
}

CREATE_THISCALL(false, shared::base + 0x9EB160, BOOL, Hw_cDvdReader_read, Hw::cDvdReader*, const char* filepath, void* filedata, size_t buffersize, int priority)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oHw_cDvdReader_read(pThis, filepath, filedata, buffersize, priority);

	BOOL result = oHw_cDvdReader_read(pThis, filepath, filedata, buffersize, priority);

	if (!result)
		return result;

	Utils::formatPath(pThis->m_pFilePath);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		FileSystem::File* file = prof->FindFile(pThis->m_pFilePath);

		if (!file)
			continue;

		if (file->m_filesize)
		{
			LOGINFO("[DVDREAD] Replacing %s[%s] -> %s[%s]", pThis->m_pFilePath, Utils::getProperSize(pThis->m_Size).c_str(), file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());

			int* block = (int*)pThis->m_pReadAddr - 1;
			Hw::cHeap* allocator = *(Hw::cHeap**)(block);

			FreeMemory(pThis->m_pReadAddr, 0); // as in hkLoad
			pThis->m_pReadAddr = ModloaderHeap.alloc(file->m_filesize, 4096u, Hw::HW_ALLOC_PHYSICAL, 0);

			if (!pThis->m_pReadAddr)
			{
				LOGERROR("[DVDREAD] Failed to allocate memory for %s[%s]!", file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
				return 0;
			}

			pThis->m_Size = file->m_filesize;

			file->read(pThis->m_pReadAddr);

			Hw::DvdEnv::DebugUnregistReader(pThis);
			Hw::DvdEnv::DebugEndCurrentReader(pThis);

			pThis->m_State = Hw::cDvdReader::STATE_COMPLETE;

			return 1;
		}
		else
		{
			if (pThis->m_pReadAddr)
				FreeMemory(pThis->m_pReadAddr, 0);

			pThis->m_Size = 0;

			pThis->m_State = Hw::cDvdReader::STATE_CANCELED;

			Hw::DvdEnv::DebugUnregistReader(pThis);
			Hw::DvdEnv::DebugEndCurrentReader(pThis);

			LOGINFO("[DVDREAD] No size for %s, aborting!", file->m_path.c_str());

			return 1;
		}
	}
	return result;
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

CREATE_THISCALL(false, shared::base + 0x9EA800, void, Hw_cDvdReader_update, Hw::cDvdReader*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oHw_cDvdReader_update(pThis);

	if (pThis->m_State == Hw::cDvdReader::STATE_COMPLETE)
	{
		if (pThis->m_pReadAddr && !strcmp((char*)pThis->m_pReadAddr, "DAT\0"))
		{
			Hw::cFmerge holder;
			holder.m_data = (Hw::FmergeHeader*)pThis->m_pReadAddr;

			pThis->m_Size = ReplaceDataArchiveFile(&holder, pThis->m_Size, pThis->m_pFilePath, *(Hw::cHeap**)(*(DWORD**)pThis->m_pReadAddr - 1), Hw::HW_ALLOC_PHYSICAL);

			pThis->m_pReadAddr = holder.m_data;
		}
		return;
	}
	oHw_cDvdReader_update(pThis);
}

CREATE_HOOK(false, shared::base + 0x5825B0, int, __cdecl, BindCpk)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oBindCpk();

	if (!CriWare::iAvailableCPKs)
		return oBindCpk();

	if (CriWare::iAvailableCPKs)
	{
		for (size_t i = 0; i < CriWare::aBinders.getSize(); i++)
			CriWare::freeBinderWork(CriWare::aBinders[i]);

		CriWare::aBinders.m_Size = 0;
	}

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		prof->FileWalk([&](FileSystem::File& file) -> void
			{
				if (const char* ext = strrchr(file.getName(), '.'); ext && !strcmp(ext, ".cpk"))
				{
					Hw::cDvdCriFsBinder *pWork = CriWare::getFreeBinderWork();

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

	return oBindCpk();
}

CREATE_THISCALL(false, shared::base + 0x9F1320, int, LoadSound, const int, const int pData, const int pEnvData, const int pLoaderData)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oLoadSound(pThis, pData, pEnvData, pLoaderData);

	char* wemName = *(char**)(pData + 0x10);
	// LOGINFO("[LOADSND] Requested %s", wemName);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		FileSystem::File* file = prof->FindFile(wemName);

		if (!file)
			continue;

		if (strcmp(wemName, file->m_path.c_str()))
		{
			LOGINFO("[LOADSND] %s -> %s[%s]", wemName, file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
			strcpy(wemName, file->m_path.c_str());
		}
		return oLoadSound(pThis, pData, pEnvData, pLoaderData);

	}

	return oLoadSound(pThis, pData, pEnvData, pLoaderData);
}

void sHooks::Init()
{
	injector::MakeJMP(shared::base + 0x9EC18F, fixDvdReadRequest, true);
}