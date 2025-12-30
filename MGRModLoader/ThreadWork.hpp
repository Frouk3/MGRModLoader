#pragma once

#include <Windows.h>
#include <vector>
#include <process.h>

class cThread
{
public:
	enum MOVE_RNO
	{
		MOVE_NONE = 0,
		MOVE_START,
		MOVE_RUNNING,
		MOVE_END
	};
protected:
	MOVE_RNO m_Rno = MOVE_NONE;
	HANDLE m_hThread = nullptr;
	DWORD m_ThreadId = 0;
	size_t m_StackSize = 0x4000; // 16 KB
	void(__cdecl* m_pThreadFunc)(cThread* pThread, LPVOID pParam) = nullptr;
	LPVOID m_pParam;
public:
	cThread(void(__cdecl* pThreadFunc)(cThread* pThread, LPVOID pParam), LPVOID pParam, bool start = true, size_t stackSize = 0x4000)
		: m_pThreadFunc(pThreadFunc), m_pParam(pParam), m_StackSize(stackSize) 
	{
		if (start)
			this->start();
	}

	virtual ~cThread();

	inline void setRno(MOVE_RNO rno) { m_Rno = rno; }
	inline MOVE_RNO getRno() const { return m_Rno; }

	virtual void start();
	virtual void join();
	virtual bool isRunning();

	static unsigned int __stdcall ThreadProc(void* lpParameter)
	{
		cThread* pThread = (cThread*)lpParameter;
		pThread->setRno(MOVE_RUNNING);
		if (pThread && pThread->m_pThreadFunc)
			pThread->m_pThreadFunc(pThread, pThread->m_pParam);
		pThread->setRno(MOVE_END);
		
		OutputDebugStringA(Utils::format("Thread %04X exited with code 0.\n", pThread->m_ThreadId).c_str());

		_endthreadex(0);

		return 0;
	}
};

inline void cThread::start()
{
	if (m_hThread)
		return;

	setRno(MOVE_START);
	m_hThread = (HANDLE)_beginthreadex(nullptr, (unsigned int)m_StackSize, ThreadProc, this, 0, (unsigned int*)&m_ThreadId);
	OutputDebugStringA(Utils::format("Thread %04X started.\n", m_ThreadId).c_str());
}

inline void cThread::join()
{
	if (m_hThread)
	{
		WaitForSingleObject(m_hThread, INFINITE);
		CloseHandle(m_hThread);
		m_hThread = nullptr;
	}
}

inline bool cThread::isRunning()
{
	return getRno() == MOVE_RUNNING;
}

inline cThread::~cThread()
{
	join();
}

class ThreadWork
{
private:
	static inline std::vector<cThread*> m_ActiveThreads;
public:
	static void AddThread(cThread* pThread)
	{
		m_ActiveThreads.push_back(pThread);
	}

	static void UpdateThreads()
	{
		for (auto it = m_ActiveThreads.begin(); it != m_ActiveThreads.end(); )
		{
			cThread* pThread = *it;
			if (pThread)
			{
				if (pThread->isRunning() || pThread->getRno() == cThread::MOVE_START)
				{
					++it;
				}
				else
				{
					pThread->join();
					delete pThread;
					it = m_ActiveThreads.erase(it);
				}
			}
			else
			{
				++it;
			}
		}
	}

	static void ClearThreads()
	{
		for (cThread* pThread : m_ActiveThreads)
		{
			if (pThread)
			{
				pThread->join();
				delete pThread;
			}
		}
		m_ActiveThreads.clear();
	}

	static size_t GetActiveThreadCount()
	{
		return m_ActiveThreads.size();
	}
};