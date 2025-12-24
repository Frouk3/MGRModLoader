#include "ModLoader.h"
#include "json.hpp"

Utils::String ModLoader::ModProfile::GetMyPath()
{
	return GetModFolder() / m_name / "";
}

Utils::String ModLoader::GetModFolder()
{
	return Utils::String(ModLoaderPath) / "mods";
}

void ModLoader::Startup()
{
	if (bInit)
		return;

	GetModuleFileName(nullptr, ModLoaderPath, MAX_PATH);
	if (char* lastSlash = strrchr(ModLoaderPath, '\\'))
		*lastSlash = 0;

	Load();

	if (!bLoadMods)
	{
		LOGWARNING("Mod Loader is disabled. Cannot continue.");
		bInit = true;
		return;
	}

	ReadProfiles();

	if (Profiles.empty())
	{
		LOGWARNING("There's no mods available, skipping...");
		bInit = true;
		return;
	}

	SortProfiles();

	if (!Profiles.empty())
	{
		Profiles[0]->m_place = 0;
		for (size_t i = 1; i < Profiles.getSize(); i++)
			Profiles[i]->m_place = Profiles[i - 1]->m_place + 1;

		SortProfiles();

		LOGINFO("Profile startup...");
		for (auto& profile : Profiles)
		{
			if (!profile->m_bStarted && !(bSaveRAM && !profile->m_bEnabled))
			{
				LOGINFO("[%s] %s -> Startup", profile->m_bEnabled ? "+" : "-", profile->m_name.c_str());
				profile->Startup();
				if (profile->m_root.empty())
					LOGWARNING("%s is an empty profile, remove the folder from the mods folder. Location: %s", profile->m_name.c_str(), profile->m_root.m_path.c_str());
			}
		}
		if (bSaveRAM)
			LOGINFO("Some disabled mods were not loaded into the game to save the RAM. Disable it if you don't want to mercy RAM of the game.");

		LOGINFO("Profile startup done.");
	}
	else
	{
		LOGWARNING("There's no profiles available.");
	}

	OpenScripts();

	bInit = true;
}

void ModLoader::Shutdown()
{
	LOGINFO("Shutting down.");
	LOGINFO("Saving before shutdown...");
	Save(true);

	if (!bInit || !bLoadMods)
	{
		LOGINFO("Shutdown complete.");
		return;
	}

	for (auto& profile : Profiles)
	{
		if (profile->m_bStarted)
			profile->Shutdown();

		delete profile;
		profile = nullptr;
	}

	Profiles.clear();

	LOGINFO("Shutdown complete.");
}

void ModLoader::Save(bool bSilent)
{
	IniReader ini("MGRModLoaderSettings.ini");

	ini.WriteBool("ModLoader", "LoadMods", bLoadMods);
	ini.WriteBool("ModLoader", "LoadScripts", bLoadScripts);
	ini.WriteBool("ModLoader", "LoadFiles", bLoadFiles);
	ini.WriteBool("ModLoader", "SaveRAM", bSaveRAM);

	if (!bInit || !bLoadMods)
		return;

	remove((GetModFolder() / "profiles.ini").c_str()); // Delete old profiles.ini to not mess up with old profiles or new ones

	ini = IniReader((GetModFolder() / "profiles.ini").c_str());
	FILE* iniFile = fopen((GetModFolder() / "profiles.ini").c_str(), "a");

	if (!bSilent) LOGINFO("Saving profiles...");

	for (auto& profile : Profiles)
	{
		if (!bSilent) LOGINFO("Saving %s...", profile->m_name.c_str());

		profile->Save(ini);

		fprintf(iniFile, "\n");
		fflush(iniFile);
	}

	if (!bSilent) LOGINFO("Saving complete.");

	fclose(iniFile);
}

void ModLoader::Load()
{
	IniReader ini("MGRModLoaderSettings.ini");

	bLoadMods = ini.ReadBool("ModLoader", "LoadMods", bLoadMods);
	bLoadScripts = ini.ReadBool("ModLoader", "LoadScripts", bLoadScripts);
	bLoadFiles = ini.ReadBool("ModLoader", "LoadFiles", bLoadFiles);
	bSaveRAM = ini.ReadBool("ModLoader", "SaveRAM", bSaveRAM);
}

void ModLoader::ReadProfiles()
{
	LOGINFO("Reading profiles...");
	IniReader ini((GetModFolder() / "profiles.ini").c_str());
	FileSystem::DirectoryWalk([&](FileSystem::Directory& dir) -> void
		{
			ModProfile* profile = new ModProfile();

			profile->m_name = dir.getName();

			profile->m_bEnabled = true;
			profile->m_bRMMMod = false;
			profile->m_bStarted = false;

			profile->m_root.m_path = dir.m_path;

			profile->Load(ini);

			LOGINFO("Found profile %s(%s, %d)", profile->m_name.c_str(), profile->m_bEnabled ? "enabled" : "disabled", profile->m_place);

			Profiles.pushBack(profile);
		}, GetModFolder().c_str());
	LOGINFO("Reading profiles done.");
}

