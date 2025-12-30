#include "ModLoader.h"
#include <Windows.h>
#include <HwDvd.h>
#include <deque>
#include <condition_variable>
#include <mutex>

#define FS_ASYNC_TRACE_TIME 0
#define FS_DEBUG_TRACE_READER_INFO 1

FileSystem::cReader::cReader()
{
	m_rno = MOVE_NONE;
	m_fp = nullptr;
	m_filedata = nullptr;
	m_buffersize = 0;
	m_bufferpos = 0;
}

FileSystem::cReader::cReader(const char* path, void* filedata, size_t buffersize)
{
	m_rno = MOVE_NONE;
	m_fp = nullptr;
	m_filedata = filedata;
	m_buffersize = buffersize;
	m_bufferpos = 0;
	open(path);
}

FileSystem::cReader::~cReader()
{
	close();
	release();
}

bool FileSystem::cReader::open(const char* path)
{
	if (isOpen())
		return false;

	Utils::String str = Utils::formatPath(path);

	m_file = File(str.c_str());
	struct stat s;
	if (stat(str.c_str(), &s) != 0)
		return false;
	m_file.m_filesize = (unsigned int)s.st_size;

	if (!m_filedata) m_rno = MOVE_READ_ALLOC;
	move();

	return isOpen();
}

void FileSystem::cReader::close()
{
	if (isOpen())
	{
		fclose(m_fp);
		m_fp = nullptr;
	}
	m_bufferpos = 0;
	m_rno = MOVE_NONE;
}

void FileSystem::cReader::seek(size_t position)
{
	if (!isOpen())
		return;

	fseek(m_fp, position, SEEK_SET);
	m_bufferpos = position;
}

size_t FileSystem::cReader::tell()
{
	if (!isOpen())
		return 0;

	return m_bufferpos;
}

size_t FileSystem::cReader::read()
{
	if (!isOpen())
		return 0;

	size_t toRead = ChunkSize;
	if (m_file.m_filesize - m_bufferpos == 0)
		return 0;

	if ((m_file.m_filesize - m_bufferpos) / ChunkSize == 0)
		toRead = m_file.m_filesize - m_bufferpos; // Read remaining bytes
	else
		toRead = ChunkSize;

	if (m_buffersize && toRead > m_buffersize)
		toRead = m_buffersize;

	size_t readBytes = fread((char*)m_filedata + m_bufferpos, 1, toRead, m_fp);

	m_bufferpos += readBytes;
	return readBytes;
}

void FileSystem::cReader::move()
{
	while (true)
	{
		if (m_WaitCount > 0)
		{
			m_WaitCount--;
			return;
		}
		switch (m_rno)
		{
		case MOVE_NONE:
			if (!moveNone())
				return;
			break;
		case MOVE_READ_ALLOC:
			if (!moveReadAlloc())
				return;
			break;

		case MOVE_READ_START:
			if (!moveReadStart())
				return;
			break;
		case MOVE_READ_WAIT:
			if (!moveReadEnd())
				return;
			break;
		case MOVE_CLEANUP_START:
			if (!moveCleanupStart())
				return;
			break;
		case MOVE_CLEANUP_END:
			if (!moveCleanupEnd())
				return;
			break;
		case MOVE_FILE_VALID:
			if (!moveFileValid())
				return;
			break;
		case MOVE_FILE_INVALID:
			if (!moveFileInvalid())
				return;
			break;
		default:
			return;
		}
	}
}

void FileSystem::cReader::startReadAsync()
{
	if (m_filedata)
		m_rno = MOVE_READ_START;
	else
		m_rno = MOVE_READ_ALLOC;
}

bool FileSystem::cReader::isComplete() const
{
	switch (m_rno)
	{
	case MOVE_FILE_VALID:
		return true;
	default:
		return false;
	}
	return false;
}

bool FileSystem::cReader::isCanceled() const
{
	switch (m_rno)
	{
	case MOVE_CLEANUP_END:
	case MOVE_FILE_INVALID:
		return true;
	default:
		return false;
	}
	return false;
}

