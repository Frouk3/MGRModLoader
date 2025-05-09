#include "Hooks.h"
#include <FileRead.h>
#include "ModLoader.h"
#include "gui.h"
#include "FilesTools.hpp"
#include <injector/injector.hpp>

CREATE_HOOK(false, shared::base + 0x61E8A0, void, __cdecl, InputUpdate)
{
	if (gui::GUIHotkey.m_bToggle)
		return;

	oInputUpdate();
}

CREATE_THISCALL(false, shared::base + 0x9D39D0, BOOL, VariableCreate, Hw::cHeapVariable*, size_t size, Hw::cHeap* heapowner, const char* targetAlloc)
{
	if (!ModLoader::bInit)
		return oVariableCreate(pThis, size, heapowner, targetAlloc);

	BOOL result = oVariableCreate(pThis, size, heapowner, targetAlloc);

	pThis->m_pReservedHeap = &ModloaderHeap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D4130, BOOL, PhysicalCreate, Hw::cHeapPhysical*, size_t size, Hw::cHeap* heapowner, const char* targetAlloc)
{
	if (!ModLoader::bInit)
		return oPhysicalCreate(pThis, size, heapowner, targetAlloc);

	BOOL result = oPhysicalCreate(pThis, size, heapowner, targetAlloc);

	pThis->m_pReservedHeap = &ModloaderHeap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D2AB0, BOOL, FixedHeapCreate, Hw::cHeapFixed*, size_t size, size_t allocSize, size_t preservedSize, Hw::cHeap* heapOwner, const char* target)
{
	if (!ModLoader::bInit)
		return oFixedHeapCreate(pThis, size, allocSize, preservedSize, heapOwner, target);

	BOOL result = oFixedHeapCreate(pThis, size, allocSize, preservedSize, heapOwner, target);

	pThis->m_pReservedHeap = &ModloaderHeap;

	return result;
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

			if (strcmp(filepath, file->m_path)) // Replace once and do not spam into the log
			{
				LOGINFO("[CRIWARE] Replacing %s -> %s...", filepath, file->m_path);
				strcpy(filepath, file->m_path);
				break;
			}
		}
	}

	return oCriFsFileLoad(loader);
}

CREATE_THISCALL(false, shared::base + 0xA9CBC0, void, FileRead_RegisterFile, FileRead::Work*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
	{
		oFileRead_RegisterFile(pThis);
		return;
	}

	if (strcmp((char*)pThis->m_Filedata, "DAT\0"))
	{
		oFileRead_RegisterFile(pThis);
		return;
	}

	DataArchiveHolder holder = DataArchiveHolder();
	holder.m_data = (char*)pThis->m_Filedata;

	pThis->m_nFilesize = ReplaceDataArchiveFile(&holder, pThis->m_nFilesize, pThis->m_Filename, pThis->m_Allocator, ((pThis->m_FileFlags & 0x20) != 0) + 1);

	pThis->m_Filedata = holder.m_data;

	oFileRead_RegisterFile(pThis);
};