void ModLoader::OpenScripts()
{
	if (!bLoadScripts)
		return;

	char origPath[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, origPath);

	LOGINFO("Loading scripts...");
	for (auto& profile : Profiles)
	{
		if (!profile->m_bEnabled)
			continue;

		profile->FileWalk([&](FileSystem::File& file) -> void
			{
				if (file.m_bInSubFolder)
					return;

				if (!file.m_filesize)
					return;

				if (const char* ext = strrchr(file.getName(), '.'); ext && Utils::strlow(ext) == ".asi")
				{
					const char* name = file.getName();

					LOGINFO("Loading %s...", name);
					HMODULE hm = LoadLibrary(file.m_path.c_str());
					if (!hm)
					{
						LOGERROR("Failed to load %s(errno = %d)", name, errno);
						return;
					}

					if (void(*init)() = (void(*)())GetProcAddress(hm, "InitializeASI"); init)
						init();

					SetCurrentDirectory(origPath);
				}
			});

	}
	LOGINFO("Scripts loaded.");
}

void ModLoader::SortProfiles()
{
	Profiles.quickSort([](ModLoader::ModProfile** a, ModLoader::ModProfile** b) -> bool const
		{
			return (*a)->m_place < (*b)->m_place;
		});
}

void ModLoader::ModExtraInfo::Load(FileSystem::File* modFile)
{
	if (!modFile || !modFile->m_filesize)
		return;

	IniReader ini(modFile->m_path.c_str());

	m_author = ini.ReadString("Desc", "Author", std::string("")).c_str();
	m_title = ini.ReadString("Desc", "Title", std::string("")).c_str();
	m_version = ini.ReadString("Desc", "Version", std::string("")).c_str();
	m_description = ini.ReadString("Desc", "Description", std::string("")).c_str();
	m_date = ini.ReadString("Desc", "Date", std::string("")).c_str();
	m_authorURL = ini.ReadString("Desc", "AuthorURL", std::string("")).c_str();

	m_UpdateServer = ini.ReadString("Main", "UpdateServer", std::string("")).c_str();
	m_SaveFile = ini.ReadString("Main", "SaveFile", std::string("")).c_str();
	m_ID = ini.ReadString("Main", "ID", std::string("")).c_str();
	m_IncludeDirCount = ini.ReadInt("Main", "IncludeDirCount", 0);
	m_DependsCount = ini.ReadInt("Main", "DependsCount", 0);
	m_DLLFile = ini.ReadString("Main", "DLLFile", std::string("")).c_str();
	m_CodeFile = ini.ReadString("Main", "CodeFile", std::string("")).c_str();
	m_ConfigSchemaFile = ini.ReadString("Main", "ConfigSchemaFile", std::string("")).c_str();

	if (!m_ConfigSchemaFile.empty())
	{
		Utils::String path = modFile->m_path;

		*path.strrchr('\\') = 0;

		path = path / m_ConfigSchemaFile;

		if (FileSystem::PathExists(path.c_str()))
		{
			LOGINFO("Config path %s", path.c_str());

			m_ConfigSchema = new ConfigSchema();

			m_ConfigSchema->Load(path);
		}
	}

	if (m_IncludeDirCount)
	{
		for (int i = 0; i < m_IncludeDirCount; i++)
		{
			Utils::String dir = ini.ReadString("Main", Utils::format("IncludeDir%d", i).c_str(), std::string("")).c_str();
			if (!dir.empty())
				m_Dirs.push_back(dir);
		}
	}

	if (!m_DLLFile.empty())
	{
		const char* begin = m_DLLFile.c_str();
		const char* end = begin + m_DLLFile.length();

		while (begin < end)
		{
			const char* next = strchr(begin, ',');
			if (!next)
				next = end;

			while (isspace(*begin))
				begin++;

			m_DLLs.push_back(Utils::String(begin, next - begin));
			begin = next + 1;
		}
	}

	if (m_ID.empty())
	{
		m_ID.reserve(8);
		m_ID.format("%X", Utils::strhash(m_title));

		m_ID.resize();
		m_ID.shrink_to_fit();
	}
}

