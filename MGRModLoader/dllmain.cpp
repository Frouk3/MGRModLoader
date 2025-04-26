#define SHARED_USE_EX_FUNCS
#include <shared.h>
#include <Events.h>
#include "gui.h"
#include "imgui/imgui.h"
#include "ini.h"
#include "ModLoader.h"
#include <GameMenuStatus.h>
#define TAKE_TIMER_SNAPSHOTS 0
#include "Timer.hpp"
#include "FilesTools.hpp"
#include "Hooks.h"

bool bOpenUpdaterMB = false;

class ModLoaderPlugin
{
public:
	static inline void InitGui()
	{
		Events::OnDeviceReset.before += gui::OnReset::Before;
		Events::OnDeviceReset.after += gui::OnReset::After;
		Events::OnPresent += gui::OnEndScene;
	}

	ModLoaderPlugin()
	{
		CTimer timer;

		sHooks::Init();

		gui::GUIHotkey.Load();

		InitGui();

		Logger::Init();
		LOG("Initialization(version %s)", Utils::FloatStringNoTralingZeros(Updater::fCurrentVersion).c_str());
		if (!ModloaderHeap.create(-1, "MLHEAP"))
			LOGERROR("Failed to initialize heap.");
		ModLoader::Startup();

		for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
		{
			prof->FileWalk([&](FileSystem::File& file)
				{
					if (const char* ext = strrchr(file.getName(), '.'); ext && !strcmp(ext, ".cpk"))
						++CriWare::iAvailableCPKs;
				});
		}

		{
			injector::scoped_unprotect vp(shared::base + 0x14CDE20, 4u);

			*(int*)(shared::base + 0x14CDE20) += CriWare::iAvailableCPKs;
		}

		Updater::Init();

		Events::OnGameStartupEvent += []()
			{
				if (Updater::bEnabled && Updater::eUpdateStatus == Updater::UPDATE_STATUS_AVAILABLE)
					bOpenUpdaterMB = true;
			};
		
		Events::OnMainCleanupEvent += []()
			{
				gui::GUIHotkey.Save();

				Updater::SaveConfig();
				Logger::SaveConfig();
				ModLoader::Shutdown();
				Logger::Close();
			};

		Events::OnUpdateEvent.after += []()
			{
				shared::ExPressKeyUpdate();
				if (g_GameMenuStatus == InMenu)
					gui::GUIHotkey.Update();
			};

		Events::OnSceneCleanupEvent.after += []()
			{
				gui::GUIHotkey.m_bToggle = false;
			};
		
		timer.stop();

		LOG_TIMER(timer, "Took %.3f seconds");
	}
} ModLoaderInitInstance;

char* stristr(const char* str1, const char* str2) 
{
	if (!*str2)
		return (char*)str1;

	for (; *str1 != '\0'; str1++) {
		const char* h = str1;
		const char* n = str2;

		while (*h != '\0' && *n != '\0' && tolower((unsigned char)*h) == tolower((unsigned char)*n)) 
		{
			h++;
			n++;
		}

		if (*n == '\0')
			return (char*)str1;
	}

	return NULL;
}

bool Explorer_Enabled = false;
FileSystem::Directory* Explorer_MainDirectory = nullptr;
const char* Explorer_CustomFilter = "";
Utils::String Explorer_Filter = ""; // So we could make a combo with switching extensions
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

void Explorer_SetCallback(const std::function<void(const std::vector<FileSystem::Directory*>&)>& dirCallback, const std::function<void(const std::vector<FileSystem::File*>&)>& fileCallback)
{
	Explorer_OnSelectDirectoryCallback = dirCallback;
	Explorer_OnSelectFileCallback = fileCallback;
}

void Explorer_Setup(const char* filter, FileSystem::Directory* mainDir, const std::function<void(const std::vector<FileSystem::Directory*>&)>& dirCb = nullptr, const std::function<void(const std::vector<FileSystem::File*>&)>& fileCb = nullptr, unsigned int flags = EXPLR_CHOOSE_ALLOW_DIRECTORY | EXPLR_CHOOSE_ALLOW_FILE)
{
	Explorer_selectedDirs.clear();
	Explorer_selectedFiles.clear();

	Explorer_Enabled = true;
	Explorer_CustomFilter = filter;
	Explorer_MainDirectory = mainDir;

	Explorer_Filter = filter;

	Explorer_SetCallback(dirCb, fileCb);
}

