#include "pch.h"
#include <assert.h>
#include "gui.h"
#include <Events.h>
#include "ModLoader.h"
#include <common.h>
#include <Hw.h>
#include <GameMenuStatus.h>
#include <shellapi.h>
#include <ctime>
#include <lib.h>
#include <Behavior.h>

char LOG_PATH[MAX_PATH];

FILE* logFile = nullptr;

#define criFsBinder_create(binderPtr) ((int(__cdecl *)(int*))(shared::base + 0xE971BB))(binderPtr)
#define criFsBinder_free(binderPtr) ((int(__cdecl *)(int))(shared::base + 0xE97C59))(binderPtr)
#define criFsBinder_GetStatus(a1, status) ((int(__cdecl *)(int, int *))(shared::base + 0xE97D56))(a1, status)
#define criFsBinder_bindCpk(binderHn, srcBinderHn, path, work, worksize, binderId) ((int(__cdecl*)(int, int, const char*, void*, CriSint32, int*))(shared::base + 0xE98448))(binderHn, srcBinderHn, path, work, worksize, binderId)

typedef unsigned int CriUint32;
typedef signed int CriSint32;

struct CriFsConfig
{
	CriUint32 thread_model;
	CriSint32 num_binders;
	CriSint32 num_loaders;
	CriSint32 num_group_loaders;
	CriSint32 num_stdio_handles;
	CriSint32 num_installers;
	CriSint32 max_binds;
	CriSint32 max_files;
	CriSint32 max_path;
};

namespace CRI
{
	struct criFsBinderWork
	{
		int m_nStatus;
		int m_CriBinder;
		int m_BinderId;
		int m_nBindStatus;
		int m_nPriority;

		criFsBinderWork()
		{
			m_nStatus = 0;
			m_CriBinder = 0;

			m_nBindStatus = 6;
			m_nPriority = -1;
		}
	};

	lib::StaticArray<criFsBinderWork, 256> m_binders;

	criFsBinderWork *getFreeBinder()
	{
		criFsBinderWork* binder = nullptr;
		if (m_binders.m_nSize < m_binders.m_nCapacity)
			binder = &m_binders[m_binders.m_nSize++];

		if (binder)
			*binder = criFsBinderWork();
		
		return binder;
	}

	void freeBinder(criFsBinderWork& binder)
	{
		if (binder.m_CriBinder)
			criFsBinder_free(binder.m_CriBinder);
	}

	inline int cpkAmount = 0;
};

#define HwCDvdFst_getSourceBinder(path) ((CRI::criFsBinderWork *(__cdecl *)(char*))(shared::base + 0x9EAED0))(path)

void openLog()
{
	if (!logFile)
		logFile = fopen(LOG_PATH, "w");
}

void closeLog()
{
	if (logFile)
	{
		fclose(logFile);
		logFile = nullptr;
	}
}

BOOL FileExists(const char* filename)
{
	struct stat buffer;
	return (stat(filename, &buffer) == 0);
}

#pragma comment(lib, "urlmon.lib")

inline void __cdecl dbgPrint(const char* fmt, ...)
{
	if (!ModLoader::bEnableLogging)
		return;

	if (!logFile)
		openLog();

	if (logFile)
	{
		char fmtBuffer[512];
		ZeroMemory(fmtBuffer, sizeof(fmtBuffer));

		SYSTEMTIME st;

		GetLocalTime(&st);
		va_list va;
		va_start(va, fmt);
		vsprintf(fmtBuffer, fmt, va);
		fprintf(logFile, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, fmtBuffer);

		fflush(logFile);

		va_end(va);
	}
}

constexpr float MOD_LOADER_VERSION = 2.0f;

struct FileRead
{
	struct Work
	{
		int field_0;
		int field_4;
		char m_FileName[32];
		int field_28;
		int field_2C;
		void* m_Placeholder;
		int m_nFilesize;
		Hw::cHeapPhysical* m_pAllocator;
		int m_nFileFlags;
		int field_40;
		int field_44;
		int field_48;
		int m_nWorkerState;
		int m_nWaitAmount;
		int field_54;
		int field_58;

		void registerFile()
		{
			((void(__thiscall*)(FileRead::Work*))(shared::base + 0xA9CBC0))(this);
		}

		void unregisterFile()
		{
			((void(__thiscall*)(FileRead::Work*))(shared::base + 0xA9CC50))(this);
		}

		BOOL cleanup()
		{
			return ((BOOL(__thiscall*)(FileRead::Work*))(shared::base + 0xA9D9A0))(this);
		}
	};

	struct Manager
	{
		int field_0;
		int field_4;
		Hw::cHeapFixed m_FileReadFactory;
		Hw::cFixedVector<FileRead::Work*> m_FileReaderVector;
		int field_7C;
		int field_80;
		int field_84;
		int field_88;
		int field_8C;
		int field_90;
		int field_94;
		int field_98;
		int field_9C;