void ModLoader::ModExtraInfo::Save(FileSystem::File *modFile)
{
	if (!modFile || !modFile->m_filesize)
		return;

	if (!m_DLLs.empty())
	{
		m_DLLFile = "\"";

		for (size_t i = 0; i < m_DLLs.size(); i++)
		{
			if (i < m_DLLs.size() - 1)
				m_DLLFile += m_DLLs[i] + ", ";
			else
				m_DLLFile += m_DLLs[i];
		}

		m_DLLFile += "\"";
	}
	else
	{
		m_DLLFile = "\"\"";
	}

	IniReader ini(modFile->m_path.c_str());

	ini.WriteString("Desc", "Author", m_author.c_str());
	ini.WriteString("Desc", "Title", m_title.c_str());
	ini.WriteString("Desc", "Version", m_version.c_str());
	ini.WriteString("Desc", "Description", m_description.c_str());
	ini.WriteString("Desc", "Date", m_date.c_str());
	ini.WriteString("Desc", "AuthorURL", m_authorURL.c_str());

	ini.WriteString("Main", "UpdateServer", m_UpdateServer.c_str());
	ini.WriteString("Main", "SaveFile", m_SaveFile.c_str());
	ini.WriteString("Main", "ID", m_ID.c_str());
	ini.WriteInt("Main", "IncludeDirCount", m_Dirs.size());
	if (!m_Dirs.empty())
	{
		for (size_t i = 0; i < m_Dirs.size(); i++)
		{
			Utils::String dirString = Utils::String("\"") + m_Dirs[i] + "\"";
			ini.WriteString("Main", Utils::format("IncludeDir%d", i).c_str(), dirString.c_str());
		}
	}
	ini.WriteInt("Main", "DependsCount", m_DependsCount);
	ini.WriteString("Main", "DLLFile", m_DLLFile.c_str());
	ini.WriteString("Main", "CodeFile", m_CodeFile.c_str());
	ini.WriteString("Main", "ConfigSchemaFile", m_ConfigSchemaFile.c_str());
}

FileSystem::File* ModLoader::ModProfile::FindFile(const Utils::String& filename)
{
	if (m_ModInfo)
	{
		for (auto& dir : m_ModInfo->m_BoundDirectories)
			if (FileSystem::File* file = dir->FindFile(filename); file)
				return file;
	}

	return m_root.FindFile(filename);
}

FileSystem::Directory* ModLoader::ModProfile::FindDirectory(const Utils::String& path)
{
	return m_root.FindSubDir(path);
}

void ModLoader::ModProfile::FileWalk(const std::function<void(FileSystem::File&)>& cb)
{
	m_root.FileWalk(cb);
}

void ModLoader::ModProfile::Startup()
{
	if (m_bStarted)
		return;

	FileSystem::File* modIni = nullptr;

	FileSystem::FileWalk([&](FileSystem::File& file) -> void
		{
			if (modIni)
				return;

			if (file.m_bInSubFolder)
				return;

			if (!file.m_filesize)
				return;

			if (!strcmp(file.getName(), "mod.ini"))
				modIni = new FileSystem::File(file);
		}, m_root.m_path.c_str());

	if (modIni)
	{
		m_bRMMMod = true;
		m_ModInfo = new ModExtraInfo();
		m_ModInfo->Load(modIni);
	}

	if (m_bRMMMod && m_ModInfo)
	{
		m_root.scanFiles(false, false, 3);
		if (!m_ModInfo->m_DLLs.empty())
		{
			for (auto& dll : m_ModInfo->m_DLLs)
			{
				if (dll.empty() || !FileSystem::PathExists((GetMyPath() / dll).c_str()))
					continue;

				LOGINFO("Loading %s...", dll.c_str());
				HMODULE hm = LoadLibrary(dll.c_str());

				if (!hm)
				{
					int err = errno;
					if (err)
						LOGERROR("Failed to load %s(errno = %d)", dll.c_str(), err);
					else
						LOGERROR("Failed to load %s(GetLastError = %d)", dll.c_str(), GetLastError());
					return;
				}

				if (void(*init)() = (void(*)())GetProcAddress(hm, "InitializeASI"); init)
					init();
			}
		}

		if (!m_ModInfo->m_Dirs.empty())
		{
			std::vector<Utils::String> includePath;

			auto BindDirectory = [&](const Utils::String &dir, char** nameAddr, size_t bytesToSkip, size_t maxAmount)
				{
					for (size_t i = 0; i < maxAmount; i++)
					{
						Utils::String name = *(nameAddr + i * bytesToSkip);

						if (name.empty())
							continue;

						if (char* ext = name.strrchr('.'); ext)
							*ext = 0;

						if (char *slash = name.strchr('\\'); slash)
							name = slash + 1;

						if (dir == ".")
						{
							if (FileSystem::PathExists((GetMyPath() / name).c_str()))
								includePath.push_back(name);
						}
						else
						{
							if (FileSystem::PathExists((GetMyPath() / dir / name).c_str()))
								includePath.push_back(dir / name.c_str());
						}
					}
				};

			for (auto& dir : m_ModInfo->m_Dirs)
			{
				if (dir.empty())
					continue;

				if (FileSystem::Directory* Dir = m_root.FindSubDir(dir.c_str()))
					Dir->scanFiles(false, false, 2);

				BindDirectory(dir, (char**)(shared::base + 0x148DB38), 3, 6);
				BindDirectory(dir, (char**)(shared::base + 0x148F460), 3, 8);
				BindDirectory(dir, (char**)(shared::base + 0x148DB80), 2, 11);
			}

			for (auto& dir : includePath)
			{
				// LOGINFO("Trying to bind %s", dir.c_str());
				FileSystem::Directory* pDir = m_root.FindSubDir(dir.c_str());
				std::function<void(FileSystem::Directory*)> bindDir = [&](FileSystem::Directory* dir) -> void
					{
						for (auto& subdir : dir->m_subdirs)
							bindDir(subdir);

						dir->scanFiles(true, false, 3);
					};

				if (pDir)
				{
					bindDir(pDir);
					LOGINFO("%s: Bound %s", m_name.c_str(), dir.c_str());

					m_ModInfo->m_BoundDirectories.push_back(pDir);
					// pDir->Dump("");
				}
			}
		}
		m_root.calculateDirectorySize();
	}
	else if (!m_bRMMMod)
	{
		m_root.scanFiles(true, false, 3);
		m_root.calculateDirectorySize();
	}

	m_bStarted = true;

	delete(modIni);
}

