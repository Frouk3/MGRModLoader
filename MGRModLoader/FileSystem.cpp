#include "ModLoader.h"
#include <Windows.h>

void FileSystem::Directory::FileWalk(const std::function<void(File&)>& cb)
{
	for (auto& file : m_files)
		cb(file);
	for (auto& dir : m_subdirs)
		dir->FileWalk(cb);
}

void FileSystem::Directory::clear()
{
	if (!m_files.empty())
		m_files.clear();

	if (!m_subdirs.empty())
	{
		for (auto& dir : m_subdirs)
		{
			dir->clear();
			delete dir;
		}

		m_subdirs.clear();
	}

	m_filesize = 0l;
}

void FileSystem::Directory::calculateDirectorySize()
{
	m_filesize = 0l;

	FileWalk([&](File& file)
		{
			m_filesize += file.m_filesize;
		});
}

const char* FileSystem::Directory::getName()
{
	return m_path.strrchr('\\') + 1;
}

void FileSystem::Directory::scanFiles(bool bRecursive, const bool bInSubFolder, unsigned int flags)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = FindFirstFileA((Utils::String(m_path) / "*").c_str(), &fd);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		Logger::PrintfLn("Failed to read directory %s", m_path.c_str());
		return;
	}
	else
	{
		do
		{
			if (fd.cFileName[0] == '.')
				continue;

			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				Directory *subdir = new Directory(Utils::String(m_path) / fd.cFileName);
				subdir->m_parent = this;

				if (bRecursive)
					subdir->scanFiles(bRecursive, true, flags);

				subdir->calculateDirectorySize();

				if (flags & 2)
					m_subdirs.push_back(subdir);
			}
			else
			{
				File file(m_path / fd.cFileName, fd.nFileSizeLow);
				file.m_bInSubFolder = bInSubFolder;

				if (flags & 1)
					m_files.push_back(file);
			}

		} while (FindNextFile(hFind, &fd) != 0);
	}

	FindClose(hFind);
}

void FileSystem::Directory::Dump(const Utils::String& extPrintLn)
{
	Logger::Printf("%sDirectory %s\n", extPrintLn.c_str(), m_path.c_str());
	for (auto& dir : m_subdirs)
		dir->Dump(extPrintLn + "|-\t");

	for (auto& file : m_files)
		Logger::Printf("%s|-\tFile %s [%s]\n", extPrintLn.c_str(), file.m_path.c_str(), Utils::getProperSize(file.m_filesize).c_str());
}

FileSystem::File* FileSystem::Directory::FindFile(const Utils::String& filepath)
{
	if (filepath.empty())
		return nullptr;

	Utils::String path = filepath;

	while (char* slash = path.strchr('/')) *slash = '\\';

	// Now we need to make sure we'll only find the filename without any restrictions with folders that were pointed in the filepath parameter

	const char* filename = path.c_str();

	if (const char *chr = path.strrchr('\\'); chr)
	{
		filename = chr + 1;
	}

	for (auto& file : m_files)
	{
		if (!strcmp(file.getName(), filename))
			return &file;
	}

	for (auto& subdir : m_subdirs)
	{
		if (File* file = subdir->FindFile(filename); file)
			return file;
	}

	return nullptr;
}

FileSystem::Directory* FileSystem::Directory::FindSubDir(const Utils::String& path)
{
	if (path.empty())
		return nullptr;

	std::vector<std::string> pathParts; // how the fuck it crashes with Utils::String where every field is valid

	const char* begin = path.c_str();
	const char* end = path.c_str() + path.length();

	while (begin < end)
	{
		const char* next = strchr(begin, '\\');
		if (!next)
			next = end;

		pathParts.push_back(std::string(begin, next - begin));
		begin = next + 1;
	}

	if (pathParts.empty())
		return nullptr;

	return FindSubDirRecursive(pathParts, 0);
}

FileSystem::Directory* FileSystem::Directory::FindSubDirRecursive(const std::vector<std::string>& pathParts, size_t index)
{
	if (index >= pathParts.size())
		return this;

	for (auto& dir : m_subdirs)
	{
		if (pathParts[index] == dir->getName())
			return dir->FindSubDirRecursive(pathParts, index + 1);
	}

	return nullptr;
}

bool FileSystem::File::read(void* filedata)
{
	if (!filedata)
		return false;

	FILE* file = fopen(m_path.c_str(), "rb");

	if (!file)
		return false;

	fseek(file, 0, SEEK_SET);

	if (!fread(filedata, m_filesize, 1u, file))
	{
		fclose(file);
		return false;
	}

	fclose(file);
	return true;
}

void* FileSystem::File::read()
{
	if (!m_filesize)
		return nullptr;

	void *heap = ModloaderHeap.AllocateMemory(m_filesize, 0x1000u, 1, 0);

	if (!heap)
		return heap; // same as `nullptr`

	if (!read(heap))
	{
		operator delete(heap, &ModloaderHeap);
		return nullptr;
	}

	return heap;
}

const char* FileSystem::File::getName()
{
	return m_path.strrchr('\\') + 1;
}

void FileSystem::FileWalk(const std::function<void(File&)>& cb, const char* path)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = FindFirstFile((Utils::String(path) / "*").c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (fd.cFileName[0] == '.')
			continue;

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		File file((Utils::String(path) / fd.cFileName).c_str(), fd.nFileSizeLow);

		cb(file);
	} while (FindNextFile(hFind, &fd) != 0);

	FindClose(hFind);
}

void FileSystem::FileWalkRecursive(const std::function<void(File&)>& cb, const char* path)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = FindFirstFile((Utils::String(path) / "*").c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (fd.cFileName[0] == '.')
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			FileWalkRecursive(cb, (Utils::String(path) / fd.cFileName).c_str());
			continue;
		}
		File file((Utils::String(path) / fd.cFileName).c_str(), fd.nFileSizeLow);

		cb(file);
	} while (FindNextFile(hFind, &fd) != 0);

	FindClose(hFind);
}

void FileSystem::DirectoryWalk(const std::function<void(Directory&)>& cb, const char* path)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = FindFirstFile((Utils::String(path) / "*").c_str(), &fd);

	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (fd.cFileName[0] == '.')
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			Directory dir((Utils::String(path) / fd.cFileName).c_str());

			cb(dir);
		}
	} while (FindNextFile(hFind, &fd) != 0);

	FindClose(hFind);
}

void FileSystem::DirectoryWalkRecursive(const std::function<void(Directory&)>& cb, const char* path)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = FindFirstFile((Utils::String(path) / "*").c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (fd.cFileName[0] == '.')
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			Directory dir((Utils::String(path) / fd.cFileName).c_str());
			dir.scanFiles(true, false, 3);

			cb(dir);
		}
	} while (FindNextFile(hFind, &fd) != 0);
	FindClose(hFind);
}

bool FileSystem::PathExists(const char* path)
{
	struct stat s;
	return stat(path, &s) == 0;
}