		static inline Manager& Instance = *(Manager*)(shared::base + 0x19DA840);
	};
};

#include "imgui/imgui.h"
#include "include/MinHook.h"

size_t getFileSize(const char* file)
{
	if (!FileExists(file))
		return -1;

	auto fFile = fopen(file, "rb");
	if (!fFile)
		return -1;
	fseek(fFile, 0L, SEEK_END);
	auto filesize = ftell(fFile);
	fclose(fFile);

	return filesize;
}

typedef int(__thiscall* load_t)(FileRead::Work* ecx);
static load_t oLoad = NULL;

typedef size_t(__cdecl* getFilesize_t)(const char*);
static getFilesize_t oGetFilesize = NULL;

typedef int(__thiscall*dvdload_t)(int ecx);
static dvdload_t oDvdload = NULL;

typedef int(__cdecl* bindCpkMaybe_t)();
static bindCpkMaybe_t oBindCpk = NULL;

typedef int(__thiscall* loadSound_t)(int ecx, int pData, int pEnvData, int pLoaderData);
static loadSound_t oLoadSound = NULL;

typedef int(__cdecl *criload_t)(int);
static criload_t oCriFsFileLoad = NULL;

#define CREATE_HOOK(ret, callconv, name, ...) typedef ret(callconv *name##_t)(__VA_ARGS__); static name##_t o##name = NULL; ret callconv hk##name(__VA_ARGS__)

#define CREATE_THISCALL(ret, thus, name, ...) typedef ret(__thiscall *name##_t)(thus, __VA_ARGS__); static name##_t o##name = NULL; ret __stdcall hk##name(__VA_ARGS__) {thus self; __asm {mov [self], ecx}

CREATE_THISCALL(BOOL, Hw::cHeapVariable*, VariableCreate, size_t size, Hw::cHeap* heapowner, const char* targetAlloc)
auto result = oVariableCreate(self, size, heapowner, targetAlloc);
self->m_pReservedHeap = Hw::cHeapGlobal::get();
return result;
}

CREATE_THISCALL(BOOL, Hw::cHeapPhysical*, PhysicalCreate, size_t size, Hw::cHeap* heapowner, const char* targetAlloc)
auto result = oPhysicalCreate(self, size, heapowner, targetAlloc);
self->m_pReservedHeap = Hw::cHeapGlobal::get();
return result;
}

CREATE_THISCALL(BOOL, Hw::cHeapFixed*, FixedHeapCreate, size_t size, size_t allocSize, size_t preservedSize, Hw::cHeap* heapOwner, const char* target)
auto result = oFixedHeapCreate(self, size, allocSize, preservedSize, heapOwner, target);
self->m_pReservedHeap = Hw::cHeapGlobal::get();
return result;
}

int __cdecl hkCriFsFileLoad(int loader) // load here .usm // I could use this function to load files through it, BUT as our Platinum Games perfectly did their heap managing, I need to implement loading for different loading function
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		if (*(int*)(loader + 0xB4) != 1u)
			return 2;

		char* filepath = *(char**)(loader + 0xEC);

		Utils::formatPath(filepath); // format it just in case CriWare doesn't

		const char* ext = strrchr(filepath, '.');

		if (!strcmp(ext, ".usm")/* || !strcmp(ext, ".wem")*/)
		{
			for (auto& prof : ModLoader::Profiles)
			{
				if (!prof->m_bEnabled)
					continue;

				auto file = prof->FindFile(filepath);

				if (!file) // If we didn't found any files in the folder, try to search the file outside
					file = prof->FindFile(strrchr(filepath, '\\') ? strrchr(filepath, '\\') + 1 : nullptr);

				if (file && strcmp(filepath, file->m_path)) // Replace once and do not spam into the log
				{
					LOGINFO("[CRIWARE ] Replacing %s -> %s...", filepath, file->m_path);
					strcpy(filepath, file->m_path);
					break;
				}
			}
		}

		// LOGINFO("[CRIWARE ] Loader %s / %d status", *(const char**)(loader + 0xEC), *(int*)(*(int*)(loader + 0xA8) + 0xC));
	}

	return oCriFsFileLoad(loader);
}

typedef BOOL(__thiscall* sub_9EB160)(int ecx, const char* filepath, void* filedata, size_t buffSize, int a5);

static sub_9EB160 oDvdReadInit = NULL;

BOOL __fastcall hkDvdReadInit(int ecx, void*, const char* filepath, void* filedata, size_t buffSize, int a5)
{
	
	return oDvdReadInit(ecx, filepath, filedata, buffSize, a5);
}