void DrawMiniExplorer()
{
	if (Explorer_Enabled && Explorer_MainDirectory)
	{
		std::vector<Utils::String> allowedExtensions;

		const char* item = Explorer_CustomFilter;

		const unsigned int flags = Explorer_flags;

		while (item && *item)
		{
			if (*item)
				allowedExtensions.push_back(item);

			item += strlen(item) + 1;
		}

		static FileSystem::Directory* queueDirectory = nullptr;
		static void* lastItemChoosen = nullptr;

		auto WasDirSelected = [&](FileSystem::Directory* dir) -> bool
			{
				for (auto& sbd : Explorer_selectedDirs)
					if (sbd == dir)
						return true;

				return lastItemChoosen == dir;
			};

		auto WasFileSelected = [&](FileSystem::File* file) -> bool
			{
				for (auto& files : Explorer_selectedFiles)
					if (file == files)
						return true;

				return lastItemChoosen == file;
			};

		ImGui::Begin("##MiniExplorer", &Explorer_Enabled, ImGuiWindowFlags_NoCollapse);
		ImVec2 pos = ImGui::GetCursorPos();

		ImGui::SetNextWindowSize(ImVec2(ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x, 70.f));
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 80.f));
		ImGui::BeginChild("FileBar", ImVec2(ImGui::GetWindowContentRegionMax().x, 40.f), ImGuiChildFlags_Borders);
		ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionAvail().x - 110.f, ImGui::GetWindowContentRegionMax().y - 30.f));
		if (ImGui::Button("OK", ImVec2(50, 25)))
		{
			Explorer_OnSelectDirectoryCallback(Explorer_selectedDirs);
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
		ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - 120.f, ImGui::GetCursorStartPos().y));
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
		Utils::String ExplorerFilterPreview = "*";
		if (Explorer_Filter[0u] != '*')
			ExplorerFilterPreview += Explorer_Filter;
		else
			ExplorerFilterPreview = Explorer_Filter;
		if (ImGui::BeginCombo("##EXPLR_EXT_SELECT", ExplorerFilterPreview))
		{
			if (ImGui::Selectable("Any file(*.*)", !stricmp(Explorer_Filter, "*")))
				Explorer_Filter = "*";
			for (int i = 0; i < allowedExtensions.size(); i++)
			{
				const bool bIsSelected = allowedExtensions[i] == Explorer_Filter;

				ImGui::PushID(i);
				Utils::String filterPreview = "*";
				if (allowedExtensions[i][0u] != '*')
					filterPreview += allowedExtensions[i];
				else
					filterPreview = allowedExtensions[i];
				if (ImGui::Selectable(filterPreview, bIsSelected))
					Explorer_Filter = allowedExtensions[i];

				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::EndChild();
		ImGui::SetCursorPos(pos);
		ImGui::PushID("##History");
		{
			FileSystem::Directory* direct = Explorer_MainDirectory;
			lib::StaticArray<FileSystem::Directory*, 32> directories;

			do
			{
				directories.push_front(direct);
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

		if (Explorer_MainDirectory->m_parent && ImGui::Selectable("..", false, ImGuiSelectableFlags_AllowDoubleClick))
		{
			lastItemChoosen = Explorer_MainDirectory;
			Explorer_MainDirectory = Explorer_MainDirectory->m_parent;
		}

		for (auto& subdir : Explorer_MainDirectory->m_subdirs)
		{
			bool selectable = ImGui::Selectable(subdir->getName(), WasDirSelected(subdir), ImGuiSelectableFlags_AllowDoubleClick);
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selectable)
			{
				queueDirectory = subdir;
				lastItemChoosen = subdir;
			}
			else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) && ImGui::IsItemHovered() && flags & EXPLR_CHOOSE_ALLOW_DIRECTORY)
			{
				if (shared::IsKeyPressed(VK_LCONTROL, true))
				{
					Explorer_selectedDirs.push_back(subdir);
				}
				else
				{
					Explorer_selectedDirs.clear();
					Explorer_selectedFiles.clear();
					Explorer_selectedDirs.push_back(subdir);
				}

				lastItemChoosen = subdir;
			}
		}
		for (auto& files : Explorer_MainDirectory->m_files)
		{
			if (Explorer_Filter != "*" && allowedExtensions.size())
			{
				bool bFilterOut = false;
				if (const char* ext = strrchr(files.getName(), '.'); ext)
				{
					if (!stricmp(ext, Explorer_Filter))
						bFilterOut = true;
				}

				if (!bFilterOut)
					continue;
			}
			bool selectable = ImGui::Selectable(Utils::format("%s [%s]", files.getName(), Utils::getProperSize(files.m_filesize).c_str()), WasFileSelected(&files), ImGuiSelectableFlags_AllowDoubleClick);

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selectable)
			{

			}
			else if (ImGui::IsItemHovered() && flags & EXPLR_CHOOSE_ALLOW_FILE)
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
				{
					if (shared::IsKeyPressed(VK_LCONTROL, true))
					{
						Explorer_selectedFiles.push_back(&files);
					}
					else
					{
						Explorer_selectedDirs.clear();
						Explorer_selectedFiles.clear();
						Explorer_selectedFiles.push_back(&files);
					}
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
}

int ImMessageBox(const char* label, const char* description, unsigned int type)
{
	int result = 0;
	const ImVec2 ButtonSize(80.f, 0.f); // Slightly wider buttons for better usability
	const float ButtonSpacing = 10.f;

	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 80.f);
		ImGui::Text("%s", description); // Wrapped text for better formatting
		ImGui::PopTextWrapPos();
		ImGui::Dummy(ImVec2(0.0f, 10.0f)); // Add vertical spacing

		auto AddButton = [&](const char* text, int returnValue) -> bool
			{
				if (ImGui::Button(text, ButtonSize))
				{
					result = returnValue;
					return true;
				}
				return false;
			};

		// Center buttons
		ImGui::SetCursorPosX((ImGui::GetWindowSize().x - (
			(type == MB_YESNOCANCEL ? 3 : (type == MB_OKCANCEL || type == MB_YESNO ? 2 : 1)) * (ButtonSize.x + ButtonSpacing) - ButtonSpacing
			)) * 0.5f);

		switch (type)
		{
		case MB_OK:
			if (AddButton("OK", IDOK)) ImGui::CloseCurrentPopup();
			break;

		case MB_OKCANCEL:
			if (AddButton("OK", IDOK)) ImGui::CloseCurrentPopup();
			ImGui::SameLine(0.0f, ButtonSpacing);
			if (AddButton("Cancel", IDCANCEL)) ImGui::CloseCurrentPopup();
			break;

		case MB_YESNO:
			if (AddButton("Yes", IDYES)) ImGui::CloseCurrentPopup();
			ImGui::SameLine(0.0f, ButtonSpacing);
			if (AddButton("No", IDNO)) ImGui::CloseCurrentPopup();
			break;

		case MB_YESNOCANCEL:
			if (AddButton("Yes", IDYES)) ImGui::CloseCurrentPopup();
			ImGui::SameLine(0.0f, ButtonSpacing);
			if (AddButton("No", IDNO)) ImGui::CloseCurrentPopup();
			ImGui::SameLine(0.0f, ButtonSpacing);
			if (AddButton("Cancel", IDCANCEL)) ImGui::CloseCurrentPopup();
			break;

		default:
			break;
		}

		ImGui::EndPopup();
	}

	return result;
}



