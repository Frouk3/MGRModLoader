#include <Events.h>
#include "gui.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "ModLoader.h"
#include <GameMenuStatus.h>
#include "FilesTools.hpp"
#include <Hooks.h>
#include "ThreadWork.hpp"
#include "licenses.h"

bool bLockInput = false;

Hw::cKeyboardState g_KeyboardState;

struct Link
{
	std::string m_Name;
	std::string m_URL;
};

std::vector<Link> g_Socials;
std::vector<Link> g_Donations;

void InitLinks()
{
	const char* szURL = "https://raw.githubusercontent.com/Frouk3/mod_about_links/main/links";

	std::vector<char> data = Utils::FetchURL(szURL);
	if (data.empty())
	{
		LOGERROR("Failed to fetch about links. No internet connection?");
		return;
	}
	IniReader ini;
	ini.ParseData(data.data());

	auto ParseSections = [&](const char* sectionName, std::vector<Link>& container)
		{
			IniReader::IniSection* section = ini.get(sectionName);

			if (!section)
				return;

			for (int i = 0; ; i++)
			{
				Utils::String root = Utils::format("Link[%d]", i);
				IniReader::IniSection::IniKey* key = section->get((root + ".Name").c_str());
				IniReader::IniSection::IniKey* urlKey = section->get((root + ".URL").c_str());

				if (!key || !urlKey)
					break;

				Link link;
				link.m_Name = key->getValue();
				link.m_URL = key->getValue();
				container.push_back(link);
			}
		};

	ParseSections("Social", g_Socials);
	ParseSections("Donations", g_Donations);
}

bool bKeyboardInputAvailable = true;

char g_KeyHoldState[Hw::KeyboardManager::MAX_KEY_MAP_FLAG] = { 0 };

typedef HRESULT(WINAPI* GetDeviceState_t)(LPDIRECTINPUTDEVICE8W, DWORD, LPVOID);
static GetDeviceState_t oGetDeviceState = nullptr;

HRESULT WINAPI hkGetDeviceState(LPDIRECTINPUTDEVICE8W pThis, DWORD lpdwData, LPVOID pBuffer)
{
	if (!ModLoader::bInit)
		return oGetDeviceState(pThis, lpdwData, pBuffer);

	if (bLockInput)
	{
		if (pBuffer == &g_KeyHoldState)
			return oGetDeviceState(pThis, lpdwData, pBuffer);

		if (pThis == Hw::KeyboardManager::m_pKeyboardDevice)
			return 0x8007001E;

		if (pThis == Hw::MouseManager::m_pMouseDevice)
			return 0x8007001E;
	}

	return oGetDeviceState(pThis, lpdwData, pBuffer);
}

bool Explorer_Enabled = false;
FileSystem::Directory* Explorer_MainDirectory = nullptr;
const char* Explorer_CustomFilter = "";
Utils::String Explorer_Filter = ""; // current extension filter (e.g. "*.dat")
std::vector<FileSystem::Directory*> Explorer_selectedDirs;
std::vector<FileSystem::File*> Explorer_selectedFiles;

enum Explorer_Flags : unsigned int
{
	EXPLR_CHOOSE_ALLOW_DIRECTORY = 1,
	EXPLR_CHOOSE_ALLOW_FILE = 2,
	EXPLR_CHOOSE_STRICT = 4,

	EXPRL_CHOOSE_ONLY_FILES = EXPLR_CHOOSE_ALLOW_FILE | EXPLR_CHOOSE_STRICT,
	EXPRL_CHOOSE_ONLY_DIRECTORY = EXPLR_CHOOSE_ALLOW_DIRECTORY | EXPLR_CHOOSE_STRICT
};

unsigned int Explorer_flags = EXPLR_CHOOSE_ALLOW_FILE | EXPLR_CHOOSE_ALLOW_DIRECTORY;

std::function<void(const std::vector<FileSystem::Directory*>&)> Explorer_OnSelectDirectoryCallback;
std::function<void(const std::vector<FileSystem::File*>&)> Explorer_OnSelectFileCallback;

static inline void Explorer_SetCallback(
	const std::function<void(const std::vector<FileSystem::Directory*>&)>& dirCallback,
	const std::function<void(const std::vector<FileSystem::File*>&)>& fileCallback)
{
	Explorer_OnSelectDirectoryCallback = dirCallback;
	Explorer_OnSelectFileCallback = fileCallback;
}

void Explorer_Setup(
	const char* filter,
	FileSystem::Directory* mainDir,
	const std::function<void(const std::vector<FileSystem::Directory*>&)>& dirCb /*= nullptr*/,
	const std::function<void(const std::vector<FileSystem::File*>&)>& fileCb /*= nullptr*/,
	unsigned int flags /*= EXPLR_CHOOSE_ALLOW_DIRECTORY | EXPLR_CHOOSE_ALLOW_FILE*/)
{
	Explorer_selectedDirs.clear();
	Explorer_selectedFiles.clear();

	Explorer_Enabled = true;
	Explorer_CustomFilter = filter ? filter : "";
	Explorer_MainDirectory = mainDir;
	Explorer_flags = flags;

	Explorer_Filter = Explorer_CustomFilter && Explorer_CustomFilter[0] ? Explorer_CustomFilter : "*";

	Explorer_SetCallback(dirCb, fileCb);
}

static inline void Explorer_UniquePush(std::vector<FileSystem::Directory*>& vec, FileSystem::Directory* dir)
{
	if (!dir) return;
	for (auto* d : vec) if (d == dir) return;
	vec.push_back(dir);
}
static inline void Explorer_UniquePush(std::vector<FileSystem::File*>& vec, FileSystem::File* f)
{
	if (!f) return;
	for (auto* x : vec) if (x == f) return;
	vec.push_back(f);
}