size_t __cdecl hkGetFilesize(const char* file) // PgIOHookDefferedCRI also uses this function to tell what size should buffer take
{
	auto size = oGetFilesize(file);
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		auto extension = strrchr(file, '.');
		if (size == 0 || !strcmp(extension, ".wem"))
		{
			for (auto& prof : ModLoader::Profiles)
			{
				if (!prof->m_bEnabled)
					continue;

				if (auto ffile = prof->FindFile(file) ? prof->FindFile(file) : prof->FindFile(strrchr(file, '\\') ? strrchr(file, '\\') + 1 : nullptr); ffile && ffile->m_nSize)
					return (int)ffile->m_nSize;
			}
		}
	}
	return size;
}

int __fastcall hkLoad(FileRead::Work* ecx)
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad && !(ecx->m_nFileFlags & 0x10000)) 
	{
		// Please fix the platinum fuck up in path
		Utils::formatPath(ecx->m_FileName);

		for (auto& prof : ModLoader::Profiles)
		{
			if (!(ecx->m_nFileFlags & 0x2000))
			{
				auto file = prof->FindFile(ecx->m_FileName);
				if (!file)
					file = prof->FindFile(strrchr(ecx->m_FileName, '\\') ? strrchr(ecx->m_FileName, '\\') + 1 : nullptr);

				if (file)
					ecx->m_nFileFlags |= 0x2000;
			}

			if (!prof->m_bEnabled)
				continue;

			auto file = prof->FindFile(ecx->m_FileName); 
			if (!file) 
				file = prof->FindFile(strrchr(ecx->m_FileName, '\\') ? strrchr(ecx->m_FileName, '\\') + 1 : nullptr); // Please someone teach the user to install mods...

			if (auto fSize = file ? file->m_nSize : -1; fSize != -1 && ecx->m_Placeholder && !(ecx->m_nFileFlags & 0x8000))
			{
				size_t fileSize = ecx->m_nFilesize;
				// result = oLoad(a1, a2, path, a4, a5, size, a7, placeholder, a8, a9);

				if (fSize)
				{
					if (fileSize != fSize)
						LOGINFO("[FILEREAD] Replacing %s(%s) -> %s(%s)", ecx->m_FileName, Utils::getProperSize(ecx->m_nFilesize).c_str(), file->m_path, Utils::getProperSize(fSize).c_str());

					if (fileSize == fSize)
						LOGINFO("[FILEREAD] Loading %s(%s)...", file->m_path, Utils::getProperSize(fSize).c_str());
				}
				else
				{
					LOGINFO("[FILEREAD] %s -> This file will not be loaded due to no size!", file->m_path);

					FreeMemory(ecx->m_Placeholder, 0); // Clear the heap
					ecx->m_Placeholder = nullptr;

					ecx->m_nFilesize = fSize; // set size to zero

					ecx->m_nWorkerState = 7; // Warn the manager that this file can't be loaded due to not having size
					ecx->m_nWaitAmount = 0;

					return 0;
				}

				FreeMemory(ecx->m_Placeholder, 0); // free memory for allocator
				ecx->m_Placeholder = nullptr;

				ecx->m_Placeholder = ecx->m_pAllocator->AllocateMemory(fSize, 4096u, ((ecx->m_nFileFlags & 0x20) != 0) + 1, 0); // and allocate it again but with different size

				if (!ecx->m_Placeholder)
				{
					LOGERROR("[FILEREAD] Failed to allocate memory for %s(%s)!", file->m_path, Utils::getProperSize(fSize).c_str());
					ecx->cleanup();
					ecx->m_nWorkerState = 1;
					ecx->m_nFileFlags |= 0x10000; // hoping that it will load original file
					ecx->m_nWaitAmount = 0;
					return 1;
				}

				if (fileSize == fSize)
					ecx->m_nFileFlags |= 0x4000;

				ecx->m_nFilesize = fSize;

				file->read(ecx->m_Placeholder);

				ecx->m_nFileFlags |= 0x8000; // was modified by 

				ecx->m_nWorkerState = 6;
				ecx->m_nWaitAmount = 0;

				ecx->registerFile(); // we still need to register it
				
				return 0;
			}
		}
	}
	// LOG("[FILEREAD] Loading original %s...", ecx->m_FileName);
	return oLoad(ecx);
}

// FileRead::Work -> cDvdReader
// cDvdReader is implementation of reader(from cpk's or from GameData)
// FileRead::Work is used to load .dat files and so on(mainly .dat because its archive)

