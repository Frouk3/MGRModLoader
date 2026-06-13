#include "ModLoader.h"
#include "ThreadWork.hpp"
#include "Utils.h"

void Updater::Init()
{
	LoadConfig();
	if (bEnabled)
		CheckAsync();
}

bool CheckUpd()
{
	using namespace Updater;

	const char* szURL = "https://raw.githubusercontent.com/Frouk3/ModMenuVersions/main/MGRModLoader";

	std::vector<char> data = Utils::FetchURL(szURL);

	if (data.empty())
	{
		eUpdateStatus = UPDATE_STATUS_NO_INTERNET;
		LOGERROR("Failed to fetch update info. No internet connection?");
		return false;
	}

	float fVersion = (float)atof(data.data());
	fLatestVersion = fVersion;
	if (fVersion > fCurrentVersion)
	{
		eUpdateStatus = UPDATE_STATUS_AVAILABLE;
		LOGINFO("New version available: %s", Utils::FloatStringNoTralingZeros(fVersion));
		return true;
	}
	else
	{
		eUpdateStatus = UPDATE_STATUS_LATEST_INSTALLED;
		LOGINFO("Mod Loader is up to date.");
	}

	return false;
}

bool Updater::CheckAsync()
{
	ThreadWork::AddThread(new cThread([](cThread* pThread, LPVOID pParam)
		{
			CheckUpd();
		}, nullptr));

	return true;
}

bool Updater::CheckSync()
{
	return CheckUpd();
}

void Updater::LoadConfig()
{
	IniReader ini("MGRModLoaderSettings.ini");

	bEnabled = ini.ReadBool("ModLoader", "CheckUpdates", bEnabled);
}

void Updater::SaveConfig()
{
	IniReader ini("MGRModLoaderSettings.ini");

	ini.WriteBool("ModLoader", "CheckUpdates", bEnabled);
}
