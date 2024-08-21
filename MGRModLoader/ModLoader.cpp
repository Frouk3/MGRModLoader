#include "ModLoader.h"
#include <stdio.h>
#include <filesystem>
#include "injector/injector.hpp"
#include <shared.h>
#include <common.h>
#include <ini.h>

inline cHeapManager* GetHeapManager()
{
	static cHeapManager manager;

	return &manager;
}

extern BOOL FileExists(const char* filename);

namespace fs = std::filesystem;

void openProfiles(const char *directory)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = (HANDLE)-1;
	char searchPath[MAX_PATH];

	LOGINFO("Reading profiles...")

	sprintf(searchPath, "%s\\*", directory);

	hFind = FindFirstFileA(searchPath, &fd);
	if (hFind == (HANDLE)-1)
	{
		ModLoader::bInitFailed = true;
		LOGERROR("Reading failed!");
		return;
	}
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (strncmp(fd.cFileName, ".", 1u) && strncmp(fd.cFileName, "..", 2u))
			{
				if (strlen(fd.cFileName) < 64u)
				{
					ModLoader::ModProfile* prof = GetHeapManager()->allocate<ModLoader::ModProfile>();
					if (prof)
					{
						*prof = ModLoader::ModProfile(fd.cFileName);
						ModLoader::Profiles.push_back(prof);
						prof->Load(prof->m_name);
						LOGINFO("Loading %s(%s, %d)", prof->m_name, prof->m_bEnabled ? "Enabled" : "Disabled", prof->m_nPriority);
					}
				}
				else
				{
					LOGINFO("Profile name exceeds 64 characters, simplify the name for %s!", fd.cFileName);
				}
			}
		}
	} while (FindNextFileA(hFind, &fd) != 0);

	FindClose(hFind);
	LOGINFO("Profiles reading done!");
}

void openScripts()
{
	if (ModLoader::bIgnoreScripts)
		return;

	char origPath[MAX_PATH];
	GetCurrentDirectoryA(sizeof(origPath), origPath);

	LOGINFO("Loading scripts...");

	int scriptsAmount = 0;

	for (auto& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		prof->FileWalk([&](ModLoader::ModProfile::File* file)
			{
				// Using strlow just to be sure if the asi may contain high case ".asi"
				if (strcmp(Utils::strlow((const char*)&file->m_path[strlen(file->m_path) - 4]), ".asi") || file->m_bInSubFolder) // Do not load if not asi or asi is in sub folder // changed to strcmp, since if we just set .asi before extension, it will load anyway
					return;

				auto h = LoadLibraryA(file->m_path);

				if (h != NULL)
				{
					if (auto init = (void(*)())GetProcAddress(h, "InitializeASI"); init)
						init();

					auto asiName = strrchr(file->m_path, '\\') + 1;

					LOGINFO("Loading %s plugin...", asiName);

					++scriptsAmount;
				}
				else
				{
					LOGERROR("Failed to load %s plugin!", prof->m_name);
				}

				SetCurrentDirectoryA(origPath);
			});
	}
	LOGINFO("%d scripts loaded!", scriptsAmount);
}

inline LONGLONG GetLongFromLargeInteger(DWORD LowPart, DWORD HighPart)
{
	LARGE_INTEGER l;
	l.LowPart = LowPart;
	l.HighPart = HighPart;
	return l.QuadPart;
}

void findFiles(const char* directory, Hw::cFixedVector<ModLoader::ModProfile::File *>& files, const bool bInSubFolder = false)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	char searchPath[MAX_PATH];

	sprintf(searchPath, "%s\\*", directory);

	hFind = FindFirstFileA(searchPath, &fd);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		LOGERROR("Cannot read files for %s", directory);
		return;
	}
	do
	{
		if (strncmp(fd.cFileName, ".", 1u) && strncmp(fd.cFileName, "..", 2u))
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				char directoryPath[MAX_PATH];

				sprintf(directoryPath, "%s\\%s", directory, fd.cFileName);
				findFiles(directoryPath, files, true);
			}
			else
			{
				char path[MAX_PATH];

				sprintf(path, "%s\\%s", directory, fd.cFileName);

				auto fileSize = GetLongFromLargeInteger(fd.nFileSizeLow, fd.nFileSizeHigh);

				auto file = GetHeapManager()->allocate<ModLoader::ModProfile::File>();

				if (file)
				{
					*file = ModLoader::ModProfile::File(fileSize, path);

					file->m_bInSubFolder = bInSubFolder;

					files.push_back(file);
				}
			}
		}

	} while (FindNextFileA(hFind, &fd) != 0);

	FindClose(hFind);
}