bool FileSystem::cReader::isAlive() const
{
	return m_rno != MOVE_NONE;
}

void FileSystem::cReader::wait()
{
	for (; !isComplete() && !isCanceled(); )
		move();
}

bool FileSystem::cReader::moveNone()
{
	return false;
}

bool FileSystem::cReader::moveReadAlloc()
{
	if (!m_filedata)
	{
		m_filedata = ModloaderHeap.alloc(m_file.m_filesize, 0x1000, m_flags & READFLAG_ALLOC_BACK ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL, 0);
		m_buffersize = m_file.m_filesize;
		m_flags |= READFLAG_OWNS_ALLOC;
	}
	m_rno = MOVE_READ_START;

	if (!m_filedata)
	{
		m_rno = MOVE_FILE_INVALID;
		LOGERROR("FileSystem::cReader::moveReadAlloc: Failed to allocate memory for file read: %s", m_file.m_path.c_str());
	}

	return true;
}

bool FileSystem::cReader::moveReadStart()
{
	m_fp = fopen(m_file.m_path.c_str(), "rb");
	if (m_fp)
	{
		m_rno = MOVE_READ_WAIT;
#if FS_ASYNC_TRACE_TIME
		m_Timer.start();
#endif
		read();
	}
	else m_rno = MOVE_FILE_INVALID;
	return true;
}

bool FileSystem::cReader::moveReadEnd()
{
	read();
	if (m_bufferpos >= m_file.m_filesize)
	{
		m_rno = MOVE_FILE_VALID;
#if FS_ASYNC_TRACE_TIME
		m_Timer.stop();
		LOGINFO("FileSystem::cReader: Asynchronous read of file %s completed in %.3f s", m_file.m_path.c_str(), m_Timer.getDifferenceSeconds());
#endif
	}
	return true;
}

bool FileSystem::cReader::moveCleanupStart()
{
	close();
	m_rno = MOVE_CLEANUP_END;
	return true;
}

bool FileSystem::cReader::moveCleanupEnd()
{
	if (m_flags & READFLAG_OWNS_ALLOC && m_filedata)
	{
		operator delete(m_filedata, &ModloaderHeap);
		m_filedata = nullptr;
	}
	m_rno = MOVE_NONE;
	return false;
}

bool FileSystem::cReader::moveFileValid()
{
	return false; // Finished, and valid
}

bool FileSystem::cReader::moveFileInvalid()
{
	m_rno = MOVE_CLEANUP_START;
	return true;
}

void FileSystem::cReader::release()
{
	if (m_refCount) m_refCount--;
	if (m_refCount <= 0)
	{
		// delete this; // what the fuck?
		m_rno = MOVE_CLEANUP_START;

		if (m_flags & READFLAG_OWNS_ALLOC && getUseCount() > 0)
			LOGWARNING("File data allocated by the reader is used, crash is imminent!");
	}
}

static std::atomic<int> s_threadToken = 0;

static std::deque<FileSystem::cReader*> s_readerQueue;
static std::mutex s_readerQueueMutex;
static std::condition_variable s_readerQueueCv;

// Called when a reader is created or needs further processing
static void EnqueueReader(FileSystem::cReader* r)
{
    if (!r) return;
    std::lock_guard<std::mutex> lk(s_readerQueueMutex);

    if (!r->m_bClaimedByThread)
    {
        r->m_bClaimedByThread = true;
        r->m_threadIndex = -1;
        s_readerQueue.push_back(r);
        s_readerQueueCv.notify_one();
    }
}

