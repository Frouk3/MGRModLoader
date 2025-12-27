#include "ModLoader.h"
#include "json.hpp"

Utils::String ModLoader::GetModFolder()
{
	return Utils::String(ModLoaderPath) / "mods" / "";
}

void ModLoader::Startup()
{
	if (bInit)
		return;

	GetModuleFileNameA(nullptr, ModLoaderPath, MAX_PATH);
	char* lastSlash = strrchr(ModLoaderPath, '\\');
	if (lastSlash)
		*(lastSlash + 1) = '\0';

	LOGINFO("Mod Loader registered at path: %s", ModLoaderPath);
	Load();

	if (!bLoadMods)
	{
		LOGINFO("Cannot proceed, mods are forbidden to load.");

		bInit = true;

		return;
	}

	ReadProfiles();

	if (Profiles.empty()) LOGWARNING("No mods found in the mods directory.");
	else
	{
		SortProfiles();

		Profiles[0]->m_place = 0;
		for (int i = 1; i < Profiles.getSize(); i++)
			Profiles[i]->m_place = Profiles[i - 1]->m_place + 1;

		SortProfiles();

		LOGINFO("Mods startup...");

		for (ModProfile*& profile : Profiles)
		{
			if (!profile->m_bEnabled && bSaveRAM)
				continue;

			if (profile->m_bStarted)
				continue;

			LOGINFO("[%s] %s -> Startup", profile->m_bEnabled ? "+" : "-", profile->m_name.c_str());
			profile->Startup();

			if (profile->m_root.empty())
				LOGWARNING("%s seems to be empty, on accident or not?", profile->m_name.c_str());
		}

		if (bSaveRAM)
			LOGINFO("If you're looking for disabled mods, they were not loaded to save resources.");

		SortProfiles();

		if (bLoadScripts)
			OpenScripts();

		LOGINFO("Mods startup complete.");
	}

	bInit = true;
}

void ModLoader::Shutdown()
{
	LOGINFO("Shutting down...");

	if (!bInit || !bLoadMods)
	{
		LOGINFO("Shutdown complete.");
		return;
	}

	Save(true);

	for (ModProfile **profile = Profiles.begin(); profile != Profiles.end(); profile++)
	{
		if ((*profile)->m_bStarted)
			(*profile)->Shutdown();

		delete *profile;
		*profile = nullptr;
	}

	Profiles.clear();

	LOGINFO("Shutdown complete.");

	bInit = false;
}

void ModLoader::Save(bool bSilent)
{
	if (!bSilent) LOGINFO("Saving...");

	IniReader ini("MGRModLoaderSettings.ini");

	ini.WriteBool("ModLoader", "LoadMods", bLoadMods);
	ini.WriteBool("ModLoader", "LoadScripts", bLoadScripts);
	ini.WriteBool("ModLoader", "LoadFiles", bLoadFiles);
	ini.WriteBool("ModLoader", "SaveRAM", bSaveRAM);

	remove((GetModFolder() / "profiles.ini").c_str()); // Remove old profiles file, resolves issues with deleted mods

	IniReader profilesIni((GetModFolder() / "profiles.ini").c_str());
	FILE* f = nullptr;
	f = fopen((GetModFolder() / "profiles.ini").c_str(), "a");

	int profPlace = 0;

	for (ModProfile*& profile : ModLoader::Profiles)
	{
		if (!bSilent) LOGINFO("Saving %s...", profile->m_name.c_str());
		profile->m_place = profPlace++;

		profile->Save(profilesIni);

		if (f)
		{
			fprintf(f, "\n");
			fflush(f);
		}
	}

	if (f) fclose(f);

	if (!bSilent) LOGINFO("Save complete.");
}

void ModLoader::Load()
{
	LOGINFO("Loading...");
	IniReader ini("MGRModLoaderSettings.ini");

	bLoadMods = ini.ReadBool("ModLoader", "LoadMods", bLoadMods);
	bLoadScripts = ini.ReadBool("ModLoader", "LoadScripts", bLoadScripts);
	bLoadFiles = ini.ReadBool("ModLoader", "LoadFiles", bLoadFiles);
	bSaveRAM = ini.ReadBool("ModLoader", "SaveRAM", bSaveRAM);

	if (!bLoadMods)
	{
		LOGINFO("Mod loading is disabled.");
		LOGINFO("Load complete.");

		return;
	}

	IniReader profilesIni((GetModFolder() / "profiles.ini").c_str());

	for (ModProfile*& profile : ModLoader::Profiles)
	{
		if (profile)
			profile->Load(profilesIni);
	}

	LOGINFO("Load complete.");

}

