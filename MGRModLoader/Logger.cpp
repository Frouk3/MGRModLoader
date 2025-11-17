#include "ModLoader.h"
#include <Windows.h>
#include <assert.h>

void Logger::Init()
{
	LoadConfig();

	if (!LogFilePath[0])
	{
		GetModuleFileNameA(nullptr, LogFilePath, MAX_PATH);
		if (char* lastSlash = strrchr(LogFilePath, '\\'))
			*lastSlash = 0;

		strcat(LogFilePath, "\\LoaderLog.log");
	}

	if (!bEnabled)
		return;

	if (!LogFile)
		Open();
}

void Logger::LoadConfig()
{
	IniReader ini("MGRModLoaderSettings.ini");

	bEnabled = ini.ReadBool("ModLoader", "LoggingEnabled", bEnabled);
	bFlushImmediately = ini.ReadBool("ModLoader", "FlushImmediately", bFlushImmediately);
}

void Logger::SaveConfig()
{
	IniReader ini("MGRModLoaderSettings.ini");

	ini.WriteBool("ModLoader", "LoggingEnabled", bEnabled);
	ini.WriteBool("ModLoader", "FlushImmediately", bFlushImmediately);
}

void Logger::SetPath(const char* path)
{
	strcpy(LogFilePath, path);
}

void Logger::Open()
{
	if (LogFile)
		return;

	if (!LogFilePath[0])
	{
		GetModuleFileNameA(nullptr, LogFilePath, MAX_PATH);
		if (char* lastSlash = strrchr(LogFilePath, '\\'))
			*lastSlash = 0;

		strcat(LogFilePath, "\\LoaderLog.log");
	}

	LogFile = fopen(LogFilePath, "w");
}

void Logger::Close()
{
	if (LogFile)
	{
		fclose(LogFile);
		LogFile = nullptr;
	}
}

void Logger::ReOpen()
{
	if (!(LogFile = freopen(LogFilePath, "w", LogFile)))
		assert(!"Failed to reopen log file");
}

void Logger::Flush()
{
	if (bFlushImmediately && LogFile)
		fflush(LogFile);
}

void Logger::vPrintf(const char* format, va_list args)
{
	if (!bEnabled)
		return;

	Open();

	if (!LogFile)
		return;

	vfprintf(LogFile, format, args);

	Flush();
}

void Logger::Printf(const char* format, ...)
{
	if (!bEnabled)
		return;

	Open();

	if (!LogFile)
		return;

	va_list args;
	va_start(args, format);

	vPrintf(format, args);

	va_end(args);

	Flush();
}

void Logger::PrintfLn(const char* format, ...)
{
	if (!bEnabled)
		return;

	Open();

	if (!LogFile)
		return;

	va_list args;
	va_start(args, format);

	SYSTEMTIME time;
	GetLocalTime(&time);

	Printf("[%02d:%02d:%02d.%03d] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
	vPrintf(format, args);
	Printf("\n");

#if LOGGER_DEBUG
	Utils::String str;
	str.formatV(format, args);
	LatestLog.push_back({ Utils::format((Utils::String("[%02d:%02d:%02d.%03d] ") + str).c_str(), time.wHour, time.wMinute, time.wSecond, time.wMilliseconds), 3.f});
#endif
	va_end(args);
}