static inline bool Explorer_FileMatchesFilter(const char* name, const Utils::String& filter)
{
	if (!name || !*name) return false;
	if (filter.empty() || filter == "*" || filter == "*.*") return true;

	// Accept "*.ext" or ".ext" or "ext"
	const char* dot = strrchr(name, '.');
	if (!dot) return false;

	Utils::String wanted = filter;
	// Normalize to ".ext"
	if (wanted[0] == '*')
		wanted.erase(0, 1); // remove leading '*'
	if (wanted[0] == '.')
	{
		// ok
	}
	else
	{
		// "ext" -> ".ext"
		wanted = Utils::format(".%s", wanted.c_str());
	}

	return stricmp(dot, wanted.c_str()) == 0;
}

void DrawMiniExplorer()
{
	if (!Explorer_Enabled || !Explorer_MainDirectory)
		return;

	// Parse multi-string filter list into allowedExtensions
	std::vector<Utils::String> allowedExtensions;
	{
		const char* item = Explorer_CustomFilter;
		while (item && *item)
		{
			allowedExtensions.emplace_back(item);
			item += strlen(item) + 1;
		}
		if (allowedExtensions.empty())
			allowedExtensions.emplace_back("*");
	}

	static FileSystem::Directory* queueDirectory = nullptr;
	static void* lastItemChoosen = nullptr;

	auto WasDirSelected = [&](FileSystem::Directory* dir) -> bool
		{
			if (lastItemChoosen == dir) return true;
			for (auto& sbd : Explorer_selectedDirs)
				if (sbd == dir) return true;
			return false;
		};
	auto WasFileSelected = [&](FileSystem::File* file) -> bool
		{
			if (lastItemChoosen == file) return true;
			for (auto& f : Explorer_selectedFiles)
				if (f == file) return true;
			return false;
		};

	ImGui::Begin("##MiniExplorer", &Explorer_Enabled, ImGuiWindowFlags_NoCollapse);

	// Bottom action bar
	ImVec2 pos = ImGui::GetCursorPos();
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x, 70.f));
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x,
		ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 80.f));
	ImGui::BeginChild("FileBar", ImVec2(ImGui::GetWindowContentRegionMax().x, 40.f), ImGuiChildFlags_Borders);

	// OK / Cancel
	ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionAvail().x - 110.f, ImGui::GetWindowContentRegionMax().y - 30.f));
	if (ImGui::Button("OK", ImVec2(50, 25)))
	{
		// Enforce strict flags: only one type selectable
		if ((Explorer_flags & EXPLR_CHOOSE_STRICT) != 0)
		{
			if ((Explorer_flags & EXPLR_CHOOSE_ALLOW_FILE) != 0 && (Explorer_flags & EXPLR_CHOOSE_ALLOW_DIRECTORY) == 0)
				Explorer_selectedDirs.clear();
			else if ((Explorer_flags & EXPLR_CHOOSE_ALLOW_DIRECTORY) != 0 && (Explorer_flags & EXPLR_CHOOSE_ALLOW_FILE) == 0)
				Explorer_selectedFiles.clear();
		}

		if (Explorer_OnSelectDirectoryCallback)
			Explorer_OnSelectDirectoryCallback(Explorer_selectedDirs);
		if (Explorer_OnSelectFileCallback)
			Explorer_OnSelectFileCallback(Explorer_selectedFiles);

		Explorer_selectedDirs.clear();
		Explorer_selectedFiles.clear();

		Explorer_Enabled = false;
		Explorer_CustomFilter = "";
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(50, 25)))
	{
		Explorer_selectedDirs.clear();
		Explorer_selectedFiles.clear();

		Explorer_Enabled = false;
		Explorer_CustomFilter = "";
	}

	// Filter combo
	ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - 120.f, ImGui::GetCursorStartPos().y));
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
	Utils::String ExplorerFilterPreview = "*";
	if (Explorer_Filter.c_str()[0] != '*')
		ExplorerFilterPreview += Explorer_Filter;
	else
		ExplorerFilterPreview = Explorer_Filter;

	if (ImGui::BeginCombo("##EXPLR_EXT_SELECT", ExplorerFilterPreview.c_str()))
	{
		if (ImGui::Selectable("Any file(*.*)", stricmp(Explorer_Filter.c_str(), "*") == 0 || stricmp(Explorer_Filter.c_str(), "*.*") == 0))
			Explorer_Filter = "*";

		for (size_t i = 0; i < allowedExtensions.size(); i++)
		{
			const bool bIsSelected = (allowedExtensions[i] == Explorer_Filter);
			ImGui::PushID((int)i);

			Utils::String filterPreview = "*";
			if (allowedExtensions[i][0u] != '*')
				filterPreview += allowedExtensions[i];
			else
				filterPreview = allowedExtensions[i];

			if (ImGui::Selectable(filterPreview.c_str(), bIsSelected))
				Explorer_Filter = allowedExtensions[i];

			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::EndChild();

	// Breadcrumbs
	ImGui::SetCursorPos(pos);
	ImGui::PushID("##History");
	{
		FileSystem::Directory* direct = Explorer_MainDirectory;
		lib::StaticArray<FileSystem::Directory*, 32> directories;

		do
		{
			directories.pushFront(direct);
		} while ((direct = direct->m_parent) != nullptr);

		for (auto& dir : directories)
		{
			if (ImGui::SmallButton(dir->getName()))
				Explorer_MainDirectory = dir;

			if (dir != directories.back())
				ImGui::SameLine();
		}
	}
	ImGui::PopID();

	ImGui::Separator();

	// Up one level
	if (Explorer_MainDirectory->m_parent &&
		ImGui::Selectable("..", false, ImGuiSelectableFlags_AllowDoubleClick))
	{
		lastItemChoosen = Explorer_MainDirectory;
		Explorer_MainDirectory = Explorer_MainDirectory->m_parent;
		queueDirectory = nullptr;
	}

	// Directories list
	for (auto& subdir : Explorer_MainDirectory->m_subdirs)
	{
		const bool canChooseDir = (Explorer_flags & EXPLR_CHOOSE_ALLOW_DIRECTORY) != 0;
		bool selectable = ImGui::Selectable(subdir->getName(),
			WasDirSelected(subdir),
			ImGuiSelectableFlags_AllowDoubleClick);

		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selectable)
		{
			// Navigate into directory
			queueDirectory = subdir;
			lastItemChoosen = subdir;
		}
		else if (ImGui::IsItemHovered() && canChooseDir)
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
			{
				const bool multi = g_KeyboardState.on(Hw::KB_CTRL_L);
				if (!multi)
				{
					Explorer_selectedDirs.clear();
					Explorer_selectedFiles.clear();
				}
				Explorer_UniquePush(Explorer_selectedDirs, subdir);
				lastItemChoosen = subdir;
			}
		}
	}

	// Files list
	for (auto& files : Explorer_MainDirectory->m_files)
	{
		if (!Explorer_FileMatchesFilter(files.getName(), Explorer_Filter))
			continue;

		Utils::String label = Utils::format("%s [%s]",
			files.getName(),
			Utils::getProperSize(files.m_filesize).c_str());

		bool selectable = ImGui::Selectable(label.c_str(),
			WasFileSelected(&files),
			ImGuiSelectableFlags_AllowDoubleClick);

		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selectable)
		{
			// Optional: on double click, confirm selection if strict files-only
			if ((Explorer_flags & EXPLR_CHOOSE_STRICT) &&
				(Explorer_flags & EXPLR_CHOOSE_ALLOW_FILE) &&
				!(Explorer_flags & EXPLR_CHOOSE_ALLOW_DIRECTORY))
			{
				Explorer_selectedDirs.clear();
				Explorer_selectedFiles.clear();
				Explorer_UniquePush(Explorer_selectedFiles, &files);

				if (Explorer_OnSelectFileCallback)
					Explorer_OnSelectFileCallback(Explorer_selectedFiles);

				Explorer_selectedFiles.clear();
				Explorer_Enabled = false;
				Explorer_CustomFilter = "";
			}
		}
		else if (ImGui::IsItemHovered() && (Explorer_flags & EXPLR_CHOOSE_ALLOW_FILE))
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
			{
				const bool multi = g_KeyboardState.on(Hw::KB_CTRL_L);
				if (!multi)
				{
					Explorer_selectedDirs.clear();
					Explorer_selectedFiles.clear();
				}
				Explorer_UniquePush(Explorer_selectedFiles, &files);
				lastItemChoosen = &files;
			}
		}
	}

	if (queueDirectory)
	{
		Explorer_MainDirectory = queueDirectory;
		queueDirectory = nullptr;
	}

	ImGui::End();
}