int __fastcall hkDvdload(int ecx)
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		Utils::formatPath((char*)(ecx + 0xC));

		char* filename = (char*)(ecx + 0xC);
		int &fileSize = *(int*)(ecx + 0x58);

		for (auto& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			auto file = prof->FindFile(filename);
			if (!file)
				file = prof->FindFile(strrchr(filename, '\\') ? strrchr(filename, '\\') + 1 : nullptr);

			if (file && *(int*)(ecx + 0x54))
			{
				auto size = file->m_nSize;

				if (size)
				{
					// Same size behavior

					LOGINFO("[DVDREAD ] Replacing %s(%s) -> %s(%s)", filename, Utils::getProperSize(fileSize).c_str(), file->m_path, Utils::getProperSize(size).c_str());

					int block = *(int*)(ecx + 0x54) - 4;
					Hw::cHeap* allocator = *(Hw::cHeap**)(block);

					FreeMemory(*(void**)(ecx + 0x54), 0); // as in hkLoad
					*(void**)(ecx + 0x54) = allocator->AllocateMemory(size, 4096u, 1, 0);

					if (!*(void**)(ecx + 0x54))
					{
						LOGERROR("[DVDREAD ] Failed to allocate memory for %s(%s)!", file->m_path, Utils::getProperSize(size).c_str());
						return 0;
					}

					fileSize = size;

					file->read(*(void**)(ecx + 0x54));

					*(int*)ecx = 5;
					return 0;
				}
				else
				{
					if (*(void**)(ecx + 0x54))
						FreeMemory(*(void**)(ecx + 0x54), 0);

					fileSize = 0;

					*(int*)ecx = 5;

					LOGINFO("No size for %s, aborting!", file->m_path);

					return 0;
				}
			}
		}
	}
	// LOG("[DVDREAD] Loading original %s...", (char*)(ecx + 0xC));
	return oDvdload(ecx);
}

int __cdecl hkBindCpk()
{
	auto result = oBindCpk();
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad && CRI::cpkAmount)
	{
		// Firstly, remove all the binders if they exist

		if (CRI::m_binders.m_nSize)
		{
			for (int i = 0; i < CRI::m_binders.m_nSize; i++)
			{
				CRI::freeBinder(CRI::m_binders[i]);
			}
		}

		CRI::m_binders.m_nSize = 0;

		for (auto& profile : ModLoader::Profiles)
		{
			if (!profile->m_bEnabled)
				continue;

			profile->FileWalk([&](ModLoader::ModProfile::File* file)
				{
					if (!strcmp(Utils::strlow(&file->m_path[strlen(file->m_path) - 4]), ".cpk") && file->m_nSize)
					{
						auto binder = CRI::getFreeBinder();
						if (binder)
						{
							if (binder->m_nStatus)
								return;

							auto fsBinder = &binder->m_CriBinder;

							if (criFsBinder_create(&binder->m_CriBinder) || !fsBinder)
							{
								criFsBinder_free(binder->m_CriBinder);
								binder->m_CriBinder = 0;
							}

							auto srcBinder = HwCDvdFst_getSourceBinder(file->m_path);

							if (criFsBinder_bindCpk(*fsBinder, srcBinder ? srcBinder->m_CriBinder : 0, file->m_path, 0, 0, &binder->m_BinderId))
							{
								LOGERROR("[BINDCPK ] Sync failed to bind %s", file->m_path);
								if (*fsBinder)
								{
									criFsBinder_free(*fsBinder);
									*fsBinder = NULL;

									binder->m_nStatus = 0;
									binder->m_nPriority = -1;
								}
							}
							else
							{
								binder->m_nBindStatus = 1;
								binder->m_nStatus = 2;
								binder->m_nPriority = 0;

								while (binder->m_nBindStatus == 1)
								{
									if (criFsBinder_GetStatus(binder->m_BinderId, &binder->m_nBindStatus) || binder->m_nBindStatus != 1 && binder->m_nBindStatus != 2)
									{
										if (binder->m_CriBinder)
										{
											criFsBinder_free(binder->m_CriBinder);
											binder->m_CriBinder = 0;
										}
										binder->m_nStatus = 0;
										binder->m_nPriority = -1;
									}
								}

								if (binder->m_nBindStatus == 2)
								{
									LOGINFO("[BINDCPK ] %s is bound successfully!", file->m_path);
									binder->m_nStatus = 2;
								}
							}
						}
					}
				});
		}
	}
	return result;
}

int __fastcall hkLoadSound(const int ecx, void*, const int pData, const int pEnvData, const int pLoaderData)
{
	// LOGINFO("[LOADSND ] AK::StreamMgr -> %s", *(char**)(pData + 0x10));
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		for (auto& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			char* wemName = *(char**)(pData + 0x10);

			auto file = prof->FindFile(wemName);
			if (!file)
				file = prof->FindFile(strrchr(wemName, '\\') ? strrchr(wemName, '\\') + 1 : nullptr);

			if (file)
			{
				if (strcmp(wemName, file->m_path))
				{
					LOGINFO("[LOADSND ] %s -> %s", wemName, file->m_path);
					strcpy(wemName, file->m_path);
				}
				return oLoadSound(ecx, pData, pEnvData, pLoaderData);
			}
		}
	}
	return oLoadSound(ecx, pData, pEnvData, pLoaderData);
}

bool bUpdateAvailable = false;
bool bUserNoticed = false;
float newVersion;

