#include "ModLoader.h"
#include <urlmon.h>

#pragma comment(lib, "urlmon.lib")

void Updater::Init()
{
	LoadConfig();

	hUpdateThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD
		{
			Updater::CheckForUpdates();
			return 0;
		}, nullptr, 0, nullptr);
}

bool Updater::CheckForUpdates()
{
	if (!bEnabled)
		return false;

	Utils::String savePath(MAX_PATH);
	GetTempPathA(MAX_PATH, savePath.data());

	savePath.resize();

	savePath /= "ModLoaderVersion.ini";

	LOGINFO("Checking updates...");

	HRESULT hr = URLDownloadToFileA(nullptr, "https://github.com/Frouk3/ModMenuVersions/raw/refs/heads/main/MODLOADERVERSION.ini", savePath.c_str(), 0, nullptr);

	if (FAILED(hr))
	{
		eUpdateStatus = UPDATE_STATUS_FAILED;
		LOGERROR("Failed to check updates.");
		return false;
	}

	if (SUCCEEDED(hr))
	{
		char versionBuffer[16] = { 0 };
		GetPrivateProfileStringA("Metal Gear Rising Revengeance", "VERSION", "-1.0", versionBuffer, sizeof(versionBuffer), savePath.c_str());
		if (fLatestVersion = atof(versionBuffer); fLatestVersion != -1.0)
		{
			if (fCurrentVersion < fLatestVersion)
			{
				eUpdateStatus = UPDATE_STATUS_AVAILABLE;
				LOGINFO("Update is available: %s < %s", Utils::FloatStringNoTralingZeros(fCurrentVersion).c_str(), Utils::FloatStringNoTralingZeros(fLatestVersion).c_str());

				return true;
			}
			else if (fCurrentVersion == fLatestVersion)
			{
				eUpdateStatus = UPDATE_STATUS_LATEST_INSTALLED;
				LOGINFO("Version is up to date.");

				return true;
			}
			else
			{
				eUpdateStatus = UPDATE_STATUS_UNEXPECTED;
				LOGINFO("You have a newer version than the latest available: %s > %s", Utils::FloatStringNoTralingZeros(fCurrentVersion).c_str(), Utils::FloatStringNoTralingZeros(fLatestVersion).c_str());

				return true;
			}
		}
		else
		{
			eUpdateStatus = UPDATE_STATUS_FAILED;
			LOGERROR("Failed to read version from file.");
			return false;
		}
	}

	eUpdateStatus = UPDATE_STATUS_UNEXPECTED;
	return false;
}

bool Updater::CheckForOnce()
{
	bool temp = bEnabled;

	bEnabled = true;
	bool result = CheckForUpdates();
	bEnabled = temp;

	return result;
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