bool bModLoaderForceDisableDueToInsultToAuthorByUsingAnotherModLoaderToLoadThisModLoader = false;

class ModLoaderPlugin
{
public:
	static inline void InitGUI()
	{
		Events::OnPresent.before += []()
			{
				gui::OnEndScene();
				if (Hw::GraphicDevice::m_pDevice->BeginScene() == S_OK)
				{
					gui::Render();
					Hw::GraphicDevice::m_pDevice->EndScene(); // It shouldn't alter mods that are based from mgr-plugin-sdk, but I do know that there are some "reshade" mods that might hook into EndScene and get called more than once, creating [1, infinite] render times
				}
			};
		Events::OnDeviceReset.before += gui::OnReset::Before;
		Events::OnDeviceReset.after += gui::OnReset::After;
	}

	ModLoaderPlugin()
	{
		InitGUI();

		Logger::Init();

		LOG("Running on Mod Loader v%s", Utils::FloatStringNoTralingZeros(Updater::fCurrentVersion));

		ModloaderHeap.create(-1, "ModloaderHeap");
		FileSystem::Init(64);

		{
			HANDLE hOpen = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId());
			if (!hOpen)
				return;

			HMODULE hMods[1024];
			DWORD cbNeeded;

			if (EnumProcessModules(hOpen, hMods, sizeof(hMods), &cbNeeded)) // by doing it on EVERY module, another mod loader can't really hide
			{
				for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
				{
					HMODULE hMod = hMods[i];
					if (!hMod)
						continue;

					FARPROC addr = GetProcAddress(hMod, "GetModLoaderAPI");
					if (addr)
					{
						LOG("What a pain.");
						bModLoaderForceDisableDueToInsultToAuthorByUsingAnotherModLoaderToLoadThisModLoader = true; // lmao

						LOGINFO("Dear user!");
						LOGINFO("It seems like you have *tried* to load this Mod Loader with another Mod Loader. Is this really a thing?");
						LOGINFO("I suggest you to refrain from modding, if you think that it is good idea to make this slop, it's not.");

						// I won't tolerate or have further discussions about this, I cannot help either.

						LOGINFO("The developer won't be able to help you with this *issue*.");

						break;
					}
				}
			}
		}

		ModLoader::Startup();
		Updater::Init();

		if (!FileSystem::PathExists((Utils::String(ModLoader::ModLoaderPath) / "repacked").c_str()))
			CreateDirectoryA((Utils::String(ModLoader::ModLoaderPath) / "repacked").c_str(), nullptr);

		for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
		{
			prof->FileWalk([&](FileSystem::File& file)
				{
					if (const char* chr = strrchr(file.getName(), '.'); !stricmp(chr, ".cpk"))
						++CriWare::iAvailableCPKs;
				});
		}