bool CheckUpdates()
{
	char buff[MAX_PATH];
	GetEnvironmentVariableA("TEMP", buff, sizeof(buff));
	strcat(buff, "/MODLOADERVER.ini");
	LOGINFO("Checking updates...");
	if (URLDownloadToFileA(NULL, "https://raw.githubusercontent.com/Frouk3/ModMenuVersions/main/MODLOADERVERSION.ini", buff, NULL, NULL) == S_OK)
	{
		char str[16];
		GetPrivateProfileStringA("Metal Gear Rising Revengeance", "VERSION", "-1.0", str, sizeof(str), buff);
		newVersion = (float)atof(str);

		if (newVersion > MOD_LOADER_VERSION)
		{
			LOGINFO("New version is available!");
			return true;
		}
		else if (newVersion == -1.0f || newVersion <= MOD_LOADER_VERSION)
		{
			LOGINFO("Updates are not available.");
			return false;
		}
	}
	else
	{
		LOGERROR("Failed to check updates!");
		return false;
	}
	return false;
}

int checkUpdate(LPVOID)
{
	bUpdateAvailable = CheckUpdates();
	return 0;
}

//inline void* __cdecl operator new(size_t s)
//{
//	if (myHeapHandle == 0)
//	{
//		EnterSpinLocked();
//
//		assert(myHeapHandle = HeapCreate(0u, 0, 0));
//
//		LeaveSpinLocked();
//	}
//	auto heap = HeapAlloc(myHeapHandle, 0, s);
//	if (heap)
//		memset(heap, 0, s);
//
//	return heap;
//}
//
//inline void __cdecl operator delete(void* block, size_t s)
//{
//	if (block)
//	{
//		HeapFree(myHeapHandle, 0u, block);
//	}
//}
//
//inline void* malloc(size_t size)
//{
//	if (!myHeapHandle)
//	{
//		EnterSpinLocked();
//
//		assert(myHeapHandle = HeapCreate(0u, 0, 0));
//
//		LeaveSpinLocked();
//	}
//	auto heap = HeapAlloc(myHeapHandle, 0, size);
//	if (heap)
//		memset(heap, 0, size);
//
//	return heap;
//}
//
//inline void free(LPVOID block)
//{
//	if (block)
//	{
//		HeapFree(myHeapHandle, 0u, block);
//	}
//}

typedef void(__cdecl* CriErrCbFunc_t)(const char* errid, unsigned int p1, unsigned int p2, unsigned int* parray);

void __cdecl criErr_Callback(const char* errId, unsigned int p1, unsigned int p2, unsigned int* parray)
{
	char buff[1024];

	sprintf(buff, errId, p1, p2, parray);

	LOGERROR("[CRIWARE ] %s", buff);
}

bool bShouldReload = false;

const char* GetNameFromKey(int vKey)
{
	static char name[128];
	auto scanCode = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
	switch (vKey)
	{
	case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
	case VK_RCONTROL: case VK_RMENU:
	case VK_LWIN: case VK_RWIN: case VK_APPS:
	case VK_PRIOR: case VK_NEXT:
	case VK_END: case VK_HOME:
	case VK_INSERT: case VK_DELETE:
	case VK_DIVIDE:
	case VK_NUMLOCK:
		scanCode |= KF_EXTENDED;
		break;
	default:
		break;
	}
	GetKeyNameText(scanCode << 16, (LPSTR)name, 128);
	return vKey == 0 ? "Off" : name;
}

bool rebindKey(int& key)
{
	for (int i = 0; i < 256; i++)
	{
		auto keyPress = shared::IsKeyPressed(i);

		if (i == VK_ESCAPE && keyPress)
		{
			key = 0;
			return true;
		}

		if ((i == VK_LBUTTON || i == VK_RBUTTON) && keyPress)
			continue;

		if (keyPress)
		{
			key = i;

			return true;
		}
	}

	return false;
}

void DrawHotkeys(const char *label, int *mainKey, int *additionalKey, bool *rebinds, int& keyOrder)
{
	if (!rebinds)
		return;

	ImGui::PushID(label);
	ImGui::Text(label);
	ImGui::PushID("##1");
	{
		ImGui::SameLine();
		if (!*rebinds && ImGui::Button(GetNameFromKey(!mainKey ? 0 : *mainKey)))
		{
			keyOrder = 0;
			*rebinds = true;
		}
		else if (*rebinds && ImGui::Button("..."))
			*rebinds = false;
	}
	ImGui::PopID();

	ImGui::PushID("##2");
	{
		ImGui::SameLine();
		if (!rebinds[1] && ImGui::Button(GetNameFromKey(!additionalKey ? 0 : *additionalKey)))
		{
			keyOrder = 1;
			rebinds[1] = true;
		}
		else if (rebinds[1] && ImGui::Button("..."))
			rebinds[1] = false;
	}
	ImGui::PopID();
	ImGui::PopID();

	if (*rebinds && mainKey && keyOrder == 0)
	{
		if (rebindKey(*mainKey))
		{
			++keyOrder;
			*rebinds = false;
			Sleep(40u);
		}
	}
	else if (rebinds[1] && additionalKey && keyOrder == 1)
	{
		if (rebindKey(*additionalKey))
		{
			++keyOrder;
			rebinds[1] = false;
			Sleep(40u);
		}
	}
}