DWORD WINAPI ReaderThreadProc(LPVOID lpParameter)
{
    const int threadIndex = (int)(uintptr_t)lpParameter;
    while (true)
    {
        FileSystem::cReader* reader = nullptr;

        {
            std::unique_lock<std::mutex> lk(s_readerQueueMutex);

            s_readerQueueCv.wait(lk, [] { return !s_readerQueue.empty(); });
            reader = s_readerQueue.front();

            s_readerQueue.pop_front();

            reader->m_threadIndex = threadIndex;
        }

        while (reader)
        {
            if (!reader->isAlive() || reader->isComplete() || reader->isCanceled())
            {
                reader->m_bClaimedByThread = false;
                reader->m_threadIndex = -1;
                break;
            }

            reader->move();

            if (reader->m_rno == FileSystem::cReader::MOVE_CLEANUP_END ||
                reader->m_rno == FileSystem::cReader::MOVE_FILE_VALID ||
                reader->m_rno == FileSystem::cReader::MOVE_FILE_INVALID)
            {
                reader->m_bClaimedByThread = false;
                reader->m_threadIndex = -1;
                break;
            }

            
            {
                std::lock_guard<std::mutex> lk(s_readerQueueMutex);

                if (reader->isAlive() && !reader->isComplete() && !reader->isCanceled())
                {
                    reader->m_bClaimedByThread = false;
                    reader->m_threadIndex = -1;
                    s_readerQueue.push_back(reader);
                    s_readerQueueCv.notify_one();
                    reader = nullptr;
                }
                else
                {
                    reader->m_bClaimedByThread = false;
                    reader->m_threadIndex = -1;
                    break;
                }
            }
        }
    }
    return 0;
}

void FileSystem::FileWalk(const std::function<void(File&)>& cb, const char* path)
{
	Hw::cDvdFileFind fileFind;
	if (!fileFind.startup((Utils::String(path) / "").c_str()))
		return;

	if (fileFind.isValid())
	{
		for (; fileFind.isValid(); fileFind.setNext())
		{
			if (fileFind.isSelf() || fileFind.isParent())
				continue;

			if (fileFind.isFile())
			{
				Utils::String filePath = Utils::String(path) / fileFind.refName();
				FileSystem::File file(filePath.c_str(), fileFind.getSize());
				cb(file);
			}
		}
	}

	fileFind.cleanup();
}

void FileSystem::FileWalkRecursive(const std::function<void(File&)>& cb, const char* path)
{
	Hw::cDvdFileFind fileFind;
	if (!fileFind.startup((Utils::String(path) / "").c_str()))
		return;

	if (fileFind.isValid())
	{
		for (; fileFind.isValid(); fileFind.setNext())
		{
			if (fileFind.isSelf() || fileFind.isParent())
				continue;

			if (fileFind.isDirectory())
			{
				Utils::String subdirPath = Utils::String(path) / fileFind.refName();
				FileWalkRecursive(cb, subdirPath.c_str());
			}
			else if (fileFind.isFile())
			{
				Utils::String filePath = Utils::String(path) / fileFind.refName();
				FileSystem::File file(filePath.c_str(), fileFind.getSize());
				cb(file);
			}
		}
	}

	fileFind.cleanup();
}

void FileSystem::DirectoryWalk(const std::function<void(Directory&)>& cb, const char* path)
{
	Directory dir(path);

	dir.scanFiles(false, false, Directory::SCANFLAG_DIRECTORIES);

	for (auto& subdir : dir.m_subdirs)
		cb(*subdir);
}

void FileSystem::DirectoryWalkRecursive(const std::function<void(Directory&)>& cb, const char* path)
{
	Directory dir(path);

	dir.scanFiles(true, false, Directory::SCANFLAG_ALL);

	for (auto& subdir : dir.m_subdirs)
		cb(*subdir);
}

bool FileSystem::PathExists(const char* path)
{
	struct stat s;
	return stat(path, &s) == 0;
}

void __cdecl fs_criErr_cb(const char* errid, unsigned int p1, unsigned int p2, unsigned int* pArray)
{
	Utils::String err = Utils::format(errid, p1, p2);
	LOGERROR("[CRIWARE] %s", err.c_str());
}