void ModLoader::ReadProfiles()
{
	LOGINFO("Reading profiles...");

	IniReader profilesIni((GetModFolder() / "profiles.ini").c_str());

	FileSystem::DirectoryWalk([&](FileSystem::Directory& dir) -> void
		{
			ModProfile* profile = new ModProfile();

			profile->m_bEnabled = true;
			profile->m_bStarted = false;

			profile->m_name = dir.getName();
			profile->m_root.m_path = dir.m_path;
			
			profile->Load(profilesIni);

			LOGINFO("Found mod profile: %s (%s, %d)", profile->m_name.c_str(), profile->m_bEnabled ? "enabled" : "disabled", profile->m_place);

			Profiles.pushBack(profile);
		}, GetModFolder().c_str());

	LOGINFO("Reading profiles complete.");
}

void ModLoader::SortProfiles()
{
	Profiles.quickSort([](ModProfile** a, ModProfile** b) -> bool const
		{
			return (*a)->m_place < (*b)->m_place;
		});
}

void ModLoader::OpenScripts()
{
	if (!bLoadScripts)
		return;

	char origPath[MAX_PATH] = { 0 };
	GetCurrentDirectoryA(MAX_PATH, origPath);

	LOGINFO("Loading scripts...");

	for (ModProfile*& prof : Profiles)
	{
		if (!prof->m_bEnabled)
			continue;

		prof->FileWalk([&](FileSystem::File& file) -> void
			{
				if (file.m_bInSubFolder)
					return;

				if (!file.m_filesize)
					return;

				if (const char* ext = file.m_path.strrchr('.'); ext && !stricmp(ext, ".asi"))
				{
					const char* name = file.getName();

					Logger::Printf("[Mod Loader] [INFO] Loading script %s from %s...", name, prof->m_name.c_str());

					HMODULE hm = LoadLibraryA(file.m_path.c_str());
					if (!hm)
					{
						Logger::PrintfNoTime("[FAIL]\n");
						LOGERROR("Failed to load %s!(errno = %d, GetLastError = %lu)", name, errno, GetLastError());
						return;
					}
					else
					{
						Logger::PrintfNoTime("[ OK ]\n");
					}

					if (void(*init)() = (void(*)())GetProcAddress(hm, "InitializeASI"); init)
						init();

					SetCurrentDirectoryA(origPath);
				}
			});
	}

	SetCurrentDirectoryA(origPath); // just in case

	LOGINFO("Scripts loading complete.");
}

void ModLoader::ModExtraInfo::ConfigSchema::Load(const Utils::String& path)
{
	using namespace nlohmann;

	if (path.empty())
		return;

	if (!FileSystem::PathExists(path.c_str())) // make sure to check the file first, before opening it
		return;

	FILE* file = nullptr;
	fopen_s(&file, path.c_str(), "rb");

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
	using namespace nlohmann;

	if (path.empty())
		return;

	json j;
	j["Groups"] = json::array();

	for (const auto& grp : m_groups)
	{
		json gj;
		gj["Name"] = std::string(grp.m_name.c_str());
		gj["DisplayName"] = std::string(grp.m_displayname.c_str());
		gj["Elements"] = json::array();

		for (const auto& elem : grp.m_elements)
		{
			json ej;
			ej["Name"] = std::string(elem.m_name.c_str());
			ej["DisplayName"] = std::string(elem.m_displayname.c_str());

			json desc = json::array();
			desc.push_back(std::string(elem.m_description.c_str()));
			ej["Description"] = desc;

			ej["Type"] = std::string(elem.m_type.c_str());

			if (elem.m_MinValue.empty())
				ej["MinValue"] = nullptr;
			else
				ej["MinValue"] = std::string(elem.m_MinValue.c_str());

			if (elem.m_MaxValue.empty())
				ej["MaxValue"] = nullptr;
			else
				ej["MaxValue"] = std::string(elem.m_MaxValue.c_str());

			ej["DefaultValue"] = std::string(elem.m_DefaultValue.c_str());

			gj["Elements"].push_back(ej);
		}

		j["Groups"].push_back(gj);
	}

	json enums = json::object();
	for (const auto& e : m_enums)
	{
		std::string key = e.m_TypeIdentifier.c_str();
		if (!enums.contains(key))
			enums[key] = json::array();

		json ej;
		ej["DisplayName"] = std::string(e.m_DisplayName.c_str());
		ej["Value"] = std::string(e.m_Value.c_str());

		json desc = json::array();
		desc.push_back(std::string(e.m_Description.c_str()));
		ej["Description"] = desc;

		enums[key].push_back(ej);
	}
	j["Enums"] = enums;

	j["IniFile"] = std::string(m_IniFile.c_str());

	FILE* file = nullptr;
	fopen_s(&file, path.c_str(), "wb");
	if (!file)
		return;

	std::string out = j.dump(4);
	fwrite(out.c_str(), 1, out.size(), file);
	fflush(file);
	fclose(file);
}