class Plugin
{
public:
	static inline void InitGUI()
	{
		Events::OnDeviceReset.before += gui::OnReset::Before;
		Events::OnDeviceReset.after += gui::OnReset::After;
		Events::OnPresent += gui::OnEndScene;
	}

	Plugin()
	{	
		GetCurrentDirectoryA(sizeof(ModLoader::path), ModLoader::path);

		sprintf(LOG_PATH, "%s\\LoaderLog.txt", ModLoader::path);

		openLog();

		/// FOR HEAP MANAGER TESTING PURPOSES
		/*
		for (int i = 0; i < 32; i++)
		{
			auto heapMgr = GetHeapManager();

			auto mem = (int*)heapMgr->allocate(8u, false);

			*mem = shared::random(1, 10);
			mem[1] = shared::random(1, 10);
		}

		for (auto block = GetHeapManager()->m_pLast; block; block = block->m_pPrev)
		{
			int* mem = (int*)block->getMemoryBlock();
			LOG("Heap at %X(size: %d), value = %d, %d", block, block->m_nSize, *mem, mem[1]);

			block->m_pAllocator->free(block);

			LOG("First %X, last %X, prev %X", GetHeapManager()->m_pFirst, GetHeapManager()->m_pLast, GetHeapManager()->m_pPrev);
		}
		*/
		/// 

		// remove(LOG_PATH); // remove it so we won't mess up latest log(and eventually making it bigger)

		assert(MH_Initialize() == MH_OK);

		Events::OnGameStartupEvent += []()
			{
				CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)checkUpdate, NULL, NULL, NULL);
			};

		ModLoader::startup();

		Events::OnUpdateEvent += []()
			{
				if (bIsForegroundWindow && g_GameMenuStatus == InMenu)
				{
					if (ModLoader::aKeys[0] == 0 && ModLoader::aKeys[1] != 0)
					{
						ModLoader::aKeys[1] == 0;
					}

					if (ModLoader::aKeys[0] == 0 && ModLoader::aKeys[1] == 0)
						gui::bShow = true;
					else if (ModLoader::aKeys[0] != 0 && ModLoader::IsGUIKeyPressed())
						gui::bShow ^= true;

				//	if (bShouldReload && !gui::bShow) // Hot Reload is still in development
				//	{
				//		// ((void(__cdecl*)())(shared::base + 0x5C9100))();
				//		// ((void(__cdecl*)())(shared::base + 0x5825B0))();

				//		// ((void(__thiscall*)(void*, unsigned int, const char*, int))(shared::base + 0x64AC40))((void*)(shared::base + 0x17E8E40), 0xF01, "START", -1);

				//		/*FileRead::Manager::Instance.field_80 |= 1u;

				//		if (((BOOL(__thiscall*)(void*, void*))(shared::base + 0xAB4340))((void*)(shared::base + 0x1ADC6C0), *(void**)(shared::base + 0x17E8E40)))
				//		{
				//			for (auto& reader : FileRead::Manager::Instance.m_FileReaderVector)
				//			{
				//				if (reader->m_nFileFlags & 0x2000)
				//				{
				//					reader->m_nFileFlags |= 8u;
				//					reader->m_nWorkerState = 9;
				//				}
				//			}

				//			bShouldReload = false;
				//		}*/

				//		bShouldReload = false;
				//	}
				}
			};

		Events::OnMainCleanupEvent += []()
			{
				ModLoader::Save();

				for (auto& prof : ModLoader::Profiles)
					prof->Shutdown();

				closeLog();
			};

		for (auto& prof : ModLoader::Profiles)
		{
			prof->FileWalk([&](ModLoader::ModProfile::File* file)
				{
					if (!strcmp(Utils::strlow(&file->m_path[strlen(file->m_path) - 4]), ".cpk") && file->m_nSize)
					{
						CRI::cpkAmount += 1;
					}
				});
		}

		LOGINFO("Total of %d .cpk's", CRI::cpkAmount);

		{
			injector::scoped_unprotect vp(shared::base + 0x14CDE20, 4u);

			*(int*)(shared::base + 0x14CDE20) += CRI::cpkAmount;
		}



		/// Will not work for every heap
		// // making each heap manager to have global heap manager as backup, making game load as much as it wants to
		//Events::OnGameStartupEvent += []()
		//	{
		//		static bool once = false;
		//		if (!once)
		//		{
		//			auto heap = Hw::cHeapGlobal::get()->m_pPrev;
		//			do
		//			{
		//				if (!heap->m_pReservedHeap && heap != Hw::cHeapGlobal::get())
		//					heap->m_pReservedHeap = Hw::cHeapGlobal::get();

		//				// LOGINFO("Set backup for %s", heap->m_TargetAlloc);

		//				heap = heap->m_pNext;
		//			} while (heap);
		//			once = true;
		//		}
		//	};