bool FileSystem::Init(unsigned int maxReaders)
{
	*(void**)(shared::base + 0x1CAE15C) = fs_criErr_cb;
	m_CycleReadId = 1;
	if (!m_ReaderFactoryCriticalSection.startup())
		return false;

	m_ReaderFactory.create(maxReaders, ModloaderHeap, "ReaderFactoryFixed");

	for (int i = 0; i < m_ReaderThreads.getCapacity(); ++i)
		m_ReaderThreads.pushBack(CreateThread(nullptr, 0, ReaderThreadProc, (void*)i, 0, nullptr));

	return true;
}

void FileSystem::Shutdown()
{
	for (auto& reader : m_ReaderFactory)
	{
		reader.m_rno = cReader::MOVE_CLEANUP_START;
		reader.wait();
	}
	UpdateReaders();
	m_ReaderFactory.destroy();
	m_ReaderFactoryCriticalSection.cleanup();
	LOGINFO("Cleaning up reader threads...");
	for (auto& thread : m_ReaderThreads)
	{
		OutputDebugStringA(Utils::format("Terminating reader thread %d...\n", (&thread - m_ReaderThreads.begin()) + 1).c_str());
		TerminateThread(thread, 0);
		CloseHandle(thread);
	}
	m_ReaderThreads.clear();
	LOGINFO("Reader threads cleaned up.");
}

void FileSystem::UpdateReaders()
{
	m_ReaderFactoryCriticalSection.enter();

	auto st = m_ReaderFactory.begin();
	auto en = m_ReaderFactory.end();
	for (; st != en; )
	{
		if (st->m_rno == cReader::MOVE_CLEANUP_END || st->getRefCount() <= 0 || !st->isAlive())
		{
			if (st->getRefCount())
				LOGERROR("Freed the reader with remaining references!");
			if (st->getUseCount())
				LOGERROR("Freed the reader with remaining uses!");

			st = m_ReaderFactory.erase(st);
		}
		else ++st;
	}

	m_ReaderFactoryCriticalSection.leave();
}

FileSystem::eReadId FileSystem::ReadAsync(const char* path, void* filedata, size_t buffersize)
{
	m_ReaderFactoryCriticalSection.enter();
	
	cReader* pWork = m_ReaderFactory.newWork();
	if (!pWork)
	{
		m_ReaderFactoryCriticalSection.leave();
		return READID_INVALID;
	}

	*pWork = cReader(path, filedata, buffersize);
	pWork->startReadAsync();
	pWork->m_readId = (eReadId)CycleReadId();
	pWork->addRef();
	EnqueueReader(pWork);

	m_ReaderFactoryCriticalSection.leave();
	return pWork->m_readId;
}

bool FileSystem::ReadSync(const char* path, void* filedata, size_t buffersize)
{
	cReader reader(path, filedata, buffersize);
	reader.startReadAsync();
	reader.wait();

	if (reader.isCanceled())
		return false;

	return true;
}

FileSystem::eReadId FileSystem::ReadAsyncAlloc(const char* path, void** filedata, size_t* outFileSize, unsigned int flags)
{
	struct stat s;
	if (stat(path, &s) != 0)
		return READID_INVALID;

	*outFileSize = (size_t)s.st_size;

	void* pFiledata = ModloaderHeap.alloc(*outFileSize, 0x1000, flags & cReader::READFLAG_ALLOC_BACK ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL, 0);
	if (!pFiledata)
		return READID_INVALID;

	eReadId readId = READID_INVALID;

	if (readId = ReadAsync(path, pFiledata, *outFileSize); readId == READID_INVALID)
	{
		operator delete(pFiledata, &ModloaderHeap);

		*filedata = nullptr;
		*outFileSize = 0;

		return READID_INVALID;
	}

	*filedata = pFiledata;

	return readId;
}

