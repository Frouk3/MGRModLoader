#include "ModLoader.h"
#include <stdio.h>
#include <filesystem>
#include "injector/injector.hpp"
#include <shared.h>
#include <common.h>
#include <ini.h>

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
					auto prof = new ModLoader::ModProfile(fd.cFileName);
					if (prof)
					{
						ModLoader::Profiles.push_back(prof);
						prof->Load(prof->m_name);
						LOGINFO("Loading %s(%s, %d)", prof->m_name, prof->m_bEnabled ? "Enabled" : "Disabled", prof->m_nPriority);
					}
				}
				else
				{
					LOGINFO("Profile name exceeds 64 characters, simplify the name!");
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

	WIN32_FIND_DATA fd;
	HANDLE hFind = (HANDLE)-1;

	LOGINFO("Loading scripts...");

	int scriptsAmount = 0;

	for (auto& prof : ModLoader::Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		char searchPath[MAX_PATH];

		sprintf(searchPath, "%s\\*.asi", prof->getMyPath().c_str());

		hFind = FindFirstFileA(searchPath, &fd);
		if (hFind != (HANDLE)-1)
		{
			do
			{
				char filePath[MAX_PATH];
				sprintf(filePath, "%s\\%s", prof->getMyPath().c_str(), fd.cFileName);

				HMODULE h = LoadLibraryA(filePath);
				SetCurrentDirectoryA(origPath);

				if (h != NULL)
				{
					LOGINFO("Loading %s script...", fd.cFileName)
					auto procedure = (void(*)())GetProcAddress(h, "InitializeASI");

					if (procedure != NULL)
						procedure();

					scriptsAmount += 1;
				}
				else
				{
					LOGERROR("Failed to load %s!", fd.cFileName)
				}
			} while (FindNextFileA(hFind, &fd) != 0);

			FindClose(hFind);
		}
	}
	LOGINFO("%d scripts loaded!", scriptsAmount);
}

void ModLoader::startup()
{
	Load();

	openProfiles(getModFolder().c_str());
	openScripts();
	LOGINFO("%d mods loaded!", Profiles.m_nSize);

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
	IniReader ini("MGRModLoaderSettings.ini");

	ini.WriteBool("ModLoader", "IgnoreScripts", bIgnoreScripts);
	ini.WriteBool("ModLoader", "IgnoreFiles", bIgnoreDATLoad);
	ini.WriteBool("ModLoader", "EnableLogging", bEnableLogging);

	for (auto& profile : Profiles)
		profile->Save();
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

void ModLoader::ModProfile::Save()
{
	LOGINFO("Saving %s...", this->m_name)
	IniReader prof(getModFolder() + "\\profiles.ini");

	prof.WriteBool(this->m_name, "Enabled", this->m_bEnabled);
	prof.WriteInt(this->m_name, "Priority", this->m_nPriority);
}