CREATE_THISCALL(false, shared::base + 0xA9E170, int, FileRead_Load, FileRead::Work*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oFileRead_Load(pThis);

	if (pThis->m_FileFlags & 0x10000 || pThis->m_FileFlags & 0x8000) // Load original or load modified one or abort if the file was loaded
		return oFileRead_Load(pThis);

	Utils::formatPath(pThis->m_Filename);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		FileSystem::File* file = prof->FindFile(pThis->m_Filename);

		if (!file)
			continue;

		if (!file->m_filesize)
		{
			LOGWARNING("[FILEREAD] %s has no size, aborting and warning the manager...", file->m_path.c_str());

			FreeMemory(pThis->m_Filedata, 0);
			pThis->m_Filedata = nullptr;

			pThis->m_nFilesize = 0;

			pThis->m_WorkerState = pThis->FILEWORK_NOT_FOUND;
			pThis->m_nWaitAmount = 0;

			return 0;
		}

		FreeMemory(pThis->m_Filedata, 0);
		pThis->m_Filedata = nullptr;

		pThis->m_Filedata = ModloaderHeap.AllocateMemory(file->m_filesize, 0x1000, ((pThis->m_FileFlags & 0x20) != 0) + 1, 0);

		if (!pThis->m_Filedata)
		{
			// Panic, we can't allocate file data for modified file

			LOGERROR("[FILEREAD] Tried to allocate memory for modified file, but failed! [%s, %s]", file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
			pThis->cleanup();
			pThis->m_WorkerState = pThis->FILEWORK_REQUEST;
			pThis->m_FileFlags |= 0x10000;
			pThis->m_nWaitAmount = 0;
			return 1;
		}

		if (file->m_filesize == pThis->m_nFilesize)
			LOGINFO("[FILEREAD] Loading %s[%s]...", file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
		else
			LOGINFO("[FILEREAD] Replacing %s[%s] with %s[%s]", pThis->m_Filename, Utils::getProperSize(pThis->m_nFilesize).c_str(), file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());

		pThis->m_nFilesize = file->m_filesize;

		pThis->m_FileFlags |= 0x8000;

		file->read(pThis->m_Filedata);

		pThis->registerFile();

		pThis->m_WorkerState = pThis->FILEWORK_FINISHED;
		pThis->m_nWaitAmount = 0;

		return 1;
	}

	return oFileRead_Load(pThis);
}

CREATE_THISCALL(false, shared::base + 0x9EB160, BOOL, DvdReadRequest, Hw::cDvdFst::Work*, const char* filepath, void* filedata, size_t buffersize, int priority)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oDvdReadRequest(pThis, filepath, filedata, buffersize, priority);

	BOOL result = oDvdReadRequest(pThis, filepath, filedata, buffersize, priority);

	if (!result)
		return result;

	Utils::formatPath(pThis->m_Filepath);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		FileSystem::File* file = prof->FindFile(pThis->m_Filepath);

		if (!file)
			continue;

		if (file->m_filesize)
		{
			LOGINFO("[DVDREAD] Replacing %s[%s] -> %s[%s]", pThis->m_Filepath, Utils::getProperSize(pThis->m_Buffersize).c_str(), file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());

			int* block = (int*)pThis->m_Filedata - 1;
			Hw::cHeap* allocator = *(Hw::cHeap**)(block);

			FreeMemory(pThis->m_Filedata, 0); // as in hkLoad
			pThis->m_Filedata = ModloaderHeap.AllocateMemory(file->m_filesize, 4096u, 1, 0);

			if (!pThis->m_Filedata)
			{
				LOGERROR("[DVDREAD] Failed to allocate memory for %s[%s]!", file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
				return 0;
			}

			pThis->m_Buffersize = file->m_filesize;

			file->read(pThis->m_Filedata);

			((void(__cdecl*)(Hw::cDvdFst::Work*))(shared::base + 0x9E8EE0))(pThis);
			if (!*(int*)(shared::base + 0x19D4118) && !*(int*)(shared::base + 0x19D4120) || *(Hw::cDvdFst::Work**)(shared::base + 0x19D4124) == pThis)
				*(Hw::cDvdFst::Work**)(shared::base + 0x19D4124) = nullptr;

			pThis->m_State = 5;

			return 1;
		}
		else
		{
			if (pThis->m_Filedata)
				FreeMemory(pThis->m_Filedata, 0);

			pThis->m_Buffersize = 0;

			pThis->m_State = 5;

			((void(__cdecl*)(Hw::cDvdFst::Work*))(shared::base + 0x9E8EE0))(pThis);
			if (!*(int*)(shared::base + 0x19D4118) && !*(int*)(shared::base + 0x19D4120) || *(Hw::cDvdFst::Work**)(shared::base + 0x19D4124) == pThis)
				*(Hw::cDvdFst::Work**)(shared::base + 0x19D4124) = nullptr;

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

CREATE_THISCALL(false, shared::base + 0x9E9B30, int, DvdReadCheck, Hw::cDvdFst::Work*)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oDvdReadCheck(pThis);

	int result = oDvdReadCheck(pThis);

	if (pThis->m_State == 5)
	{
		if (pThis->m_Filedata && !strcmp((char*)pThis->m_Filedata, "DAT\0"))
		{
			DataArchiveHolder holder = DataArchiveHolder();
			holder.m_data = (char*)pThis->m_Filedata;

			pThis->m_Buffersize = ReplaceDataArchiveFile(&holder, pThis->m_Buffersize, pThis->m_Filepath, *(Hw::cHeap**)(*(DWORD**)pThis->m_Filedata - 1), 1);

			pThis->m_Filedata = holder.m_data;
		}
	}

	return result;
}

CREATE_HOOK(false, shared::base + 0x5825B0, int, __cdecl, BindCpk)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oBindCpk();

	if (!CriWare::iAvailableCPKs)
		return oBindCpk();

	if (CriWare::iAvailableCPKs)
	{
		for (int i = 0; i < CriWare::aBinders.size(); i++)
			CriWare::freeBinderWork(CriWare::aBinders[i]);

		CriWare::aBinders.m_size = 0;
	}

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		prof->FileWalk([&](FileSystem::File& file) -> void
			{
				if (const char* ext = strrchr(file.getName(), '.'); ext && !strcmp(file.getName(), ".cpk"))
				{
					CriFsBinderWork *pWork = CriWare::getFreeBinderWork();

					if (!pWork->bindCpkFileSync(file.m_path, 0, 0, 0x10000))
					{
						LOGERROR("[CRIWARE] Failed to sync %s archive.", file.m_path.c_str());
						return;
					}

					while (pWork->m_BindStatus == 1)
					{
						if (criFsBinder_GetStatus(pWork->m_BinderId, &pWork->m_BindStatus) || pWork->m_BindStatus != 1 && pWork->m_BindStatus != 2)
						{
							CriWare::freeBinderWork(pWork);
							break;
						}
					}

					if (pWork->m_BindStatus != 2)
					{
						LOGERROR("[CRIWARE] Failed to bind %s archive.", file.m_path.c_str());
						return;
					}

					LOGINFO("[CRIWARE] Bound %s archive successfully.", file.m_path.c_str());
					pWork->m_nStatus = 2;
				}
			});
	}

	return oBindCpk();
}

CREATE_THISCALL(false, shared::base + 0x9F1320, int, LoadSound, const int, const int pData, const int pEnvData, const int pLoaderData)
{
	if (!ModLoader::bInit || !ModLoader::bLoadFiles || !ModLoader::bLoadMods)
		return oLoadSound(pThis, pData, pEnvData, pLoaderData);

	for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		char* wemName = *(char**)(pData + 0x10);

		FileSystem::File* file = prof->FindFile(wemName);

		if (!file)
			continue;

		if (strcmp(wemName, file->m_path))
		{
			LOGINFO("[LOADSND] %s -> %s[%s]", wemName, file->m_path.c_str(), Utils::getProperSize(file->m_filesize).c_str());
			strcpy(wemName, file->m_path);
		}
		return oLoadSound(pThis, pData, pEnvData, pLoaderData);

	}

	return oLoadSound(pThis, pData, pEnvData, pLoaderData);
}

void sHooks::Init()
{
	injector::MakeJMP(shared::base + 0x9EC18F, fixDvdReadRequest, true);
}