#define HOOK(target, hooked, original) MH_CreateHook((LPVOID)(target), hooked, (LPVOID*)&original); MH_EnableHook((LPVOID)(target));

		HOOK(shared::base + 0xA9E170, hkLoad, oLoad);
		HOOK(shared::base + 0x9EAF60, hkGetFilesize, oGetFilesize);
		HOOK(shared::base + 0x9E9900, hkDvdload, oDvdload);
		HOOK(shared::base + 0x5825B0, hkBindCpk, oBindCpk);
		HOOK(shared::base + 0x9F1320, hkLoadSound, oLoadSound);
		HOOK(shared::base + 0xE9C0F6, hkCriFsFileLoad, oCriFsFileLoad);
		HOOK(shared::base + 0x9EB160, hkDvdReadInit, oDvdReadInit);

		HOOK(shared::base + 0x9D39D0, hkVariableCreate, oVariableCreate);
		HOOK(shared::base + 0x9D4130, hkPhysicalCreate, oPhysicalCreate);
		HOOK(shared::base + 0x9D2AB0, hkFixedHeapCreate, oFixedHeapCreate);

		*(CriErrCbFunc_t**)(shared::base + 0x1CAE15C) = (CriErrCbFunc_t*)criErr_Callback;

		InitGUI();

#undef HOOK

	}
} plugin;


const char* formatFloatPrecision(char* buff)
{
	char* chr = strrchr(buff, '0');

	if (chr)
	{
		while (chr)
		{
			if (!chr)
				break;

			if (*chr == '\0')
				break;

			if (chr[1] != '\0' && chr[1] != '0')
				break;

			if (*(chr - 1) == '.')
				break;

			if (*chr == '0')
			{
				if (chr[1] == '\0')
					*chr = '\0';

				if (chr[1] == '0')
					*chr = '\0';
			}

			chr = strrchr(buff, '0');
		}
	}

	return buff;
}

