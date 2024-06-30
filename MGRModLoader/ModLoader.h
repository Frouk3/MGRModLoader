#pragma once
#include "lib.h"
#include <string>
#include <common.h>
#include <format>
#pragma warning(disable : 4996)

#define MAX_MODS_PROFILE 1024

extern inline void __cdecl dbgPrint(const char* fmt, ...);

#define LOG(x, ...) \
	dbgPrint("[Mod Loader] " x, __VA_ARGS__);

#define LOGERROR(x, ...) \
	LOG("[ERROR] " x, __VA_ARGS__)

#define LOGINFO(x, ...) \
	LOG("[INFO ] " x, __VA_ARGS__)

namespace ModLoader
{
	inline char path[MAX_PATH];
	inline bool bInitFailed = false;
	inline bool bInit = false;
	inline bool bIgnoreScripts = false;
	inline bool bIgnoreDATLoad = false;

	void startup();
	void SortProfiles();
	void Load();
	void Save();
	std::string getModFolder();

	struct ModProfile
	{
		char m_name[64];
		int m_nPriority;
		bool m_bEnabled;

		ModProfile()
		{
			this->m_name[0] = '\0';
			this->m_nPriority = 7; // def
			this->m_bEnabled = true;
		}

		ModProfile(const char* szName)
		{
			strcpy(this->m_name, szName);
			this->m_nPriority = 7;
			this->m_bEnabled = true;
		}

		~ModProfile()
		{
			this->m_name[0] = '\0';
			this->m_nPriority = -1;
			this->m_bEnabled = false;
		}

		void Save();
		void Load(const char* name);

		std::string getMyPath()
		{
			return std::format("{}\\{}", getModFolder().c_str(), m_name);
		};

	};

	inline lib::StaticArray<ModProfile*, MAX_MODS_PROFILE> Profiles;
}