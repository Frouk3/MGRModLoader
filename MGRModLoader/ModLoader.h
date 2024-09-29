#pragma once
#include "lib.h"
#include <string>
#include <common.h>
#include <format>
#include <Hw.h>
#include <functional>
#include <ini.h>
#pragma warning(disable : 4996)

#define MAX_MODS_PROFILE 1024

extern inline void __cdecl dbgPrint(const char* fmt, ...);

#define LOG(x, ...) \
	dbgPrint("[Mod Loader] " x, __VA_ARGS__)

#define LOGERROR(x, ...) \
	LOG("[ERROR] " x, __VA_ARGS__)

#define LOGINFO(x, ...) \
	LOG("[INFO ] " x, __VA_ARGS__)

namespace Utils
{
	class String
	{
	private:
		char* m_string;
		size_t m_nLength;

		void copyFrom(const char* str)
		{
			deallocateString();

			if (str)
			{
				m_nLength = strlen(str);
				m_string = new char[m_nLength + 1];
				memcpy(m_string, str, m_nLength + 1);
			}
			else
			{
				m_string = nullptr;
				m_nLength = 0;
			}
		}

		void deallocateString()
		{
			delete[] m_string;
			m_string = nullptr;
			m_nLength = 0;
		}

	public:
		String()
			: m_string(nullptr), m_nLength(0)
		{}

		String(const char* string)
			: m_string(nullptr), m_nLength(0)
		{
			copyFrom(string);
		}

		String(const String& other)
			: m_string(nullptr), m_nLength(0)
		{
			copyFrom(other.m_string);
		}

		String(String&& other) noexcept
			: m_string(other.m_string), m_nLength(other.m_nLength)
		{
			other.m_string = nullptr;
			other.m_nLength = 0;
		}

		~String()
		{
			deallocateString();
		}

		String& operator=(const String& other)
		{
			if (this != &other)
			{
				deallocateString();
				copyFrom(other.m_string);
			}
			return *this;
		}

		String& operator=(String&& other) noexcept
		{
			if (this != &other)
			{
				deallocateString();
				m_string = other.m_string;
				m_nLength = other.m_nLength;
				other.m_string = nullptr;
				other.m_nLength = 0;
			}
			return *this;
		}

		String& operator=(const char* str)
		{
			copyFrom(str);
			return *this;
		}

		String& operator+=(const char* str)
		{
			if (str)
			{
				size_t newLength = m_nLength + strlen(str);
				char* newString = new char[newLength + 1];

				if (m_string)
				{
					memcpy(newString, m_string, m_nLength);
				}

				memcpy(newString + m_nLength, str, strlen(str) + 1);

				delete[] m_string;
				m_string = newString;
				m_nLength = newLength;
			}
			return *this;
		}

		String operator+(const char* str) const
		{
			String result = *this;
			result += str;
			return result;
		}

		operator bool()
		{
			return m_nLength && m_string;
		}

		bool operator==(const String& other) const
		{
			return m_nLength == other.m_nLength && strcmp(m_string, other.m_string) == 0;
		}

		bool operator!=(const String& other) const
		{
			return !(*this == other);
		}

		char& operator[](size_t index)
		{
			return m_string[index];
		}

		const char& operator[](size_t index) const
		{
			return m_string[index];
		}

		size_t length() const
		{
			return m_nLength;
		}

		int format(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);

			char buffer[1024];

			int length = vsnprintf(buffer, 1024, fmt, args);
			va_end(args);

			*this = buffer;
			return length;
		}

		char* data()
		{
			return m_string;
		}

		void resize()
		{
			m_nLength = strlen(m_string);
		}

		const char* c_str() const
		{
			return m_string ? m_string : "";
		}