void ModLoader::ModExtraInfo::Load(FileSystem::File* modFile)
{
	if (!modFile)
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

		if (char* chr = path.strrchr('\\'))
			chr[1] = 0;

		path.resize();
		path /= m_ConfigSchemaFile;

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

void ModLoader::ModExtraInfo::Save(FileSystem::File* modFile)
{
	if (!modFile)
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

FileSystem::File *ModLoader::ModProfile::FindFile(const Utils::String& filename)
{
	if (m_ModInfo)
	{
		for (FileSystem::Directory* dir : m_ModInfo->m_BoundDirectories)
		{
			if (FileSystem::File* file = dir->FindFile(filename); file)
				return file;
		}
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

	if (FileSystem::PathExists((GetMyPath() / "mod.ini").c_str()))
	{
		m_bRMMMod = true;

		struct stat s;
		stat((GetMyPath() / "mod.ini").c_str(), &s);

		FileSystem::File modFile((GetMyPath() / "mod.ini").c_str(), (unsigned int)s.st_size);

		if (!m_ModInfo)
			m_ModInfo = new ModExtraInfo();

		m_ModInfo->Load(&modFile);
	}

	if (m_bRMMMod && m_ModInfo)
	{
		m_root.scanFiles(false, false, m_root.SCANFLAG_ALL);
		if (!m_ModInfo->m_DLLs.empty())
		{
			for (auto& dll : m_ModInfo->m_DLLs)
			{
				if (dll.empty())
					continue;

				if (!FileSystem::PathExists((GetMyPath() / dll).c_str()))
				{
					LOGERROR("DLL %s was set in DLL field, but was not found!", dll.c_str());
					continue;
				}

				LOGINFO("Loading %s...", dll.c_str());
				HMODULE hm = LoadLibrary(dll.c_str());

				if (!hm)
				{
					LOGERROR("Failed to load %s!(errno = %d, GetLastError = %lu)", dll.c_str(), errno, GetLastError());
					return;
				}

				if (void(*init)() = (void(*)())GetProcAddress(hm, "InitializeASI"); init) // I don't know why people put ASI's into DLL's, but whatever
					init();
			}
		}
		if (!m_ModInfo->m_Dirs.empty())
		{
			std::vector<Utils::String> includePath;

			auto BindDirectory = [&](const Utils::String& dir, char** nameAddr, size_t bytesToSkip, size_t maxAmount)
				{
					for (size_t i = 0; i < maxAmount; i++)
					{
						Utils::String name = *(nameAddr + i * bytesToSkip);

						if (name.empty())
							continue;

						if (char* ext = name.strrchr('.'); ext)
							*ext = 0;

						if (char* slash = name.strchr('\\'); slash)
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
					Dir->scanFiles(false, false, Dir->SCANFLAG_DIRECTORIES); // yeah, I'm lazy to write namespaces for using the flags

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

						dir->scanFiles(true, false, dir->SCANFLAG_ALL);
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
		m_root.scanFiles(true, false, m_root.SCANFLAG_ALL);
		m_root.calculateDirectorySize();
	}

	m_bStarted = true;
}

void ModLoader::ModProfile::Shutdown()
{
	if (m_ModInfo)
	{
		delete m_ModInfo;
		m_ModInfo = nullptr;
	}
	m_root.clear();

	m_bStarted = false;
}

void ModLoader::ModProfile::Save(IniReader& ini)
{
	ini.WriteBool(m_name.c_str(), "Enabled", m_bEnabled);
	ini.WriteInt(m_name.c_str(), "MyPlace", m_place);
}

void ModLoader::ModProfile::Load(IniReader& ini)
{
	m_bEnabled = ini.ReadBool(m_name.c_str(), "Enabled", m_bEnabled);
	m_place = ini.ReadInt(m_name.c_str(), "MyPlace", m_place);
}

void ModLoader::ModProfile::Restart()
{
	Shutdown();
	Startup();
}

Utils::String ModLoader::ModProfile::GetMyPath()
{
	return m_root.m_path;
}