void gui::RenderWindow()
{
	if (!ModLoader::bInit)
	{
		ImGui::Begin("Mod Loader", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("Failed to initialize Mod Loader.");
		ImGui::End();
		return;
	}
	if (bOpenUpdaterMB)
	{
		ImGui::OpenPopup("Updater");
		bOpenUpdaterMB = false;
	}
	ImMessageBox("Updater", Utils::format("New version of Mod Loader is available!(Current: %s, Latest: %s)", Utils::FloatStringNoTralingZeros(Updater::fCurrentVersion).c_str(), Utils::FloatStringNoTralingZeros(Updater::fLatestVersion).c_str()), MB_OK);
	if ((GUIHotkey.m_bToggle || GUIHotkey.GetHotkeyType() == Hotkey::HT_OFF) && g_GameMenuStatus == InMenu)
	{
		ImGui::Begin("Mod Loader", GUIHotkey.GetHotkeyType() == Hotkey::HT_OFF ? nullptr : &GUIHotkey.m_bToggle);
		if (ImGui::BeginTabBar("Main", ImGuiTabBarFlags_NoTooltip))
		{
			if (ModLoader::bLoadMods && ImGui::BeginTabItem("Profiles"))
			{
				static bool bSearchBar = false;
				static Utils::String SearchBarContent;
				if (shared::IsKeyPressed(VK_LCONTROL) && shared::IsKeyPressed('F', false))
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
					ImGui::InputText("##search", SearchBarContent);
					ImGui::SetItemDefaultFocus();

					ImGui::SameLine();
					if (ImGui::Button("Clear", ImVec2(ImGui::GetContentRegionAvail().x - 5.f, 0)))
					{
						SearchBarContent.clear();
						SearchBarContent.shrink_to_fit();
					}

					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(2);
				}

				if (ImGui::BeginTable("ML_PROFILES", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders))
				{
					static bool bRequireMoving = false;
					static size_t profileToMove = -1;
					static size_t profileToSwap = -1;

					ImGui::TableSetupColumn("Mods");
					ImGui::TableSetupColumn("Author");
					ImGui::TableHeadersRow();
					for (ModLoader::ModProfile*& prof : ModLoader::Profiles)
					{
						Utils::String title = prof->m_name;

						if (prof->m_ModInfo && !prof->m_ModInfo->m_title.empty())
						{
							title = prof->m_ModInfo->m_title;
							if (!prof->m_ModInfo->m_version.empty())
								title += Utils::format(" - %s", prof->m_ModInfo->m_version.c_str());
						}

						if (!SearchBarContent.empty() && !stristr(title.c_str(), SearchBarContent.c_str()))
							continue;

						ImGui::TableNextRow();
						ImGui::PushID(prof);

						ImGui::TableNextColumn();

						ImGui::Checkbox(title, &prof->m_bEnabled);

						if (ImGui::BeginDragDropSource())
						{
							profileToMove = &prof - ModLoader::Profiles.begin();
							ImGui::SetDragDropPayload("##ML_MOVE_PROFILE", prof, sizeof(prof));

							ImGui::EndDragDropSource();
						}

						if (ImGui::BeginDragDropTarget())
						{
							const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("##ML_MOVE_PROFILE");

							if (payload)
							{
								ModLoader::ModProfile* profile = (ModLoader::ModProfile*)payload->Data;

								if (prof != profile)
								{
									bRequireMoving = true;
									profileToSwap = &prof - ModLoader::Profiles.begin();
								}
							}

							ImGui::EndDragDropTarget();
						}

						if (ImGui::IsItemHovered())
						{
							if (ImGui::IsMouseClicked(ImGuiMouseButton_Right, false))
							{
								ImGui::OpenPopup("##POPUP_ADV_SETTINGS");
							}
							if (prof->m_ModInfo && prof->m_ModInfo->m_description)
								ImGui::SetTooltip("%s\n%s\n[%s]", prof->m_ModInfo->m_description.c_str(), prof->m_ModInfo->m_date.c_str(), Utils::getProperSize(prof->m_root.m_filesize).c_str());
							else
								ImGui::SetTooltip("<No description given>\n[%s]", Utils::getProperSize(prof->m_root.m_filesize).c_str());
						}

						if (ImGui::BeginPopup("##POPUP_ADV_SETTINGS"))
						{
							if (prof->m_ModInfo)
							{
								ImGui::InputText("Author", prof->m_ModInfo->m_author);
								ImGui::InputText("Mod Title", prof->m_ModInfo->m_title);
								ImGui::InputText("Version", prof->m_ModInfo->m_version);
								ImGui::InputText("Description", prof->m_ModInfo->m_description);
								ImGui::InputText("Author URL", prof->m_ModInfo->m_authorURL);
								ImGui::InputText("Date", prof->m_ModInfo->m_date);

								ImGui::PushID("DLLsFields");
								ImGui::Text(prof->m_ModInfo->m_DLLs.empty() ? "There's no DLLs" : "DLLs:");
								ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());

								for (size_t i = 0; i < prof->m_ModInfo->m_DLLs.size(); i++)
								{
									ImGui::BulletText("%s", prof->m_ModInfo->m_DLLs[i].c_str());
									ImGui::SameLine();
									ImGui::PushID(&prof->m_ModInfo->m_DLLs[i]);
									if (ImGui::SmallButton("-"))
									{
										prof->m_ModInfo->m_DLLs.erase(prof->m_ModInfo->m_DLLs.begin() + i);
										ImGui::PopID();
										break;
									}
									ImGui::PopID();
								}
								if (ImGui::SmallButton("+"))
								{
									Explorer_Setup(".DLL\0", &prof->m_root, nullptr,
										[&](std::vector<FileSystem::File*> files) -> void
										{
											for (auto& file : files)
											{
												bool bIsInList = false;

												std::for_each(prof->m_ModInfo->m_DLLs.begin(), prof->m_ModInfo->m_DLLs.end(), [&](const Utils::String &dllStr)
													{
														if (dllStr == file->getName())
															bIsInList = true;
													});

												if (!bIsInList)
													prof->m_ModInfo->m_DLLs.push_back(file->getName());
											}
										});
								}
								ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
								ImGui::PopID();

								ImGui::PushID("IncludeList");
								ImGui::Text(prof->m_ModInfo->m_Dirs.empty() ? "There's no include directories" : "Include Directories:");
								ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
								for (int i = 0; i < prof->m_ModInfo->m_Dirs.size(); i++)
								{
									ImGui::PushID(&prof->m_ModInfo->m_Dirs[i]);
									ImGui::BulletText(prof->m_ModInfo->m_Dirs[i]);
									ImGui::SameLine();
									if (ImGui::SmallButton("-"))
									{
										prof->m_ModInfo->m_Dirs.erase(prof->m_ModInfo->m_Dirs.begin() + i);
										break;
									}
									ImGui::PopID();
								}
								if (ImGui::SmallButton("+"))
								{
									Explorer_Setup("", &prof->m_root, [&](const std::vector<FileSystem::Directory*>& dirList)
										{
											if (!dirList.empty())
											{
												for (auto& dir : dirList)
													prof->m_ModInfo->m_Dirs.push_back(dir->getName());
											}
										}, nullptr, EXPRL_CHOOSE_ONLY_DIRECTORY);
								}
								ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
								ImGui::PopID();

								if (prof->m_ModInfo->m_ConfigSchema && ImGui::Button("Configuration"))
									ImGui::OpenPopup("##PROFILE_CONFIG_EDIT");

								if (ImGui::BeginPopup("##PROFILE_CONFIG_EDIT"))
								{
									for (auto& group : prof->m_ModInfo->m_ConfigSchema->m_groups)
									{
										ImGui::SeparatorText(group.m_displayname);
										for (auto& element : group.m_elements)
										{
											ImGui::PushID(&element);
											Utils::String preview;
											Utils::String value;
											for (auto& enm : prof->m_ModInfo->m_ConfigSchema->m_enums)
											{
												if (enm.m_TypeIdentifier == element.m_type)
												{
													int inclDirId = -1;
													if (sscanf(element.m_name, "IncludeDir%d", &inclDirId) == 1 && prof->m_ModInfo->m_Dirs[inclDirId] == enm.m_Value)
														preview = enm.m_DisplayName;
												}
											}
											ImGui::TextUnformatted(element.m_displayname);
											ImGui::Spacing();
											if (ImGui::BeginCombo("", preview, ImGuiComboFlags_WidthFitPreview))
											{
												for (auto& enm : prof->m_ModInfo->m_ConfigSchema->m_enums)
												{
													if (enm.m_TypeIdentifier == element.m_type)
													{
														bool bSelected = false;
														for (auto& dir : prof->m_ModInfo->m_Dirs)
														{
															if (dir == enm.m_Value)
																bSelected = true;
														}

														if (ImGui::Selectable(enm.m_DisplayName, bSelected))
														{
															int inclDirId = -1;
															if (sscanf(element.m_name, "IncludeDir%d", &inclDirId) == 1)
																prof->m_ModInfo->m_Dirs[inclDirId] = enm.m_Value;
														}

														if (bSelected)
															ImGui::SetItemDefaultFocus();

														if (!enm.m_Description.empty() && ImGui::IsItemHovered())
															ImGui::SetTooltip(enm.m_Description);
													}
												}
												ImGui::EndCombo();
											}
											if (!element.m_description.empty() && ImGui::IsItemHovered())
												ImGui::SetTooltip(element.m_description);
											ImGui::PopID();
										}
									}
									ImGui::EndPopup();
								}
								if (ImGui::Button("Save"))
								{
									FileSystem::File* mod = prof->FindFile("mod.ini");
									prof->m_ModInfo->Save(mod);

									if (!mod)
									{
										prof->m_root.m_files.push_back({ prof->m_root.m_path / "mod.ini" });
										
										mod = prof->FindFile("mod.ini");
										prof->m_ModInfo->Save(mod);
									}
								}
								if (ImGui::Button("Delete"))
								{
									delete prof->m_ModInfo;
									prof->m_ModInfo = nullptr;

									FileSystem::File* mod = prof->FindFile("mod.ini");

									if (mod)
										remove(mod->m_path);
								}
							}
							else
							{
								ImGui::Text("This mod doesn't have any information.");
								if (ImGui::Button("Add"))
								{
									prof->m_ModInfo = new ModLoader::ModExtraInfo();

									if (FILE* file = fopen(prof->m_root.m_path / "mod.ini", "w"); file)
										fclose(file);
								}
							}

							ImGui::EndPopup();
						}

						if (prof->m_ModInfo)
						{
							if (!prof->m_ModInfo->m_author.empty())
							{
								ImGui::TableNextColumn();
								ImGui::TextUnformatted(prof->m_ModInfo->m_author);
								if (!prof->m_ModInfo->m_authorURL.empty() && ImGui::IsItemHovered() && ImGui::IsItemClicked(ImGuiMouseButton_Left))
									ShellExecute(0, "open", prof->m_ModInfo->m_authorURL, nullptr, nullptr, 0);
							}
						}

						ImGui::PopID();
					}
					if (bRequireMoving)
					{
						ModLoader::ModProfile*& with = ModLoader::Profiles[profileToSwap];
						ModLoader::ModProfile*& element = ModLoader::Profiles[profileToMove];
						ModLoader::Profiles.swap(with, element);
						std::swap(with->m_place, element->m_place);
						bRequireMoving = false;
					}
					ImGui::EndTable();
				}
				ImGui::EndTabItem();
			}
			
			if (ImGui::BeginTabItem("Settings"))
			{
				GUIHotkey.Draw("Menu Hotkey");
				ImGui::Checkbox("Check for updates", &Updater::bEnabled);
				Hint("Will automatically check updates when you're launching the game.");
				ImGui::Checkbox("Enable mods", &ModLoader::bLoadMods);
				ImGui::Checkbox("Enable script loading", &ModLoader::bLoadScripts);
				ImGui::Checkbox("Enable file loading", &ModLoader::bLoadFiles);
				if (ImGui::Checkbox("Enable logging", &Logger::bEnabled))
				{
					if (!Logger::bEnabled)
					{
						remove(Logger::LogFilePath);
						Logger::Close();
					}
					else
					{
						Logger::Open();
					}
				}
				Hint("Helps developers to find issues that were caused with certain mods.\n"
					 "If a certain mod causes a crash, and you have this option disabled, enable it in ini file.");
				ImGui::Checkbox("Flush Immediately", &Logger::bFlushImmediately);
				HelpTip("Writes everything in the log immediately, if disabled, will write everything after game exit.");
				if (ImGui::Button("Check Updates"))
				{
					Updater::hUpdateThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD
						{
							bOpenUpdaterMB = Updater::CheckForOnce() && Updater::eUpdateStatus == Updater::UPDATE_STATUS_AVAILABLE;

							return 0;
						}, nullptr, 0, nullptr);
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("About"))
			{
				// bla bla bla bla
				ImGui::Text("Mod Loader %s", Utils::FloatStringNoTralingZeros(Updater::fCurrentVersion).c_str());
				if (Updater::eUpdateStatus == Updater::UPDATE_STATUS_AVAILABLE)
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), "New version is available");
				}
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

		DrawMiniExplorer();
	}
}