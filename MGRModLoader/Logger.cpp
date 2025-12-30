#include "ModLoader.h"
#include <Windows.h>
#include <assert.h>
#include <Fw.h>
#include <sys/stat.h>

void Logger::Init()
{
	LoadConfig();

	GetModuleFileNameA(nullptr, LogFilePath, MAX_PATH);
	struct stat s;
	if (stat(LogFilePath, &s) == 0)
	{
		if ((s.st_mode & S_IFMT) & S_IFDIR) Fw::String::append(LogFilePath, "LoaderLog.log");
		else
		{
			char* lastSlash = strrchr(LogFilePath, '\\');
			if (lastSlash)
			{
				lastSlash[1] = '\0';
				Fw::String::append(LogFilePath, "LoaderLog.log");
			}
			// else Fw::String::copy(LogFilePath, "LoaderLog.log", MAX_PATH); // Unlikely case, but we can handle it
		}
	}
	Open();
}

void Logger::Shutdown()
{
	Close();
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

void Logger::Open()
{
	LogFile = fopen(LogFilePath, "w"); // using safe version will prevent you from opening files from explorer, apparently
	assert(LogFile != nullptr);
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
	LogFile = freopen(LogFilePath, "w", LogFile);
}

void Logger::SetPath(const char* path)
{
	Fw::String::copy(LogFilePath, path, MAX_PATH);
}

void Logger::Flush()
{
	if (LogFile)
		fflush(LogFile);
}

void Logger::vPrintf(const char* format, va_list args)
{
	if (!bEnabled || !LogFile)
		return;

	SYSTEMTIME sys;
	GetLocalTime(&sys);

	fprintf_s(LogFile, "[%02d:%02d:%02d.%03d] ", sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
	vfprintf_s(LogFile, format, args);

	if (bFlushImmediately)
		Flush();
}

void Logger::PrintfLn(const char* format, ...)
{
	if (!bEnabled || !LogFile)
		return;

	va_list args;
	va_start(args, format);
	vPrintf(format, args);
	va_end(args);
	fprintf_s(LogFile, "\n");

	if (bFlushImmediately)
		Flush();
}

void Logger::Printf(const char* format, ...)
{
	if (!bEnabled || !LogFile)
		return;

	va_list args;
	va_start(args, format);
	vPrintf(format, args);
	va_end(args);
}

void Logger::PrintfNoTime(const char* format, ...)
{
	if (!bEnabled || !LogFile)
		return;

	va_list args;
	va_start(args, format);
	vfprintf_s(LogFile, format, args);
	va_end(args);

	if (bFlushImmediately)
		Flush();
}