bool FileSystem::ReadSyncAlloc(const char* path, void** filedata, size_t* outFileSize, unsigned int flags)
{
	struct stat s;
	if (stat(path, &s) != 0)
		return false;

	*outFileSize = (size_t)s.st_size;
	void* pFiledata = ModloaderHeap.alloc(*outFileSize, 0x1000, flags & cReader::READFLAG_ALLOC_BACK ? Hw::HW_ALLOC_PHYSICAL_BACK : Hw::HW_ALLOC_PHYSICAL, 0);
	if (!pFiledata)
		return false;

	if (!ReadSync(path, pFiledata, *outFileSize))
	{
		operator delete(pFiledata, &ModloaderHeap);

		*filedata = nullptr;
		*outFileSize = 0;

		return false;
	}

	*filedata = pFiledata;

	return true;
}

bool FileSystem::IsReaderAlive(eReadId reader)
{
	for (auto& r : m_ReaderFactory)
	{
		if (r.m_readId == reader)
			return r.isAlive();
	}
	return false;
}

bool FileSystem::IsReadCanceled(eReadId reader)
{
	for (auto& r : m_ReaderFactory)
	{
		if (r.m_readId == reader)
			return r.isCanceled();
	}
	return false;
}

void FileSystem::CancelRead(eReadId reader)
{
	for (auto& r : m_ReaderFactory)
	{
		if (r.m_readId == reader)
		{
			r.release();
			return;
		}
	}
}

void* FileSystem::GetReadFileData(eReadId reader)
{
	for (auto& r : m_ReaderFactory)
	{
		if (r.m_readId == reader)
			return r.m_filedata;
	}
}

bool FileSystem::WaitForRead(eReadId reader)
{
	for (auto &r : m_ReaderFactory)
	{
		if (r.m_readId == reader)
		{
			r.wait();
			return true;
		}
	}
	return false;
}

bool FileSystem::IsReadComplete(eReadId reader)
{
	for (auto& r : m_ReaderFactory)
	{
		if (r.m_readId == reader)
			return r.isComplete();
	}
	return false;
}

bool FileSystem::File::read(void *filedata)
{
	if (!filedata)
		return false;

	cReader reader(m_path.c_str(), filedata, m_filesize);
	reader.startReadAsync();

	reader.wait();

	if (reader.isCanceled())
		return false;

	return true;
}

void* FileSystem::File::read()
{
	cReader reader(m_path.c_str(), nullptr, m_filesize);
	reader.startReadAsync();

	reader.wait();

	if (reader.isCanceled())
		return nullptr;

	return reader.m_filedata;
}

const char* FileSystem::File::getName()
{
	const char* lastSlash = strrchr(m_path.c_str(), '\\');
	if (lastSlash)
		return lastSlash + 1;

	return m_path.c_str();
}

void FileSystem::Directory::FileWalk(const std::function<void(File&)>& cb)
{
	for (auto& file : m_files)
		cb(file);

	for (auto& subdir : m_subdirs)
		subdir->FileWalk(cb);
}

void FileSystem::Directory::clear()
{
	for (auto& subdir : m_subdirs)
	{
		subdir->clear();
		delete subdir;
	}

	m_subdirs.clear();
	m_files.clear();
}

void FileSystem::Directory::calculateDirectorySize()
{
	m_filesize = 0l;
	for (auto& file : m_files)
		m_filesize += file.m_filesize;

	for (auto& subdir : m_subdirs)
	{
		subdir->calculateDirectorySize();
		m_filesize += subdir->m_filesize;
	}
}

const char* FileSystem::Directory::getName()
{
	const char* lastSlash = strrchr(m_path.c_str(), '\\');
	if (lastSlash)
		return lastSlash + 1;

	return m_path.c_str();
}