		operator const char* ()
		{
			return c_str();
		}
	};

	inline char* formatPath(char* buffer)
	{
		while (auto chr = strchr(buffer, '/'))
			*chr = '\\';

		return buffer;
	}

	inline char* formatPath(const char* buffer)
	{
		static char buff[MAX_PATH];
		memset(buff, 0, MAX_PATH);

		strcpy(buff, buffer);

		while (auto chr = strchr(buff, '/'))
			*chr = '\\';

		return buff;
	}

	/// <summary>
	/// Game implementation of format path
	/// </summary>
	/// <param name="buffer">Pointer to buffer</param>
	/// <param name="bufferSize">Size of buffer</param>
	/// <param name="data">Contains data about path</param>
	inline void formatPathGI(char* buffer, size_t bufferSize, const char* data)
	{
		((void(__cdecl*)(char*, size_t, const char*))(shared::base + 0x9F8090))(buffer, bufferSize, data);
	}

	inline String getProperSize(uint64_t fileSize)
	{
		String format;

		// do double since we don't know where the float will not display accurately

		auto sizeKB = (double)fileSize / 1024.0;
		auto sizeMB = sizeKB / 1024.0;
		auto sizeGB = float(sizeMB / 1024.0); // I don't think that it will ever reach 256GB or so on

		format.format("%.1f%s", (double)fileSize > 1024.0 ? ((sizeKB > 1024.0) ? (sizeMB > 1024.0 ? sizeGB : sizeMB) : sizeKB) : fileSize, (double)fileSize > 1024.0 ? ((sizeKB > 1024.0) ? (sizeMB > 1024.0 ? "GB" : "MB") : "KB") : " Bytes");

		return format;
	}

	inline char* strlow(const char* buffer)
	{
		static char buff[MAX_PATH];

		memset(buff, 0, MAX_PATH);

		strcpy(buff, buffer); // Make a copy of string so we don't modify the buffer

		for (int i = strlen(buff); i > 0; i--)
			buff[i] = tolower(buff[i]);

		return buff;
	}

	inline char* strlow(char* buffer)
	{
		for (int i = strlen(buffer); i > 0; i--)
			buffer[i] = tolower(buffer[i]); // here we modify the buffer

		return buffer;
	}
}

namespace ModLoader
{
	inline char path[MAX_PATH];
	inline bool bInitFailed = false;
	inline bool bInit = false;
	inline bool bIgnoreScripts = false;
	inline bool bIgnoreDATLoad = false;
	inline bool bEnableLogging = true;

	void startup();
	void SortProfiles();
	void Load();
	void Save();
	Utils::String getModFolder();

	struct ModProfile
	{
		struct File;
		struct ModExtraInfo; // RMM or just has mod.ini in it

		Utils::String m_name;
		int m_nPriority;
		bool m_bEnabled;
		bool m_bStarted;
		Hw::cFixedVector<struct File*> m_files;
		uint64_t m_nTotalSize;
		ModExtraInfo *m_ModInfo;

		ModProfile()
		{
			this->m_nPriority = 7; // def
			this->m_bEnabled = true;
			this->m_bStarted = false;
			this->m_nTotalSize = 0;

			m_ModInfo = nullptr;
		}

		ModProfile(const char* szName) : m_name(szName)
		{
			this->m_nPriority = 7;
			this->m_bEnabled = true;
			this->m_bStarted = false;
			this->m_nTotalSize = 0;

			m_ModInfo = nullptr;
		}

		~ModProfile()
		{
			this->m_nPriority = -1;
			this->m_bEnabled = false;

			Shutdown();
		}

		void Shutdown()
		{
			if (m_files.m_pBegin)
				free(m_files.m_pBegin);

			m_files.m_pBegin = nullptr;
			m_files.m_nSize = 0;
			m_files.m_nCapacity = 0;

			m_nTotalSize = 0;

			m_bStarted = false;
		}

		struct File
		{
			uint64_t m_nSize;
			char* m_path;
			bool m_bInSubFolder;

			File()
			{
				m_nSize = 0;
				m_path = nullptr;
				m_bInSubFolder = false;
			}

			File(uint64_t size, const char* path) : m_nSize(size)
			{
				m_path = nullptr;

				SetFilePath(path);
			}

			File(uint64_t size, char* path) : m_nSize(size)
			{
				m_path = nullptr;

				SetFilePath(path);
			}

			void SetFilePath(const char* path)
			{
				if (m_path)
				{
					free(m_path);
					m_path = nullptr;
				}

				m_path = (char*)malloc(strlen(path) + 1);

				if (m_path)
					strcpy(m_path, path);

				Utils::formatPath(m_path);
			}