		*(int*)(shared::base + 0x14CDE20) += CriWare::iAvailableCPKs;

		gui::GUIHotkey.Load();

		Events::OnGameStartupEvent.after += [](cGame*)
			{
				ThreadWork::AddThread(new cThread([](cThread* pThread, LPVOID pParam)
					{
						Sleep(1000);
						void** vftable = *(void***)Hw::KeyboardManager::m_pKeyboardDevice;
						oGetDeviceState = (GetDeviceState_t)vftable[9];
						injector::WriteMemory<void*>((void**)&vftable[9], (void*)hkGetDeviceState, true);

						LOG("Input hook installed.");

						InitLinks();
					}, nullptr));

				Hw::KeyboardManager::InitState(g_KeyboardState);

				/*
				FileSystem::Directory pTestDir((ModLoader::GetModFolder() / "test" / "pl1010" / "").c_str());
				pTestDir.scanFiles(true, false, FileSystem::Directory::SCANFLAG_ALL);
				std::vector<DataArchiveTools::FileStructure> outFiles;
				for (auto &file : pTestDir.m_files)
				{
					DataArchiveTools::FileStructure fs;
					fs.m_filename = file.getName();
					fs.m_fileSize = file.m_filesize;
					bool bRead = FileSystem::ReadSyncAlloc(file.m_path.c_str(), &fs.m_file, &fs.m_fileSize);
					if (bRead)
						outFiles.push_back(fs);
					else
						LOGERROR("Failed to read file %s for testing.", file.m_path.c_str());
				}
				if (!outFiles.empty())
				{
					if (DataArchiveTools::rebuildFmerge(outFiles, (ModLoader::GetModFolder() / "test" / "pl1010_rebuilt.dat").c_str()))
						LOGERROR("Error occured.");
					else
						LOG("Rebuilt fmerge file successfully.");
				}
				*/
			};

		Events::OnUpdateEvent += []()
			{
				if (bKeyboardInputAvailable && *(int*)(shared::base + 0x19D509C))
				{
					bool bUpdateStrokes = true;
					if (Hw::KeyboardManager::m_pKeyboardDevice)
					{
						HRESULT hr = Hw::KeyboardManager::m_pKeyboardDevice->Acquire();
						if (hr == DIERR_INPUTLOST)
						{
							hr = Hw::KeyboardManager::m_pKeyboardDevice->Acquire();
						}
						if (hr == DIERR_NOTACQUIRED)
						{
							bUpdateStrokes = false;
						}

						if (SUCCEEDED(hr))
						{
							hr = Hw::KeyboardManager::m_pKeyboardDevice->GetDeviceState(Hw::KeyboardManager::MAX_KEY_MAP_FLAG, (LPVOID)&g_KeyHoldState);
							if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
								bUpdateStrokes = false;
						}
						else bUpdateStrokes = false;
					}
					else bUpdateStrokes = false;
					if (bUpdateStrokes)
					{
						Hw::KeyboardManager::UpdateStateOnToOld(g_KeyboardState);
						{
							for (int i = 0; i < Hw::KEYBOARD_MAP::KB_MAP_MAX; ) // Oh right.
							{
								int hwKey = Hw::KeyboardManager::m_pStrokeHidFlag[i * 2 + 0];
								int diKey = Hw::KeyboardManager::m_pStrokeHidFlag[i * 2 + 1];

								if (g_KeyHoldState[diKey] & 0x80)
									g_KeyboardState.m_pOn[hwKey >> 5] |= (0x80000000 >> (hwKey & 0x1F));
								else
									g_KeyboardState.m_pOn[hwKey >> 5] &= ~(0x80000000 >> (hwKey & 0x1F));

								i++;
							}

							Hw::KeyboardManager::UpdateKeyState(g_KeyboardState);
						}
					}
				}
				else Hw::KeyboardManager::InitState(g_KeyboardState);

				ThreadWork::UpdateThreads();
				if (g_GameMenuStatus == InMenu) gui::GUIHotkey.Update();
				else
				{
					gui::GUIHotkey.m_bToggle = false; // force close GUI when not in menu, and make sure that input is available
				}
				FileSystem::UpdateReaders();
			};

		Events::OnMainCleanupEvent.before += []()
			{
				Explorer_MainDirectory = nullptr;
				Explorer_selectedDirs.clear();
				Explorer_selectedFiles.clear();

				ThreadWork::ClearThreads();

				for (int i = 0; i < CriWare::aBinders.getSize(); ++i)
				{
					auto& binder = CriWare::aBinders[i];
					if (!binder)
						continue;

					if (binder->m_BinderHandle)
					{
						CriFsBinderHn_free(binder->m_BinderHandle);
						binder->m_BinderHandle = nullptr;
					}
					operator delete(binder, (Hw::cHeap*)nullptr);
				}

				CriWare::aBinders.clear();

				Updater::SaveConfig();
				Logger::SaveConfig();
				ModLoader::Shutdown();
				gui::GUIHotkey.Save();
				FileSystem::Shutdown();
				gui::Shutdown();
			};

		Events::OnMainCleanupEvent.after += []()
			{
				if (ModLoader::ModLoaderPath[0])
				{
					Utils::String repackedDir = Utils::String(ModLoader::ModLoaderPath) / "repacked" / "";
					if (FileSystem::PathExists(repackedDir.c_str()))
						FileSystem::RemoveDirectoryRecursively(repackedDir.c_str()); // scary but works
				}
				// Make sure to free resources before heap destruction, if you'll have a leak here, that's your fault
				for (void* pBlock = ModloaderHeap.getNextAlloc(nullptr); pBlock; pBlock = ModloaderHeap.getNextAlloc(pBlock))
					LOGWARNING("This allocation at %p is a memory leak", pBlock);

				ModloaderHeap.destroy(); // Destroying heap would also free all allocations, but we want to log leaks

				LOGINFO("Cleanup complete.");
				Logger::Shutdown();
			};
	}
} gModLoaderPlugin;