void FileSystem::Directory::scanFiles(bool bRecursive, const bool bInSubFolder, unsigned int flags)
{
	Hw::cDvdFileFind fileFind;

	if (!fileFind.startup((m_path / "").c_str())) // that's so retarded
		return;

	if (fileFind.isValid())
	{
		for (Directory* pDir = nullptr; fileFind.isValid(); fileFind.setNext())
		{
			if (fileFind.isSelf() || fileFind.isParent()) // . and .., need to be applied to files too
				continue;

			if (flags & SCANFLAG_DIRECTORIES && fileFind.isDirectory())
			{
				Utils::String subdirPath = m_path / fileFind.refName();

				pDir = new Directory(subdirPath.c_str());
				pDir->m_parent = this;

				m_subdirs.push_back(pDir);
				if (bRecursive)
					pDir->scanFiles(true, true, flags);
			}
			else if (flags & SCANFLAG_FILES && fileFind.isFile())
			{
				Utils::String filePath = m_path / fileFind.refName();

				FileSystem::File file(filePath.c_str(), fileFind.getSize());

				file.m_bInSubFolder = bInSubFolder;
				m_files.push_back(file);
			}
		}
	}
	fileFind.cleanup();
}

void FileSystem::Directory::Dump(const Utils::String& indent)
{
	Utils::String newIndent = indent + "  ";
	//Logger::Printf("%sDirectory: %s\n", indent.c_str(), getName());
	for (auto& file : m_files)
		//Logger::Printf("%sFile: %s (Size: %u bytes)\n", newIndent.c_str(), file.getName(), file.m_filesize);

	for (auto& subdir : m_subdirs)
		subdir->Dump(newIndent);
}

FileSystem::File* FileSystem::Directory::FindFile(const Utils::String& filepath)
{
	if (filepath.empty())
		return nullptr;

	Utils::String path = filepath;
	char* buf = path.data();
	for (size_t i = 0, n = path.size(); i < n; ++i)
	{
		if (buf[i] == '/')
			buf[i] = '\\';
	}

	const char* filename = path.c_str();
	if (const char* chr = path.strrchr('\\'); chr)
		filename = chr + 1;

	if (!filename || filename[0] == '\0')
		return nullptr;

	for (auto& file : m_files)
	{
		if (strcmp(file.getName(), filename) == 0)
			return &file;
	}

	// Recurse into subdirectories
	for (auto& subdir : m_subdirs)
	{
		if (File* found = subdir->FindFile(filename))
			return found;
	}

	return nullptr;
}

FileSystem::Directory* FileSystem::Directory::FindSubDir(const Utils::String& path)
{
	if (path.empty())
		return nullptr;

	Utils::String dirPath = path;
	char* buf = dirPath.data();
	for (size_t i = 0, n = dirPath.size(); i < n; ++i)
	{
		if (buf[i] == '/')
			buf[i] = '\\';
	}

	const char* dirname = dirPath.c_str();

	if (FileSystem::PathExists((m_path / dirname).c_str()))
	{
		std::vector<std::string> pathParts;
		size_t start = 0;
		size_t end = 0;
		while ((end = dirPath.find("\\", start)) != -1)
		{
			if (end != start)
				pathParts.push_back(dirPath.substr(start, end - start).c_str());
			start = end + 1;
		}
		if (start < dirPath.size())
			pathParts.push_back(dirPath.substr(start).c_str());
		return FindSubDirRecursive(pathParts, 0);
	}
	
	if (const char *chr = dirPath.strrchr('\\'); chr)
		dirname = chr + 1;

	if (!dirname || dirname[0] == '\0')
		return nullptr;

	for (auto& subdir : m_subdirs)
	{
		if (strcmp(subdir->getName(), dirname) == 0)
			return subdir;
	}

	return nullptr;
}

FileSystem::Directory* FileSystem::Directory::FindSubDirRecursive(const std::vector<std::string>& pathParts, size_t index)
{
	if (index >= pathParts.size())
		return nullptr;

	const char* dirname = pathParts[index].c_str();

	if (!dirname || dirname[0] == '\0')
		return nullptr;

	for (auto& subdir : m_subdirs)
	{
		if (strcmp(subdir->getName(), dirname) == 0)
		{
			if (index + 1 == pathParts.size())
				return subdir;
			else
				return subdir->FindSubDirRecursive(pathParts, index + 1);
		}
	}
	return nullptr;
}