#pragma once
#include "lib.h"
#include <string>
#include <common.h>
#include <format>
#include <Hw.h>
#include <functional>
#include <ini.h>
#include "Utils.h"
#pragma warning(disable : 4996)

#define MAX_MODS_PROFILE 1024

inline Hw::cHeapGlobal ModloaderHeap;

namespace FileSystem
{
	struct File
	{
		Utils::String m_path;
		unsigned int m_filesize; // 4GB will be enough
		bool m_bInSubFolder;

		File()
		{
			m_filesize = 0;
			m_bInSubFolder = false;
		}

		File(const char* path)
		{
			m_path = path;
			m_filesize = 0;
			m_bInSubFolder = false;
		}

		File(const char* path, unsigned int filesize)
		{
			m_path = path;
			m_filesize = filesize;
			m_bInSubFolder = false;
		}

		~File()
		{
			m_filesize = 0;
			m_bInSubFolder = false;
		}

		bool read(void* filedata);
		void* read(); // Unless we don't have any space for it
		const char* getName();
	};

	struct Directory
	{
		Utils::String m_path;
		UINT64 m_filesize;
		std::vector<File> m_files;
		std::vector<Directory*> m_subdirs; // to pointer since we have problems with further directories
		Directory* m_parent;

		Directory()
		{
			m_filesize = 0l;
			m_parent = nullptr;
		}

		Directory(const char* path)
		{
			m_path = path;
			m_filesize = 0l;
			m_parent = nullptr;
		};

		~Directory()
		{
			m_filesize = 0l;
			m_parent = nullptr;

			clear();
		}

		bool empty() const { return m_files.empty() && m_subdirs.empty(); }

		void FileWalk(const std::function<void(File&)>& cb);

		void clear();
		void calculateDirectorySize();
		const char* getName();
		void scanFiles(bool bRecursive = false, const bool bInSubFolder = false, unsigned int flags = 3);

		void Dump(const Utils::String& extPrintLn);

		File *FindFile(const Utils::String& filepath);
		Directory* FindSubDir(const Utils::String& path);
		Directory* FindSubDirRecursive(const std::vector<std::string>& pathParts, size_t index = 0);
	};

	void FileWalk(const std::function<void(File&)>& cb, const char *path); // Static function to go through all files in the path
	void FileWalkRecursive(const std::function<void(File&)>& cb, const char* path); // Static function to go through all files in the path and subfolders
	void DirectoryWalk(const std::function<void(Directory&)>& cb, const char* path); // Static function to go through all directories in the path
	void DirectoryWalkRecursive(const std::function<void(Directory&)>& cb, const char* path); // Static function to go through all directories in the path and subfolders

	bool PathExists(const char* path);
}

#define LOGGER_DEBUG 0

namespace Logger
{
	inline FILE* LogFile = nullptr;
	inline bool bFlushImmediately = false;
	inline bool bEnabled = true;
	inline char LogFilePath[MAX_PATH] = { 0 };
#if LOGGER_DEBUG
	inline lib::StaticArray<std::pair<Utils::String, float>, 64> LatestLog;
#endif

	void Init();

	void LoadConfig();
	void SaveConfig();

	void Open();
	void Close();
	void ReOpen();
	void SetPath(const char* path);

	void Flush();

	void vPrintf(const char* format, va_list args);
	void PrintfLn(const char* format, ...); // New line escape and the end
	void Printf(const char* format, ...); // Full control of the line + without time or anything related to "Mod Loader"
}

namespace Updater
{
	inline bool bEnabled = true;
	inline constexpr double fCurrentVersion = 2.8;
	inline double fLatestVersion = -1.0;
	inline HANDLE hUpdateThread;
	enum UpdateStatus : unsigned int
	{
		UPDATE_STATUS_NONE = 0,
		UPDATE_STATUS_FAILED,
		UPDATE_STATUS_AVAILABLE,
		UPDATE_STATUS_LATEST_INSTALLED,
		UPDATE_STATUS_UNEXPECTED // Hit the end without any status
	};

	inline UpdateStatus eUpdateStatus = UPDATE_STATUS_NONE;

	void Init();

	bool CheckForUpdates();
	bool CheckForOnce();
	void LoadConfig();
	void SaveConfig();
}

#define LOG(x, ...) \
	Logger::PrintfLn("[Mod Loader] " x, __VA_ARGS__)