void gui::RenderWindow()
{
	if (!bUserNoticed && bUpdateAvailable)
		ImGui::OpenPopup("Update checker");
	if (ImGui::BeginPopupModal("Update checker", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		char version[32], newVer[32];

		sprintf(version, "%f", MOD_LOADER_VERSION);
		sprintf(newVer, "%f", newVersion);

		formatFloatPrecision(version);
		formatFloatPrecision(newVer);

		ImGui::Text("New update is available(New: %s, Installed: %s)", newVer, version);
		if (ImGui::Button("OK"))
		{
			ShellExecute(NULL, "open", "https://www.nexusmods.com/metalgearrisingrevengeance/mods/650?tab=files", NULL, NULL, FALSE);
			ImGui::CloseCurrentPopup();
			bUserNoticed = true;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("(Pressing OK will redirect you to Nexusmods page where you can download newest version of Mod Loader)");
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
			bUserNoticed = true;
		}
		ImGui::EndPopup();
	}

	if (!bShow || g_GameMenuStatus != InMenu)
		return;

	if (!ModLoader::bInit || ModLoader::bInitFailed)
	{
		ImGui::Begin("Mod Loader", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
		ImGui::Text("Initialization failed.");
		ImGui::End();
		return;
	}
	if (ModLoader::aKeys[0] == 0)
	{
		ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
	}
	ImGui::Begin("Mod Loader", NULL, ModLoader::aKeys[0] == 0 ? 0 : ImGuiWindowFlags_NoCollapse);
	if (ImGui::BeginTabBar("##LOADERTAB"))
	{
		if (ImGui::BeginTabItem("Mods"))
		{
			if (ImGui::BeginTable("##ModsTable", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders))
			{
				ImGui::TableSetupColumn("Enabled");
				ImGui::TableSetupColumn("Mod Name");
				ImGui::TableSetupColumn("Priority");
				ImGui::TableSetupColumn("Author");
				ImGui::TableHeadersRow();
				for (auto& prof : ModLoader::Profiles)
				{
					ImGui::TableNextRow();
					ImGui::PushID(prof->m_name.c_str());

					Utils::String modName;

					modName = prof->m_name;

					if (prof->m_ModInfo && prof->m_ModInfo->m_title)
						modName = prof->m_ModInfo->m_title;


					ImGui::TableNextColumn();
					if (ImGui::Checkbox("##IsEnabled", &prof->m_bEnabled))
						bShouldReload = true;

					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Some mods require game restart!");
					}
					
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(modName);

					if (ImGui::IsItemClicked())
						prof->m_bEnabled ^= true;

					if (ImGui::IsItemHovered())
					{
						if (prof->m_ModInfo && prof->m_ModInfo->m_description)
							ImGui::SetTooltip("%s\n%s\n[%s]", prof->m_ModInfo->m_description.c_str(), prof->m_ModInfo->m_date.c_str(), Utils::getProperSize(prof->m_nTotalSize).c_str());
						else
							ImGui::SetTooltip("<No description given>\n[%s]", Utils::getProperSize(prof->m_nTotalSize).c_str());
					}

					ImGui::TableNextColumn();
					ImGui::PushItemWidth(80.f);
					if (ImGui::InputInt("##Priority", &prof->m_nPriority))
						ModLoader::SortProfiles();

					ImGui::PopItemWidth();

					if (prof->m_ModInfo)
					{
						if (prof->m_ModInfo->m_author && prof->m_ModInfo->m_authorURL)
						{
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(prof->m_ModInfo->m_author.c_str());
							if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
								ShellExecuteA(NULL, "open", prof->m_ModInfo->m_authorURL.c_str(), NULL, NULL, NULL);
						}
						else if (prof->m_ModInfo->m_author)
						{
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(prof->m_ModInfo->m_author.c_str());
						}
					}

					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Mod Info"))
		{
			if (ImGui::CollapsingHeader("Modified Files"))
			{
				auto modifiedFile = 0;
				auto loadedFiles = 0;
				for (auto& file : FileRead::Manager::Instance.m_FileReaderVector)
				{
					modifiedFile += (file->m_nFileFlags & 0x8000) != 0;
					loadedFiles += (file->m_nFileFlags & 0x4000) != 0;
				}
				ImGui::Text("Modified files: %d\nLoaded Files: %d", modifiedFile, loadedFiles);
				for (auto& file : FileRead::Manager::Instance.m_FileReaderVector)
				{
					if (file->m_nFileFlags & 0x8000 || file->m_nFileFlags & 0x4000)
						ImGui::BulletText("%s", file->m_FileName);
				}
			}
			long long totalSize = 0L;

			for (auto& prof : ModLoader::Profiles)
				totalSize += prof->m_bEnabled ? prof->m_nTotalSize : 0;

			ImGui::Text("Total size of enabled mods: %s", Utils::getProperSize(totalSize));
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Settings"))
		{
			static bool bRebinds[2] = { false };
			static int iKeyOrder = 0;
			DrawHotkeys("Menu Keys", &ModLoader::aKeys[0], &ModLoader::aKeys[1], bRebinds, iKeyOrder);
			ImGui::Separator();
			ImGui::Checkbox("Don't load files", &ModLoader::bIgnoreDATLoad);
			ImGui::Checkbox("Don't load scripts", &ModLoader::bIgnoreScripts);
			if (ImGui::Checkbox("Enable logging", &ModLoader::bEnableLogging) && !ModLoader::bEnableLogging)
			{
				if (FileExists(LOG_PATH))
					remove(LOG_PATH); // delete the log because user surely doesn't want it
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Enable/disable writing into a log file\nIt surely helps developers to find the issue you've encountered with");
			if (ImGui::Button("Save"))
				ModLoader::Save();
			ImGui::SameLine();
			if (ImGui::Button("Load"))
				ModLoader::Load();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("About"))
		{
			char ver[32];

			sprintf(ver, "%f", MOD_LOADER_VERSION);

			formatFloatPrecision(ver);

			ImGui::Text("Mod Loader (%s)", ver);
			ImGui::Text("Build : (%s | %s)", __TIME__, __DATE__);
			ImGui::Spacing();
			if (ImGui::Button("YouTube", ImVec2(100, 20)))
				ShellExecute(0, "open", "https://www.youtube.com/@frouk3378", NULL, NULL, 0);
			ImGui::SameLine();
			if (ImGui::Button("GitHub", ImVec2(100, 20)))
				ShellExecute(0, "open", "https://github.com/Frouk3", NULL, NULL, 0);
			gui::TextCentered("Mod Loader made by Frouk");
			gui::TextCentered("If you like my work, consider donating :)");
			ImGui::Text("Donations(to author):");
			if (ImGui::Button("Donatello"))
				ShellExecute(0, "open", "https://donatello.to/Frouk3", NULL, NULL, 0);
			ImGui::SameLine();
			if (ImGui::Button("PayPal"))
				ShellExecute(0, "open", "https://paypal.me/MykhailoKytsun", NULL, NULL, 0);
			ImGui::Text("Credits:");
			ImGui::BulletText("ImGui (%s) : ocornut", ImGui::GetVersion());
			ImGui::BulletText("MinHook : TsudaKageyu");
			gui::TextCentered("And also, thanks to other people that helped me");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}