int iMouseShowReqCount = 0;
bool bUserNotice = false;
bool bManualUpdateCheck = false;

void SetUpdatePopup()
{
	ImGui::PushID("UpdatePopup");
	switch (Updater::eUpdateStatus)
	{
	case Updater::UPDATE_STATUS_NONE:
		break;
	case Updater::UPDATE_STATUS_LATEST_INSTALLED:
	{
		if (!bUserNotice && bManualUpdateCheck)
		{
			ImGui::OpenPopup("##Latest");

			++iMouseShowReqCount;
			bUserNotice = true;
		}
		break;
	}
	case Updater::UPDATE_STATUS_AVAILABLE:
	{
		if (!bUserNotice)
		{
			ImGui::OpenPopup("##UpdateAvailable");
			++iMouseShowReqCount;
			bUserNotice = true;
		}
		break;
	}
	case Updater::UPDATE_STATUS_FAILED:
	{
		if (!bUserNotice)
		{
			ImGui::OpenPopup("##UpdateFailed");
			++iMouseShowReqCount;
			bUserNotice = true;
		}
		break;
	}
	case Updater::UPDATE_STATUS_UNEXPECTED:
	{
		if (!bUserNotice)
		{
			ImGui::OpenPopup("##UpdateUnexpected");
			++iMouseShowReqCount;
			bUserNotice = true;
		}
		break;
	}
	case Updater::UPDATE_STATUS_NO_INTERNET:
	{
		if (!bUserNotice)
		{
			ImGui::OpenPopup("##NoInternet");
			++iMouseShowReqCount;
			bUserNotice = true;
		}
		break;
	}
	default:
		break;
	}

	unsigned int flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
	ImVec2 buttonSize = ImVec2(100, 0);

	if (ImGui::BeginPopupModal("##Latest", nullptr, flags))
	{
		ImGui::Text("You have the latest version of Mod Loader installed.");
		if (ImGui::Button("OK", buttonSize))
		{
			ImGui::CloseCurrentPopup();
			--iMouseShowReqCount;
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("##UpdateAvailable", nullptr, flags))
	{
		ImGui::Text("New version of Mod Loader is available!\n\nCurrent version: %s\nLatest version: %s", Utils::FloatStringNoTralingZeros(Updater::fCurrentVersion).c_str(), Utils::FloatStringNoTralingZeros(Updater::fLatestVersion).c_str());

		if (ImGui::Button("OK", buttonSize))
		{
			ImGui::CloseCurrentPopup();
			--iMouseShowReqCount;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", buttonSize))
		{
			ImGui::CloseCurrentPopup();
			--iMouseShowReqCount;
		}
		ImGui::SameLine();
		if (ImGui::Button("Visit website", buttonSize))
		{
			ThreadWork::AddThread([](cThread*, LPVOID) -> void
				{
					ShellExecuteA(NULL, "open", "https://www.nexusmods.com/metalgearrisingrevengeance/mods/650", NULL, NULL, SW_SHOWNORMAL);

				}, nullptr);
			--iMouseShowReqCount;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("##UpdateFailed", nullptr, flags))
	{
		ImGui::Text("Failed to check for updates. Please try again later.");
		if (ImGui::Button("OK", buttonSize))
		{
			ImGui::CloseCurrentPopup();
			--iMouseShowReqCount;
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("##UpdateUnexpected", nullptr, flags))
	{
		ImGui::Text("An unexpected error occurred while checking for updates. Please try again later.");
		if (ImGui::Button("OK", buttonSize))
		{
			ImGui::CloseCurrentPopup();
			--iMouseShowReqCount;
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("##NoInternet", nullptr, flags))
	{
		ImGui::Text("No internet connection. Please check your connection and try again.");
		if (ImGui::Button("OK", buttonSize))
		{
			ImGui::CloseCurrentPopup();
			--iMouseShowReqCount;
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();
}

void gui::RenderWindow()
{
	if (g_GameMenuStatus == InMenu && gui::GUIHotkey.GetHotkeyType() == Hotkey::HT_OFF)
		gui::GUIHotkey.m_bToggle = true;

	static bool bToggleGuard = false;
	if (bToggleGuard != gui::GUIHotkey.m_bToggle)
	{
		if (gui::GUIHotkey.m_bToggle)
			++iMouseShowReqCount;
		else
			--iMouseShowReqCount;

		bToggleGuard = gui::GUIHotkey.m_bToggle;
	}

	ImGui::GetIO().MouseDrawCursor = iMouseShowReqCount > 0;
	bLockInput = iMouseShowReqCount > 0;

	SetUpdatePopup();

	if (!gui::GUIHotkey.m_bToggle)
		return;

	if (g_GameMenuStatus != InMenu)
		return;

	unsigned int flags = ImGuiWindowFlags_NoCollapse;
	if (gui::GUIHotkey.GetHotkeyType() == Hotkey::HT_ALWAYS || gui::GUIHotkey.GetHotkeyType() == Hotkey::HT_OFF)
		flags &= ~ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Mod Loader", &gui::GUIHotkey.m_bToggle, flags);
	if (ImGui::Button("Save Config"))
	{
		Updater::SaveConfig();
		Logger::SaveConfig();
		ModLoader::Save();

		gui::GUIHotkey.Save();
	}
	ImGui::SameLine();
	if (ImGui::Button("Load Config"))
	{
		Updater::LoadConfig();
		Logger::LoadConfig();
		ModLoader::Load();

		gui::GUIHotkey.Load();
	}
	ImGui::Separator();
	if (ImGui::BeginTabBar("MLTabBar"))
	{
		if (ImGui::BeginTabItem("Mods"))
		{
			if (!ModLoader::bLoadMods)
			{
				ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Mods are disabled from loading!");
			}
			else if (bModLoaderForceDisableDueToInsultToAuthorByUsingAnotherModLoaderToLoadThisModLoader)
			{
				ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Another mod loader is detected. I suggest you to stop what you're doing.");
				ImGui::TextDisabled("(This is a joke, but seriously, use this mod loader if you want to use mods, other mod loaders are not supported and may cause issues)");
				ImGui::TextDisabled("(Also, if you think that using another mod loader to load this mod loader is a good idea, you probably shouldn't be modding in the first place)");
			}
			else
			{
				static bool bSearchBar = false;
				static Utils::String searchBarContent;
				if (g_KeyboardState.on(Hw::KB_CTRL_L) && g_KeyboardState.trig(Hw::KB_F))
					bSearchBar ^= true;

				if (bSearchBar)
				{
					ImVec4 bgColor(0.2f, 0.2f, 0.2f, 0.8f);
					ImVec4 borderColor(0.7f, 0.7f, 0.7f, 1.0f);

					ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
					ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 5));

					ImGui::PushItemWidth(ImGui::GetWindowContentRegionMax().x - 100.f);
					ImGui::InputText("##search", searchBarContent);
					ImGui::SetItemDefaultFocus();
					ImGui::PopItemWidth();

					ImGui::SameLine();
					if (ImGui::Button("Clear", ImVec2(ImGui::GetContentRegionAvail().x - 5.f, 0)))
					{
						searchBarContent.clear();
						searchBarContent.shrink_to_fit();
					}

					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(2);
				}

				if (ImGui::BeginTable("ModsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Mod Name");
					ImGui::TableSetupColumn("Author");
					ImGui::TableSetupColumn("Version");

					static int moveTo = -1, moveFrom = -1;
					static bool bRequireMoving = false;

					ImGui::TableHeadersRow();
					for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
					{
						{
							Utils::String searchTarg = prof->m_name;
							if (prof->m_ModInfo && !prof->m_ModInfo->m_title.empty())
								searchTarg = prof->m_ModInfo->m_title;

							if (!Utils::contains(searchTarg.c_str(), searchBarContent.c_str(), true)) // do case sensitive search
								continue;
						}
						ImGui::TableNextRow();
						ImGui::PushID(prof);
						ImGui::TableNextColumn();
						if (prof->m_ModInfo)
							ImGui::Checkbox(prof->m_ModInfo->m_title.c_str(), &prof->m_bEnabled);
						else
							ImGui::Checkbox(prof->m_name.c_str(), &prof->m_bEnabled);
						if (ImGui::BeginDragDropSource())
						{
							moveFrom = &prof - ModLoader::Profiles.begin();
							ImGui::SetDragDropPayload("##ML_PROF", &prof, sizeof(ModLoader::ModProfile*));

							ImGui::EndDragDropSource();
						}
						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("##ML_PROF"))
							{
								ModLoader::ModProfile* const* pProf = (ModLoader::ModProfile* const*)payload->Data;
								if (pProf != &prof)
								{
									bRequireMoving = true;
									moveTo = &prof - ModLoader::Profiles.begin();
								}
							}
							ImGui::EndDragDropTarget();
						}
						if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
							ImGui::OpenPopup("##EXTRA_MOD_INFO_POPUP");
						if (ImGui::BeginPopup("##EXTRA_MOD_INFO_POPUP"))
						{
							if (ModLoader::ModExtraInfo* mod = prof->m_ModInfo; mod)
							{
								ImGui::InputText("Title", mod->m_title);
								ImGui::InputText("Author", mod->m_author);
								ImGui::InputText("Author URL", mod->m_authorURL);
								ImGui::InputText("Version", mod->m_version);
								ImGui::InputText("Description", mod->m_description);
								ImGui::InputText("Date", mod->m_date);

								ImGui::InputText("Update Server", mod->m_UpdateServer);
								ImGui::InputText("Save File", mod->m_SaveFile);

								ImGui::PushID("#DLL_LIST");
								ImGui::Text(mod->m_DLLs.empty() ? "There are no DLL's specified." : "DLLs:");
								if (!mod->m_DLLs.empty())
								{
									ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
									for (size_t i = 0; i < mod->m_DLLs.size(); i++)
									{
										ImGui::PushID(i);
										ImGui::BulletText(mod->m_DLLs[i].c_str());
										ImGui::SameLine();
										if (ImGui::SmallButton("-"))
										{
											mod->m_DLLs.erase(mod->m_DLLs.begin() + i);
											ImGui::PopID();
											break;
										}
										ImGui::PopID();
									}
									ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
								}
								if (ImGui::SmallButton("+"))
								{
									Explorer_Setup(".DLL\0", &prof->m_root, nullptr, [&](const std::vector<FileSystem::File*>& files)
										{
											if (files.empty())
												return;

											for (auto& file : files)
											{
												if (!file)
													continue;

												Utils::String relPath;
												relPath = file->m_path.c_str() + prof->m_root.m_path.length();
												if (relPath.c_str()[0] == '\\')
													relPath.erase(0, 1);

												prof->m_ModInfo->m_DLLs.push_back(relPath); // ah yes, the `mod` stack value that just there, being useless
											}
										}, Explorer_Flags::EXPRL_CHOOSE_ONLY_FILES);
								}
								ImGui::PopID();

								ImGui::PushID("#DIR_LIST");
								ImGui::Text(mod->m_Dirs.empty() ? "There are no include directories." : "Directories: ");
								if (!mod->m_Dirs.empty())
								{
									ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());

									for (size_t i = 0; i < mod->m_Dirs.size(); i++)
									{
										Utils::String& dir = mod->m_Dirs[i];
										ImGui::PushID(i);
										ImGui::BulletText(dir.c_str());
										ImGui::SameLine();
										if (ImGui::SmallButton("-"))
										{
											mod->m_Dirs.erase(mod->m_Dirs.begin() + i);
											ImGui::PopID();
											break;
										}
										ImGui::PopID();
									}

									ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
								}
								if (ImGui::SmallButton("+"))
								{
									Explorer_Setup("", &prof->m_root, [&](const std::vector<FileSystem::Directory*>& dirs)
										{
											if (dirs.empty())
												return;

											for (auto& dir : dirs)
											{
												if (!dir)
													continue;

												Utils::String relPath = dir->m_path.c_str() + prof->m_root.m_path.length();
												if (relPath.c_str()[0] == '\\')
													relPath.erase(0, 1);

												prof->m_ModInfo->m_Dirs.push_back(relPath);
											}
										}, nullptr, EXPRL_CHOOSE_ONLY_DIRECTORY);
								}
								ImGui::PopID();
								if (mod->m_ConfigSchema && ImGui::Button("Config"))
									ImGui::OpenPopup("##MOD_CONFIG_POPUP");
								if (ImGui::BeginPopup("##MOD_CONFIG_POPUP"))
								{
									for (auto& group : mod->m_ConfigSchema->m_groups)
									{
										ImGui::PushID(&group);

										ImGui::SeparatorText(group.m_displayname.c_str());

										if (mod->m_ConfigSchema->m_IniFile == "mod.ini")
										{
											for (auto& elem : group.m_elements)
											{
												ImGui::PushID(&elem);

												Utils::String preview;
												for (auto& enm : mod->m_ConfigSchema->m_enums)
												{
													if (enm.m_TypeIdentifier == elem.m_type)
													{
														int inclDir = -1;
														if (sscanf(elem.m_name.c_str(), "IncludeDir%d", &inclDir) == 1 && mod->m_Dirs[inclDir] == enm.m_Value)
															preview = enm.m_DisplayName;
													}
												}
												ImGui::TextUnformatted(elem.m_displayname.c_str());
												Hint(elem.m_description.c_str());
												ImGui::Spacing();
												if (ImGui::BeginCombo("", preview.c_str(), ImGuiComboFlags_WidthFitPreview))
												{
													for (auto& enm : mod->m_ConfigSchema->m_enums)
													{
														if (enm.m_TypeIdentifier == elem.m_type)
														{
															bool bSelected = false;
															for (auto& dir : prof->m_ModInfo->m_Dirs)
															{
																if (dir == enm.m_Value)
																	bSelected = true;
															}

															if (ImGui::Selectable(enm.m_DisplayName.c_str(), bSelected))
															{
																int inclDirId = -1;
																if (sscanf(elem.m_name.c_str(), "IncludeDir%d", &inclDirId) == 1)
																	prof->m_ModInfo->m_Dirs[inclDirId] = enm.m_Value;
															}

															if (!enm.m_Description.empty())
																Hint(enm.m_Description.c_str());

															if (bSelected)
																ImGui::SetItemDefaultFocus();
														}
													}
													ImGui::EndCombo();
												}
												ImGui::PopID();
											}
										}
										else
										{
											if (!mod->m_ConfigSchema->m_bIniInUse)
											{
												FileSystem::File* file = prof->FindFile(mod->m_ConfigSchema->m_IniFile);
												if (file)
													new(&mod->m_ConfigSchema->m_ini) IniReader(file->m_path.c_str());

												mod->m_ConfigSchema->m_bIniInUse = true;
											}
											for (auto& group : mod->m_ConfigSchema->m_groups)
											{
												ImGui::PushID(&group);

												Utils::String preview;
												for (auto& elem : group.m_elements)
												{
													ImGui::PushID(&elem);
													for (auto& enm : mod->m_ConfigSchema->m_enums)
													{
														if (enm.m_TypeIdentifier == elem.m_type)
														{
															for (auto& sect : mod->m_ConfigSchema->m_ini)
															{
																for (auto& key : sect)
																	if (key.getValue() == enm.m_Value.c_str())
																		preview = enm.m_DisplayName;
															}
														}
													}
													ImGui::TextUnformatted(elem.m_displayname.c_str());
													Hint(elem.m_description.c_str());
													ImGui::Spacing();
													if (ImGui::BeginCombo("", preview.c_str(), ImGuiComboFlags_WidthFitPreview))
													{
														for (auto& enm : mod->m_ConfigSchema->m_enums)
														{
															if (enm.m_TypeIdentifier == elem.m_type)
															{
																bool bSelected = false;
																for (auto& sect : mod->m_ConfigSchema->m_ini)
																{
																	auto key = sect.get(elem.m_name.c_str());
																	if (key && key->getValue() == enm.m_Value.c_str())
																		bSelected = true;
																}

																if (ImGui::Selectable(enm.m_DisplayName.c_str(), bSelected))
																{
																	for (auto& sect : mod->m_ConfigSchema->m_ini)
																	{
																		auto key = sect.get(elem.m_name.c_str());
																		if (key)
																		{
																			key->getValue() = enm.m_Value.c_str();
																			mod->m_ConfigSchema->m_ini.WriteString(sect.getSectionName(), key->getKeyName(), enm.m_Value.c_str());
																		}
																	}
																}

																if (!enm.m_Description.empty())
																	Hint(enm.m_Description.c_str());

																if (bSelected)
																	ImGui::SetItemDefaultFocus();
															}
														}
														ImGui::EndCombo();
													}
													ImGui::PopID();
												}

												ImGui::PopID();
											}
										}
										ImGui::PopID();
									}

									ImGui::EndPopup();
								}
								if (ImGui::Button("Save"))
								{
									FileSystem::File* file = prof->FindFile("mod.ini");
									if (file) mod->Save(file);
									else
									{
										FileSystem::File file("mod.ini", 0);
										mod->Save(&file);
									}
								}
								if (ImGui::Button("Delete"))
								{
									FileSystem::File* file = prof->FindFile("mod.ini");
									if (file)
									{
										remove(file->m_path.c_str());
										for (auto it = prof->m_root.m_files.begin(); it != prof->m_root.m_files.end(); ++it)
										{
											if (&(*it) == file)
											{
												prof->m_root.m_files.erase(it);
												break;
											}
										}

										delete prof->m_ModInfo;
										prof->m_ModInfo = nullptr;
									}
								}
							}
							else
							{
								ImGui::Text("No additional info available for this mod.\nWould you like to create and provide additional info for this mod?");
								if (ImGui::Button("Create Mod Info"))
								{
									prof->m_ModInfo = new ModLoader::ModExtraInfo();

									if (FILE* file = fopen((prof->m_root.m_path / "mod.ini").c_str(), "w"))
										fclose(file);

									prof->m_root.m_files.push_back(FileSystem::File((prof->m_root.m_path / "mod.ini").c_str(), 0));
								}
							}
							ImGui::EndPopup();
						}
						ImGui::TableNextColumn();
						if (prof->m_ModInfo)
							ImGui::Text(prof->m_ModInfo->m_author.c_str());
						else
							ImGui::Text("");
						if (ImGui::IsItemClicked())
						{
							if (prof->m_ModInfo)
								ThreadWork::AddThread(new cThread([](cThread*, LPVOID pParam)
									{
										ShellExecuteA(0, "open", ((ModLoader::ModProfile*)pParam)->m_ModInfo->m_authorURL.c_str(), NULL, NULL, 0);
									}, prof));
						}
						ImGui::TableNextColumn();
						if (prof->m_ModInfo)
							ImGui::Text(prof->m_ModInfo->m_version.c_str());
						else
							ImGui::Text("");
						ImGui::PopID();
					}

					ImGui::EndTable();

					if (bRequireMoving)
					{
						if (moveTo != -1 && moveFrom != -1 && moveTo != moveFrom)
						{
							ModLoader::ModProfile* prof = ModLoader::Profiles[moveFrom];

							ModLoader::Profiles.erase(ModLoader::Profiles.begin() + moveFrom);
							ModLoader::Profiles.insert(ModLoader::Profiles.begin() + moveTo, prof);
						}

						bRequireMoving = false;
						moveTo = moveFrom = -1;
					}
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Settings"))
		{
			gui::GUIHotkey.Draw("Menu Hotkey");
			ImGui::SeparatorText("Mod Loader");
			ImGui::Checkbox("Load mods", &ModLoader::bLoadMods);
			ImGui::Checkbox("Load files", &ModLoader::bLoadFiles);
			ImGui::Checkbox("Load scripts", &ModLoader::bLoadScripts);
			ImGui::Checkbox("Save RAM", &ModLoader::bSaveRAM);
			HelpTip("If enabled, it will load the mod despite being disabled\nNo need to worry since these mods will not be picked for mod loading");
			ImGui::SeparatorText("Log");
			ImGui::Checkbox("Enable Logging", &Logger::bEnabled);
			ImGui::Checkbox("Flush Log to File Immediately", &Logger::bFlushImmediately);
			ImGui::SeparatorText("Updater");
			ImGui::Checkbox("Enable Updater", &Updater::bEnabled);
			if (ImGui::Button("Check for Updates Now"))
			{
				bUserNotice = false;
				Updater::CheckSync();
				bManualUpdateCheck = true;
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("About"))
		{
			ImGui::Text("Mod Loader %s", Utils::FloatStringNoTralingZeros(Updater::fCurrentVersion).c_str());
			if (Updater::eUpdateStatus == Updater::UPDATE_STATUS_AVAILABLE)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), "New version is available");
			}

			ImGui::SeparatorText("Socials");
			if (g_Socials.empty())
			{
				ImGui::TextDisabled("Not available.");
			}
			else
			{
				for (int i = 0; i < (int)g_Socials.size(); i++)
				{
					if (ImGui::Button(g_Socials[i].m_Name.c_str()))
					{
						ThreadWork::AddThread([](cThread*, LPVOID pParam)
							{
								ShellExecuteA(0, "open", ((Link*)pParam)->m_URL.c_str(), NULL, NULL, SW_SHOWNORMAL);
							}, &g_Socials[i]);
					}
					if (i < (int)g_Socials.size() - 1)
						ImGui::SameLine();
				}
			}
			ImGui::SeparatorText("Donations");
			if (g_Donations.empty())
			{
				ImGui::TextDisabled("Not available.");

			}
			else
			{
				for (int i = 0; i < (int)g_Donations.size(); i++)
				{
					if (ImGui::Button(g_Donations[i].m_Name.c_str()))
					{
						ThreadWork::AddThread([](cThread*, LPVOID pParam)
							{
								ShellExecuteA(0, "open", ((Link*)pParam)->m_URL.c_str(), NULL, NULL, SW_SHOWNORMAL);
							}, &g_Donations[i]);
					}
					if (i < (int)g_Donations.size() - 1)
						ImGui::SameLine();
				}
			}
			ImGui::SeparatorText("License");

			for (std::pair<const char*, const char*> pair : { std::pair("ImGui", g_ImGuiLicense), {"HDE x86", g_HdeLicense86 }, {"HDE x64", g_HdeLicense64 } })
			{
				if (ImGui::CollapsingHeader(pair.first))
				{
					ImGui::TextWrapped(pair.second);
				}
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	ImGui::End();

	DrawMiniExplorer();
}