void ModLoader::startup()
{
	Load();

	openProfiles(getModFolder().c_str());
	int enabledMods = 0;
	for (auto& profile : Profiles)
	{
		if (profile->m_bEnabled)
		{
			LOGINFO("%s -> Startup", profile->m_name);
			profile->Startup();
			enabledMods++;
		}
	}
	openScripts();
	LOGINFO("%d mod profiles loaded!", Profiles.m_nSize);
	LOGINFO("Only %d was enabled", enabledMods);

	if (Profiles.m_nSize)
		SortProfiles();

	bInit = true;
}

void ModLoader::SortProfiles()
{
	for (int i = 0; i < Profiles.m_nSize - 1; i++)
	{
		for (int j = 0; j < Profiles.m_nSize - i - 1; j++)
		{
			auto arr = Profiles.m_pStart;
			if (arr[j]->m_nPriority < arr[j + 1]->m_nPriority)
			{
				auto temp = arr[j];
				arr[j] = arr[j + 1];
				arr[j + 1] = temp;
			}
		}
	}
}

FILE *profileFile = nullptr;

void ModLoader::Load()
{
	IniReader ini("MGRModLoaderSettings.ini");

	bIgnoreScripts = ini.ReadBool("ModLoader", "IgnoreScripts", false);
	bIgnoreDATLoad = ini.ReadBool("ModLoader", "IgnoreFiles", false);
	bEnableLogging = ini.ReadBool("ModLoader", "EnableLogging", true);

	for (auto& profile : Profiles)
		profile->Load(profile->m_name);
}

void ModLoader::Save()
{
	remove((getModFolder() + "\\profiles.ini").c_str());
	profileFile = fopen((getModFolder() + "\\profiles.ini").c_str(), "a");
	IniReader ini("MGRModLoaderSettings.ini");

	ini.WriteBool("ModLoader", "IgnoreScripts", bIgnoreScripts);
	ini.WriteBool("ModLoader", "IgnoreFiles", bIgnoreDATLoad);
	ini.WriteBool("ModLoader", "EnableLogging", bEnableLogging);

	for (auto& profile : Profiles)
		profile->Save();

	fclose(profileFile);
}

std::string ModLoader::getModFolder()
{
	return std::string(path) + "\\mods";
}

void ModLoader::ModProfile::Load(const char* name)
{
	IniReader prof(getModFolder() + "\\profiles.ini");
	
	this->m_bEnabled = prof.ReadBool(name, "Enabled", true);
	this->m_nPriority = prof.ReadInt(name, "Priority", 7);
}

void ModLoader::ModProfile::Startup()
{
	if (!m_bStarted)
	{
		if (!m_files.m_pBegin)
		{
			m_files.m_pBegin = (File**)GetHeapManager()->allocate(sizeof(File*) * 1024);
			if (m_files.m_pBegin)
			{
				m_files.m_nCapacity = 1024;
				m_files.m_nSize = 0;
				m_files.field_10 = 1;
			}
			else
			{
				LOGERROR("Failed to startup the mod!");
			}
		}

		ReadFiles();

		if (m_files.m_pBegin)
		{
			for (auto file : m_files)
			{
				m_nTotalSize += file->m_nSize;
				LOGINFO("%s -> %s [%s]", m_name, file->m_path, Utils::getProperSize(file->m_nSize).c_str());
			}
			LOGINFO("%s -> Size: %s", m_name, Utils::getProperSize(m_nTotalSize).c_str());
		}
	}

	m_bStarted = true;
}

void ModLoader::ModProfile::Restart()
{
	m_bStarted = false;

	Shutdown();

	Startup();
}

void ModLoader::ModProfile::ReadFiles()
{
	char directory[MAX_PATH];

	sprintf(directory, "%s", getMyPath().c_str());

	findFiles(directory, m_files);
}

void ModLoader::ModProfile::Save()
{
	LOGINFO("Saving %s...", this->m_name)
	IniReader prof(getModFolder() + "\\profiles.ini");

	prof.WriteBool(this->m_name, "Enabled", this->m_bEnabled);
	prof.WriteInt(this->m_name, "Priority", this->m_nPriority);

	fprintf(profileFile, "\n"); // at least not stacking up on each other
	fflush(profileFile);
}