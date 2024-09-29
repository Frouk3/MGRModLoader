#include "ModLoader.h"
#include <stdio.h>
#include <filesystem>
#include "injector/injector.hpp"
#include <shared.h>
#include <common.h>
#include <ini.h>

extern BOOL FileExists(const char* filename);

namespace fs = std::filesystem;

template <typename T>
void reallocateVector(Hw::cFixedVector<T>& vector, size_t newSize)
{
	auto newVector = (T*)malloc(sizeof(T) * newSize);
	
	if (newVector)
	{
		for (int i = 0; i < vector.m_nSize; i++)
			newVector[i] = vector.m_pBegin[i];

		vector.m_nCapacity = newSize;
		free(vector.m_pBegin);
		vector.m_pBegin = newVector;
	}
}

void openProfiles(const char *directory)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = (HANDLE)-1;
	char searchPath[MAX_PATH];

	LOGINFO("Reading profiles...");

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
			if (fd.cFileName[0] != '.' && fd.cFileName[1] != '.')
			{
				if (strlen(fd.cFileName) < 64u)
				{
					auto prof = new ModLoader::ModProfile(fd.cFileName);
					if (prof)
					{
						ModLoader::Profiles.push_back(prof);
						prof->Load(prof->m_name);
						LOGINFO("Loading %s(%s, %d)", prof->m_name.c_str(), prof->m_bEnabled ? "Enabled" : "Disabled", prof->m_nPriority);
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
				if ((strrchr(file->m_path, '\\') + 1)[0] == '.' || (strrchr(file->m_path, '\\') + 1)[1] == '.')
					return;

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
					LOGERROR("%s -> Failed to load %s!", prof->m_name, file->m_path);
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

void findFiles(const char* directory, Hw::cFixedVector<ModLoader::ModProfile::File *>& files, ModLoader::ModProfile *profile, const bool bInSubFolder = false)
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
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			if (fd.cFileName[0] != '.' && fd.cFileName[1] != '.')
			{
				auto file = new ModLoader::ModProfile::File();
				if (file)
				{
					char path[MAX_PATH];

					sprintf(path, "%s\\%s", directory, fd.cFileName);

					file->File::File(GetLongFromLargeInteger(fd.nFileSizeLow, fd.nFileSizeHigh), path);
					file->m_bInSubFolder = bInSubFolder;

					files.push_back(file);
					// LOGINFO("%s -> %s [%s]", profile->m_name.c_str(), path, Utils::getProperSize(file->m_nSize).c_str();

					profile->m_nTotalSize += file->m_nSize;
				}
			}
		}
		else
		{
			if (fd.cFileName[0] != '.' && fd.cFileName[1] != '.')
			{
				char directoryPath[MAX_PATH];

				sprintf(directoryPath, "%s\\%s", directory, fd.cFileName);
				findFiles(directoryPath, files, profile, true);
			}
		}

	} while (FindNextFileA(hFind, &fd) != 0);

	FindClose(hFind);
}

void ModLoader::startup()
{
	Load();

	openProfiles(getModFolder().c_str());

	if (Profiles.m_nSize)
		SortProfiles();

	int enabledMods = 0;
	for (auto& profile : Profiles)
	{
		LOGINFO("%s -> Startup", profile->m_name.c_str());
		profile->Startup();
		if (profile->m_bEnabled) enabledMods++;
	}
	openScripts();
	LOGINFO("%d mod profiles loaded!", Profiles.m_nSize);
	LOGINFO("Only %d was enabled", enabledMods);

	bInit = true;
}

void ModLoader::SortProfiles()
{
	for (int i = 0; i < Profiles.m_nSize - 1; i++)
	{
		for (int j = 0; j < Profiles.m_nSize - i - 1; j++)
		{
			auto arr = Profiles.m_pBegin;
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

Utils::String ModLoader::getModFolder()
{
	auto res = Utils::String(path) + "\\mods\\";
	return res;
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
			m_files.m_pBegin = (File**)malloc(sizeof(File*) * 1024);
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

		if (auto file = FindFile("mod.ini"); file)
		{
			m_ModInfo = new ModExtraInfo();

			if (m_ModInfo)
				m_ModInfo->load(file);
		}

		auto newArray = (File**)malloc(sizeof(File*) * m_files.m_nSize);

		if (newArray)
		{
			for (int i = 0; i < m_files.m_nSize; i++)
				newArray[i] = m_files.m_pBegin[i];

			free(m_files.m_pBegin);

			m_files.m_pBegin = newArray;
			m_files.m_nCapacity = m_files.m_nSize;
		}

		if (m_bEnabled && m_ModInfo && m_ModInfo->m_pDLLs)
		{
			for (const auto& dll : *m_ModInfo->m_pDLLs)
			{
				auto file = FindFile(dll.c_str());

				if (file)
				{
					if (LoadLibraryA(file->m_path))
						LOGINFO("%s -> LoadLibrary(%s) successful", m_name, dll.c_str());
					else
						LOGINFO("%s -> Failed to load %s", m_name, dll.c_str());
				}
			}
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
	findFiles(getMyPath().c_str(), m_files, this, false);
}

void ModLoader::ModProfile::Save()
{
	if (!ModLoader::bInit)
		return;

	LOGINFO("Saving %s...", this->m_name.c_str());
	IniReader prof(getModFolder() + "\\profiles.ini");

	prof.WriteBool(this->m_name, "Enabled", this->m_bEnabled);
	prof.WriteInt(this->m_name, "Priority", this->m_nPriority);

	fprintf(profileFile, "\n"); // at least not stacking up on each other
	fflush(profileFile);
}