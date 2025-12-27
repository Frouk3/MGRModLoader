#include "ModLoader.h"
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

void Updater::Init()
{
	LoadConfig();
	if (bEnabled)
		CheckAsync();
}

bool CheckUpd()
{
	using namespace Updater;

	bool result = false;
	HRESULT hr;
	const char* szURL = "https://github.com/Frouk3/ModMenuVersions/raw/refs/heads/main/MODLOADERVERSION.ini";

	Utils::String tempPath(MAX_PATH);
	GetTempPathA(MAX_PATH, tempPath.data());
	tempPath.resize();

	tempPath /= "MODLOADERVERSION.ini";

	hr = URLDownloadToFileA(nullptr, szURL, tempPath.c_str(), 0, nullptr);
	if (hr == S_OK)
	{
		char verBuf[16] = { 0 };
		if (GetPrivateProfileStringA("Metal Gear Rising Revengeance", "VERSION", "-1.0", verBuf, sizeof(verBuf), tempPath.c_str()))
		{
			fLatestVersion = atof(verBuf);
			if (fLatestVersion > fCurrentVersion)
			{
				eUpdateStatus = UPDATE_STATUS_AVAILABLE;
				LOGINFO("New version available!: %.2f (You have %.2f)", fLatestVersion, fCurrentVersion);
				result = true;
			}
			else if (fLatestVersion == -1.0)
			{
				eUpdateStatus = UPDATE_STATUS_FAILED;
				LOGERROR("Failed to retrieve the latest version.");
				result = false;
			}
			else
			{
				eUpdateStatus = UPDATE_STATUS_LATEST_INSTALLED;
				LOGINFO("You have the latest version installed: %.2f", fCurrentVersion);
				result = false;
			}
		}
		else
		{
			eUpdateStatus = UPDATE_STATUS_FAILED;
			LOGERROR("Failed to read the latest version from the update file.");
			result = false;
		}
	}
	else
	{
		if (hr == INET_E_DOWNLOAD_FAILURE)
		{
			eUpdateStatus = UPDATE_STATUS_NO_INTERNET;
			LOGERROR("Unable to check for updates due to no internet connection?");
		}
		else
		{
			eUpdateStatus = UPDATE_STATUS_FAILED;
			LOGERROR("Failed to download the update file. HRESULT: 0x%X", hr);
		}
		result = false;
	}

	remove(tempPath.c_str());

	return result;
}

bool Updater::CheckAsync()
{
	hUpdateThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD
		{
			CheckUpd();

			return 0;
		}, nullptr, 0, nullptr);

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