#define LOGERROR(x, ...) \
	LOG("[ERROR] " x, __VA_ARGS__)

#define LOGINFO(x, ...) \
	LOG("[INFO] " x, __VA_ARGS__)

#define LOGWARNING(x, ...) \
	LOG("[WARNING] " x, __VA_ARGS__)

namespace ModLoader
{
	inline char ModLoaderPath[MAX_PATH] = { 0 };
	inline bool bInit = false;
	inline bool bLoadMods = true;
	inline bool bLoadScripts = true;
	inline bool bLoadFiles = true;

	Utils::String GetModFolder();
	void Startup();
	void Shutdown();
	void Save(bool bSilent = false);
	void Load();

	void ReadProfiles();
	void OpenScripts();
	void SortProfiles();

	struct ModExtraInfo
	{
		Utils::String m_author;
		Utils::String m_title;
		Utils::String m_version;
		Utils::String m_description;
		Utils::String m_date;
		Utils::String m_authorURL;

		Utils::String m_UpdateServer;
		Utils::String m_SaveFile;
		Utils::String m_ID;
		Utils::String m_DLLFile;
		Utils::String m_CodeFile;
		int m_IncludeDirCount;
		int m_DependsCount;
		std::vector<Utils::String> m_DLLs;
		std::vector<Utils::String> m_CodeFiles;
		std::vector<Utils::String> m_Dirs;
		Utils::String m_ConfigSchemaFile;

		std::vector<FileSystem::Directory *> m_BoundDirectories;

		struct ConfigSchema
		{
			struct Group
			{
				struct Element
				{
					Utils::String m_name;
					Utils::String m_displayname;
					Utils::String m_description;

					Utils::String m_type;
					Utils::String m_MinValue;
					Utils::String m_MaxValue;
					Utils::String m_DefaultValue;

					Element() = default;
					~Element() = default;
				};

				Utils::String m_name;
				Utils::String m_displayname;

				std::vector<Element> m_elements;

				Group() = default;
				
				~Group()
				{
					m_name.clear();
					m_displayname.clear();
					m_elements.clear();
				}
			};

			struct Enum
			{
				Utils::String m_TypeIdentifier;
				Utils::String m_DisplayName;
				Utils::String m_Value;
				Utils::String m_Description;

				Enum() = default;
				~Enum() = default;
			};

			std::vector<Group> m_groups;
			std::vector<Enum> m_enums;
			Utils::String m_IniFile;

			ConfigSchema() = default;

			~ConfigSchema()
			{
				m_groups.clear();
				m_enums.clear();
				m_IniFile.clear();
			}

			void Load(const Utils::String& path);
			void Save(const Utils::String& path);
		};

		ConfigSchema *m_ConfigSchema; // we need to make sure if we really have the scheme config

		ModExtraInfo() = default;
		
		~ModExtraInfo()
		{
			m_DLLs.clear();
			m_CodeFiles.clear();
			m_Dirs.clear();

			if (m_ConfigSchema)
			{
				delete m_ConfigSchema;
				m_ConfigSchema = nullptr;
			}
		}

		void Load(FileSystem::File *modFile);
		void Save(FileSystem::File *modFile);
	};

	struct ModProfile
	{
		Utils::String m_name;
		int m_place;
		FileSystem::Directory m_root;
		bool m_bEnabled;
		bool m_bRMMMod;
		bool m_bStarted;
		ModExtraInfo* m_ModInfo;

		ModProfile()
		{
			m_place = -1;
			m_bEnabled = true;
			m_bRMMMod = false;
			m_bStarted = false;

			m_ModInfo = nullptr;
		}

		~ModProfile()
		{
			Shutdown();

			m_place = -1;
			m_bEnabled = false;
			m_bRMMMod = false;
			m_bStarted = false;

			if (m_ModInfo)
			{
				delete m_ModInfo;
				m_ModInfo = nullptr;
			}
		}

		FileSystem::File* FindFile(const Utils::String& filename);
		FileSystem::Directory* FindDirectory(const Utils::String& path);
		void FileWalk(const std::function<void(FileSystem::File&)>& cb);

		void Startup();
		void Shutdown();
		void ScanFiles();
		void Save(IniReader &ini);
		void Load(IniReader &ini);

		void Restart();

		Utils::String GetMyPath();
	};

	inline lib::StaticArray<ModProfile*, MAX_MODS_PROFILE> Profiles;
}