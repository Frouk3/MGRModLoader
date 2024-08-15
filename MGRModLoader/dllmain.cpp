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

constexpr float MOD_LOADER_VERSION = 1.6f;

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

typedef int(__thiscall* bindCpkMaybe_t)(int, const char* path, int, int, int);
static bindCpkMaybe_t oBindCpk = NULL;

typedef int(__thiscall* loadSound_t)(int ecx, int pData, int pEnvData, int pLoaderData);
static loadSound_t oLoadSound = NULL;

typedef int(__cdecl *criload_t)(int);
static criload_t oCriFsFileLoad = NULL;

int __cdecl hkCriFsFileLoad(int loader) // load here .usm
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		if (*(int*)(loader + 0xB4) != 1u)
			return 2;

		const char* ext = strrchr(*(const char**)(loader + 0xEC), '.');

		if (!strcmp(ext, ".usm"))
		{
			char buffer[MAX_PATH];

			for (auto& prof : ModLoader::Profiles)
			{
				if (!prof->m_bEnabled)
					continue;

				sprintf(buffer, "%s\\%s", prof->getMyPath().c_str(), *(const char**)(loader + 0xEC));
				if (getFileSize(buffer) != -1)
				{
					LOGINFO("[CRIWARE ] Replacing %s -> %s...", *(char**)(loader + 0xEC), buffer);
					strcpy(*(char**)(loader + 0xEC), buffer);
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

size_t __cdecl hkGetFilesize(const char* file)
{
	auto size = oGetFilesize(file);
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		char buff[MAX_PATH];
		if (size == 0)
		{
			for (auto& prof : ModLoader::Profiles)
			{
				if (!prof->m_bEnabled)
					continue;

				sprintf(buff, "%s\\%s", prof->getMyPath().c_str(), file);
				if (auto fSize = getFileSize(buff); fSize != -1)
					return fSize;
			}
		}
	}
	return size;
}


// TODO: Load file data from threads
int __fastcall hkLoad(FileRead::Work* ecx)
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad && !(ecx->m_nFileFlags & 0x10000)) 
	{
		char buff[MAX_PATH];
		for (auto& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			char modName[128];
			sprintf(buff, "%s\\%s", prof->getMyPath().c_str(), ecx->m_FileName);
			sprintf(modName, "mods\\%s\\%s", prof->m_name, ecx->m_FileName);

			if (auto fSize = getFileSize(buff); fSize != -1 && ecx->m_Placeholder && !(ecx->m_nFileFlags & 0x8000))
			{
				size_t fileSize = hkGetFilesize(ecx->m_FileName);
				// result = oLoad(a1, a2, path, a4, a5, size, a7, placeholder, a8, a9);
				if (fileSize != fSize)
					LOGINFO("[FILEREAD] Replacing %s(%.1fMB) -> %s(%.1fMB)", ecx->m_FileName, (float)(((float)ecx->m_nFilesize / 1024.f) / 1024.f), modName, (float)(((float)fSize / 1024.f) / 1024.f));

				if (fileSize == fSize)
					LOGINFO("[FILEREAD] Loading %s(%.1fMB)...", modName, ((float)fSize / 1024.f) / 1024.f);

				auto file = fopen(buff, "rb");
				fseek(file, 0, SEEK_SET);

				FreeMemory(ecx->m_Placeholder, 0); // free memory for allocator
				ecx->m_Placeholder = nullptr;

				ecx->m_Placeholder = ecx->m_pAllocator->AllocateMemory(fSize, 4096u, ((ecx->m_nFileFlags & 0x20) != 0) + 1, 0); // and allocate it again but with different size

				if (!ecx->m_Placeholder)
				{
					LOGERROR("[FILEREAD] Failed to allocate memory for %s(%.1fMB)!", modName, ((float)fSize / 1024.f) / 1024.f);
					ecx->cleanup();
					ecx->m_nWorkerState = 1;
					ecx->m_nFileFlags |= 0x10000; // hoping that it will load original file
					ecx->m_nWaitAmount = 0;
					fclose(file);
					return 1;
				}

				if (fileSize == fSize)
					ecx->m_nFileFlags |= 0x4000;

				ecx->m_nFilesize = fSize;

				fread(ecx->m_Placeholder, 1u, fSize, file);
				fclose(file);

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

int __fastcall hkDvdload(int ecx)
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		char buff[MAX_PATH];
		for (auto& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			sprintf(buff, "%s\\%s", prof->getMyPath().c_str(), (char*)(ecx + 0xC));
			char modName[128];
			sprintf(modName, "mods\\%s\\%s", prof->m_name, (char*)(ecx + 0xC));

			if (auto size = getFileSize(buff); size != -1 && *(int*)(ecx + 0x54))
			{
				LOGINFO("[DVDREAD ] Replacing %s(%.1fMB) -> %s(%.1fMB)", (char*)(ecx + 0xC), ((float)(*(int*)(ecx + 0x58)) / 1024.f) / 1024.f, modName, ((float)size / 1024.f) / 1024.f);
				auto file = fopen(buff, "rb");
				fseek(file, 0, SEEK_SET);

				int block = *(int*)(ecx + 0x54) - 4;
				Hw::cHeap* allocator = *(Hw::cHeap**)(block);

				FreeMemory(*(void**)(ecx + 0x54), 0); // as in hkLoad
				*(void**)(ecx + 0x54) = allocator->AllocateMemory(size, 4096u, 1, 0);

				if (!*(void**)(ecx + 0x54))
				{
					LOGERROR("[DVDREAD ] Failed to allocate memory for %s(%.1fMB)!", modName, ((float)size / 1024.f) / 1024.f);
					fclose(file);
					return 0;
				}

				*(int*)(ecx + 0x58) = size;

				memset(*(void**)(ecx + 0x54), 0, size);
				fread(*(void**)(ecx + 0x54), 1u, size, file);
				fclose(file);

				*(int*)ecx = 5;
				return 0;
			}
		}
	}
	// LOG("[DVDREAD] Loading original %s...", (char*)(ecx + 0xC));
	return oDvdload(ecx);
}

int __cdecl criFsBinder_bindCpk(int a1, int a2, const char* Str, char* a4, size_t Size, int * a6)
{
	return ((int(__cdecl*)(int, int, const char*, char*, size_t, int*))(shared::base + 0xE98448))(a1, a2, Str, a4, Size, a6);
}

int __fastcall hkBindCpk(int ecx, void*, const char* path, int a3, int a4, int a5)
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		for (auto& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			char buff[MAX_PATH];
			sprintf(buff, "%s\\%s", prof->getMyPath().c_str(), path);
			if (getFileSize(buff) != -1)
			{
				if (((int(__cdecl*)(int*))(shared::base + 0xE971BB))((int*)(ecx + 4)) || !*(int*)(ecx + 4))
				{
					((int(__cdecl*)(int))(shared::base + 0xE97C59))(*(int*)(ecx + 4));
					*(int*)(ecx + 4) = NULL;
				}
				char modname[128];
				sprintf(modname, "mods\\%s\\%s", prof->m_name, path);

				if (!criFsBinder_bindCpk(*(int*)(ecx + 4), 0, buff, 0, 0, (int*)(ecx + 8)))
				{
					LOGINFO("[BINDCPK ] Mounted %s successfully!", modname);
					*(int*)(ecx + 0xC) = 1;
					*(int*)(ecx) = 2;
					*(int*)(ecx + 0x10) = a5;
					return 1;
				}
				else
				{
					LOGERROR("[BINDCPK ] bindCpk failed somewhere?? %s", buff);
					((int(__cdecl*)(int))(shared::base + 0xE97C59))(*(int*)(ecx + 4));
					*(int*)(ecx + 4) = NULL;
					*(int*)(ecx) = NULL;
					*(int*)(ecx + 0x10) = NULL;
					return 0;
				}
			}
		}
	}
	return oBindCpk(ecx, path, a3, a4, a5);
}