void ModLoader::ModProfile::Shutdown()
{

}

void ModLoader::ModProfile::ScanFiles()
{
	m_root.scanFiles(false, false, 3);
	m_root.calculateDirectorySize();
}

void ModLoader::ModProfile::Save(IniReader &ini)
{
	ini.WriteInteger(m_name.c_str(), "MyPlace", m_place);
	ini.WriteBool(m_name.c_str(), "Enabled", m_bEnabled);
}

void ModLoader::ModProfile::Load(IniReader &ini)
{
	m_place = ini[m_name.c_str()]["MyPlace"]; // m_place is zero, so there's no need to call full function
	m_bEnabled = ini.ReadBool(m_name.c_str(), "Enabled", m_bEnabled);
}

void ModLoader::ModProfile::Cleanup()
{
	if (m_ModInfo)
	{
		delete m_ModInfo;
		m_ModInfo = nullptr;
	}
	m_bStarted = false;
	m_root.clear();
}

void ModLoader::ModProfile::Restart()
{

}

void ModLoader::ModExtraInfo::ConfigSchema::Load(const Utils::String& path)
{
	using namespace nlohmann;

	FILE* file = fopen(path.c_str(), "rb");

	if (!file)
		return;

	json j = json::parse(file, nullptr, false, true);

	if (j.is_object())
	{
		for (const auto& group : j["Groups"])
		{
			ConfigSchema::Group grp;

			grp.m_name = group["Name"].get<std::string>().c_str();
			grp.m_displayname = group["DisplayName"].get<std::string>().c_str();

			for (const auto& element : group["Elements"])
			{
				ConfigSchema::Group::Element elem;

				elem.m_name = element["Name"].get<std::string>().c_str();
				elem.m_displayname = element["DisplayName"].get<std::string>().c_str();
				elem.m_description = "";
				for (const auto& descTable : element["Description"])
				{
					elem.m_description += descTable.get<std::string>().c_str();
				}
				elem.m_type = element["Type"].get<std::string>().c_str();
				elem.m_MinValue = element["MinValue"].is_null() ? "" : element["MinValue"].get<std::string>().c_str();
				elem.m_MaxValue = element["MaxValue"].is_null() ? "" : element["MaxValue"].get<std::string>().c_str();
				elem.m_DefaultValue = element["DefaultValue"].get<std::string>().c_str();

				grp.m_elements.push_back(elem);
			}

			m_groups.push_back(grp);
		}

		for (const auto& item : j["Enums"].items())
		{
			Utils::String eName = item.key().c_str();

			for (const auto& enm : item.value())
			{
				ConfigSchema::Enum e;

				e.m_TypeIdentifier = eName;
				e.m_DisplayName = enm["DisplayName"].get<std::string>().c_str();
				e.m_Value = enm["Value"].get<std::string>().c_str();
				e.m_Description = "";
				for (const auto& descTable : enm["Description"])
				{
					e.m_Description += descTable.get<std::string>().c_str();
				}

				m_enums.push_back(e);
			}
		}

		m_IniFile = j["IniFile"].get<std::string>().c_str();
	}

	fclose(file);
}

void ModLoader::ModExtraInfo::ConfigSchema::Save(const Utils::String& path)
{
	// NOPE, WE AIN'T TOUCHIN' THAT
}