			int read(void *datafile)
			{
				if (m_nSize)
				{
					auto file = fopen(m_path, "rb");

					fseek(file, 0, SEEK_SET);

					bool bReadSuccessfully = fread(datafile, 1u, m_nSize, file) <= m_nSize;

					fclose(file);

					return bReadSuccessfully;
				}

				return 0;
			}
		};

		struct ModExtraInfo
		{
			Utils::String m_author;
			Utils::String m_title;
			Utils::String m_version;
			Utils::String m_description;
			Utils::String m_date;
			Utils::String m_authorURL;

			std::vector<Utils::String>* m_pDLLs = nullptr;

			ModExtraInfo()
			{
				
			}

			void load(File* file)
			{
				if (file->m_nSize)
				{
					IniReader ini(file->m_path);

					m_author = ini.ReadString("Desc", "Author", nullptr);
					m_title = ini.ReadString("Desc", "Title", nullptr);
					m_version = ini.ReadString("Desc", "Version", nullptr);
					m_description = ini.ReadString("Desc", "Description", nullptr);
					m_date = ini.ReadString("Desc", "Date", nullptr);
					m_authorURL = ini.ReadString("Desc", "AuthorURL", nullptr);

					Utils::String dlls = ini.ReadString("Main", "DLLFile", nullptr);

					if (dlls && dlls.length() > 1)
					{
						m_pDLLs = new std::vector<Utils::String>();

						for (int i = 0; i < dlls.length(); i++)
						{
							if (dlls.data()[i] == '\0')
								continue;

							if (isspace(dlls.data()[i])) // move it
							{
								for (int j = i; j < dlls.length(); j++)
								{
									if (dlls.data()[j] == '\0')
										break;

									dlls.data()[j] = dlls.data()[j + 1];
								}
							}

							if (dlls.data()[i] == ',')
								dlls.data()[i] = '\0';
						}

						auto pos = dlls.data();

						while (true)
						{
							if (pos - dlls.data() > dlls.length() || pos[0] == '\0')
								break;

							m_pDLLs->push_back(Utils::formatPath(pos));

							pos += strlen(pos) + 1;
						}
					}
				}
			}
		};
		
		// Can find file, or any file with extension
		File* FindAnyFile(const char* filename)
		{
			if (!filename || filename[0] == '\0' || !strcmp(filename, ""))
				return nullptr;

			for (auto& file : m_files)
			{
				if (strstr(file->m_path, Utils::formatPath(filename)) 
					&& (strrchr(file->m_path, '\\') + 1)[0] != '.' && (strrchr(file->m_path, '\\') + 1)[1] != '.') // Skip the files with dots at the start
					return file;
			}
			return nullptr;
		}

		// Finds file by the end of the filename(like: pl\\pl0010.dat -> pathToGame\\mods\\ModName\\pl\\pl0010.dat)
		File* FindFile(const char* filename)
		{
			if (!filename || filename[0] == '\0' || !strcmp(filename, ""))
				return nullptr;

			for (auto& file : m_files)
			{
				if (!strcmp(&file->m_path[strlen(file->m_path) - strlen(filename)], Utils::formatPath(filename)) && strlen(strrchr(file->m_path, '\\') + 1) == strlen(strrchr(filename, '\\') ? strrchr(filename, '\\') + 1 : filename)
					&& (strrchr(file->m_path, '\\') + 1)[0] != '.' && (strrchr(file->m_path, '\\') + 1)[1] != '.') // Skip the files with dots at the start
					return file;
			}
			return nullptr;
		};

		// Go through each file and callback
		void FileWalk(const std::function<void(File*)>& cb)
		{
			for (auto& file : m_files)
				cb(file);
		}

		void Startup();
		void Restart();
		void ReadFiles();
		void Save();
		void Load(const char* name);

		Utils::String getMyPath()
		{
			return (getModFolder() + m_name);
		};

	};

	inline lib::StaticArray<ModProfile*, MAX_MODS_PROFILE> Profiles;
}