int __fastcall hkLoadSound(int ecx, void*, int pData, int pEnvData, int pLoaderData)
{
	if (ModLoader::bInit && !ModLoader::bIgnoreDATLoad)
	{
		char buffer[MAX_PATH];
		for (auto& prof : ModLoader::Profiles)
		{
			if (!prof->m_bEnabled)
				continue;

			sprintf(buffer, "%s\\%s", prof->getMyPath().c_str(), *(char**)(pData + 0x10)); // replacing a path to a sound with a path to a modified .wem/.bnk, yet still loads

			if (getFileSize(buffer) != -1)
			{
				strcpy(*(char**)(pData + 0x10), buffer);
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
		newVersion = atof(str);

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
		GetHeapManager()->create("GLOBAL");
		
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
				if (bIsForegroundWindow && shared::IsKeyPressed(VK_LMENU) && shared::IsKeyPressed(VK_F2, false))
					gui::bShow ^= true;
			};

		Events::OnMainCleanupEvent += []()
			{
				ModLoader::Save();
				closeLog();
			};

		// making each heap manager to have global heap manager as backup, making game load as much as it wants to
		Events::OnGameStartupEvent += []()
			{
				static bool once = false;
				if (!once)
				{
					auto heap = Hw::cHeapGlobal::get()->m_pPrev;
					do
					{
						if (!heap->m_pReservedHeap && heap != Hw::cHeapGlobal::get())
							heap->m_pReservedHeap = Hw::cHeapGlobal::get();

						// LOGINFO("Set backup for %s", heap->m_TargetAlloc);

						heap = heap->m_pNext;
					} while (heap);
					once = true;
				}
			};

		LPVOID trg = (LPVOID)(shared::base + 0xA9E170);
		MH_CreateHook(trg, hkLoad, (LPVOID*)&oLoad);
		MH_EnableHook(trg);
		
		trg = (LPVOID)(shared::base + 0x9EAF60);
		MH_CreateHook(trg, hkGetFilesize, (LPVOID*)&oGetFilesize);
		MH_EnableHook(trg);

		trg = (LPVOID)(shared::base + 0x9E9900);
		MH_CreateHook(trg, hkDvdload, (LPVOID*)&oDvdload);
		MH_EnableHook(trg);

		trg = (LPVOID)(shared::base + 0x9EAFB0);
		MH_CreateHook(trg, hkBindCpk, (LPVOID*)&oBindCpk);
		MH_EnableHook(trg);
		
		trg = (LPVOID)(shared::base + 0x9F1320);
		MH_CreateHook(trg, hkLoadSound, (LPVOID*)&oLoadSound);
		MH_EnableHook(trg);

		trg = (LPVOID)(shared::base + 0xE9C0F6);
		MH_CreateHook(trg, hkCriFsFileLoad, (LPVOID*)&oCriFsFileLoad);
		MH_EnableHook(trg);

		trg = (LPVOID)(shared::base + 0x9EB160);
		MH_CreateHook(trg, hkDvdReadInit, (LPVOID*)&oDvdReadInit);
		MH_EnableHook(trg);

		*(CriErrCbFunc_t**)(shared::base + 0x1CAE15C) = (CriErrCbFunc_t*)criErr_Callback;

		InitGUI();

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

		ImGui::Text("New update is available(New: %s, Installed: %s)", version, newVer);
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
	ImGui::Begin("Mod Loader", NULL);
	if (ImGui::BeginTabBar("##LOADERTAB"))
	{
		if (ImGui::BeginTabItem("Mods"))
		{
			for (auto& prof : ModLoader::Profiles)
			{
				if (ImGui::TreeNode(prof->m_name))
				{
					ImGui::Checkbox("Enabled", &prof->m_bEnabled);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Some mods require restarting game.");
					if (ImGui::InputInt("Priority", &prof->m_nPriority))
						ModLoader::SortProfiles();
					ImGui::TreePop();
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Mod Info"))
		{
			auto modifiedFile = 0;
			auto loadedFiles = 0;
			for (auto& file : FileRead::Manager::Instance.m_FileReaderVector)
			{
				modifiedFile += (file->m_nFileFlags & 0x8000) != 0;
				loadedFiles += (file->m_nFileFlags & 0x4000) != 0;
			}
			if (ImGui::CollapsingHeader("Modified Files"))
			{
				ImGui::Text("Modified files: %d\nLoaded Files: %d", modifiedFile, loadedFiles);
				for (auto& file : FileRead::Manager::Instance.m_FileReaderVector)
				{
					if (file->m_nFileFlags & 0x8000 || file->m_nFileFlags & 0x4000)
						ImGui::BulletText("%s", file->m_FileName);
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Settings"))
		{
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