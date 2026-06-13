#pragma once
#include <Windows.h>
#include <stdio.h>
#include <cstdio>
#include <assert.h>
#include <wininet.h>
#include <vector>
#pragma comment(lib, "wininet.lib")

#pragma warning(push)

#pragma warning(disable : 4996)

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#ifndef Clamp
#define Clamp(x, xmin, xmax) (min(xmax, max(x, xmin)))
#endif

namespace Utils
{
	class String
	{
	private:
		char* m_string = nullptr;
		size_t m_length = 0;
		size_t m_capacity = 0;

		void allocate(size_t capacity)
		{
			if (capacity == 0) return;
			deallocate();
			m_capacity = capacity;
			m_string = new char[capacity];
			m_length = 0;
			m_string[0] = '\0';
		}

		void copyFrom(const char* str, size_t length = 0)
		{
			deallocate();
			if (!str) return;

			if (length == 0) length = strlen(str);
			allocate(length + 1);
			m_length = length;
			memcpy(m_string, str, length);
			m_string[m_length] = '\0';
		}

		void reallocate(size_t capacity)
		{
			if (capacity <= m_capacity) return;
			char* newStr = new char[capacity];
			if (m_string)
			{
				memcpy(newStr, m_string, m_length + 1);
				delete[] m_string;
			}
			m_string = newStr;
			m_capacity = capacity;
		}

		void deallocate()
		{
			delete[] m_string;
			m_string = nullptr;
			m_length = 0;
			m_capacity = 0;
		}

	public:
		String() = default;

		explicit String(size_t length, char fill = '\0') { allocate(length); if (m_string) { memset(m_string, fill, length); m_string[length] = '\0'; } m_length = fill ? length : 0; }
		String(const char* str) { copyFrom(str); }
		String(const char* str, size_t length) { copyFrom(str, length); }
		String(const String& other) { copyFrom(other.m_string, other.m_length); }

		String(String&& other) noexcept
			: m_string(other.m_string), m_length(other.m_length), m_capacity(other.m_capacity) {
			other.m_string = nullptr;
			other.m_length = other.m_capacity = 0;
		}

		~String() { deallocate(); }

		String& operator=(const String& other)
		{
			if (this != &other) copyFrom(other.m_string, other.m_length);
			return *this;
		}

		String& operator=(String&& other) noexcept
		{
			if (this != &other)
			{
				deallocate();
				m_string = other.m_string;
				m_length = other.m_length;
				m_capacity = other.m_capacity;
				other.m_string = nullptr;
				other.m_length = other.m_capacity = 0;
			}
			return *this;
		}

		String& operator=(const char* str)
		{
			copyFrom(str);
			return *this;
		}

		void append(const char* str)
		{
			if (!str) return;
			size_t subLen = strlen(str);
			reallocate(m_length + subLen + 1);
			memcpy(m_string + m_length, str, subLen + 1);
			m_length += subLen;
		}

		void append(const String& other)
		{
			append(other.c_str());
		}

		String& operator+=(const char* str) { append(str); return *this; }
		String operator+(const char* str) const { String s(*this); s.append(str); return s; }

		String& operator+=(const Utils::String& str) { append(str); return *this; }
		String operator+(const Utils::String& str) const { String s(*this); s.append(str); return s; }

		bool operator==(const String& other) const
		{
			return strnicmp(c_str(), other.c_str(), m_length) == 0;
		}

		bool operator==(const char* str) const
		{
			return strnicmp(c_str(), str, m_length) == 0;
		}

		bool operator!=(const String& other) const { return !(*this == other); }
		bool operator!=(const char* str) const { return !(*this == str); }

		String operator/(const String& other) const
		{
			String copy = { *this };

			if (copy[copy.m_length - 1] != '\\')
				copy.append("\\");

			copy.append(other);

			return copy;
		}

		String operator/(const char* other) const
		{
			String copy = { *this };

			if (copy[copy.m_length - 1] != '\\')
				copy.append("\\");

			copy.append(other);

			return copy;
		}

		String& operator/=(const char* str)
		{
			if (m_string[m_length - 1] != '\\')
				append("\\");

			append(str);
			return *this;
		}

		String& operator/=(const Utils::String& str)
		{
			if (m_string[m_length - 1] != '\\')
				append("\\");

			append(str);
			return *this;
		}

		char& operator[](size_t idx)
		{
			// assert(idx < m_length);
			return m_string[idx];
		}

		const char& operator[](size_t idx) const
		{
			// assert(idx < m_length);
			return m_string[idx];
		}

		size_t length() const { return m_length; }
		size_t size() const { return m_length; }
		size_t capacity() const { return m_capacity; }
		bool empty() const { return m_length == 0; }

		void clear()
		{
			if (m_string) m_string[0] = '\0';

			m_length = 0;
		}

		void reserve(size_t cap) { reallocate(cap); }

		void shrink_to_fit()
		{
			if (m_length + 1 < m_capacity)
			{
				char* newStr = new char[m_length + 1];
				memcpy(newStr, m_string, m_length + 1);
				delete[] m_string;
				m_string = newStr;
				m_capacity = m_length + 1;
			}
		}

		const char* c_str() const { return m_string ? m_string : ""; }
		char* data() { return m_string; }

		String substr(size_t pos, size_t len = -1) const
		{
			if (pos >= m_length) return {};
			if (len == -1 || pos + len > m_length)
				len = m_length - pos;
			return String(m_string + pos, len);
		}

		const char* lower()
		{
			for (size_t i = 0; i < m_length; ++i) m_string[i] = tolower((unsigned char)m_string[i]);
			return c_str();
		}

		const char* upper()
		{
			for (size_t i = 0; i < m_length; ++i) m_string[i] = toupper((unsigned char)m_string[i]);
			return c_str();
		}

		int format(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			int len = formatV(fmt, args);
			va_end(args);
			return len;
		}

		int formatV(const char* fmt, va_list args)
		{
			va_list copy;
			va_copy(copy, args);
			int len = vsnprintf(nullptr, 0, fmt, copy);
			va_end(copy);

			reallocate(len + 1);
			vsnprintf(m_string, m_capacity, fmt, args);
			m_length = len;
			return len;
		}

		void resize()
		{
			m_length = strlen(c_str());
		}

		char* strrchr(int _C)
		{
			size_t len = length();

			if (len)
			{
				while (len)
				{
					if (m_string[len] == _C)
						return m_string + len;

					--len;
				}
			}

			return nullptr;
		}

		const char* strrchr(int _C) const
		{
			size_t len = length();

			if (len)
			{
				while (len)
				{
					if (m_string[len] == _C)
						return m_string + len;

					--len;
				}
			}

			return nullptr;
		}

		char* strchr(int _C)
		{
			if (!length())
				return nullptr;

			for (size_t i = 0; i < m_length && m_string[i] != '\0'; i++)
			{
				if (m_string[i] == _C)
					return m_string + i;
			}

			return nullptr;
		}

		const char* strchr(int _C) const
		{
			if (!length())
				return nullptr;

			for (size_t i = 0; i < m_length && m_string[i] != '\0'; i++)
			{
				if (m_string[i] == _C)
					return m_string + i;
			}

			return nullptr;
		}

		void erase(size_t pos, size_t len = -1)
		{
			if (pos >= m_length) return;

			if (len == -1 || pos + len > m_length)
				len = m_length - pos;

			memmove(m_string + pos, m_string + pos + len, m_length - pos - len + 1);
			m_length -= len;
		}

		size_t find(const char* substr, size_t start = 0) const
		{
			if (!substr || start >= m_length) return -1;
			const char* found = strstr(m_string + start, substr);
			return found ? found - m_string : -1;
		}
	};

	inline char* formatPath(char* buffer)
	{
		while (char* chr = strchr(buffer, '/'))
			*chr = '\\';

		return buffer;
	}

	inline char* formatPath(const char* buffer)
	{
		static char buff[MAX_PATH];
		memset(buff, 0, MAX_PATH);

		strcpy_s(buff, buffer);

		while (char* chr = strchr(buff, '/'))
			*chr = '\\';

		return buff;
	}

	inline String getProperSize(unsigned long long fileSize)
	{
		String format;

		// do double since we don't know where the float will not display accurately

		double sizeKB = (double)fileSize / 1024.0;
		double sizeMB = sizeKB / 1024.0;
		float sizeGB = float(sizeMB / 1024.0); // I don't think that it will ever reach 1024GB or so on

		format.format("%.1f%s", (double)fileSize > 1024.0 ? ((sizeKB > 1024.0) ? (sizeMB > 1024.0 ? sizeGB : sizeMB) : sizeKB) : fileSize, (double)fileSize > 1024.0 ? ((sizeKB > 1024.0) ? (sizeMB > 1024.0 ? "GB" : "MB") : "KB") : " Bytes");

		return format;
	}

	inline String strlow(const char* buffer)
	{
		String buff(buffer);

		for (int i = buff.length() - 1; i >= 0; i--)
			buff.data()[i] = tolower(buff.data()[i]);

		return buff;
	}

	inline char* strlow(char* buffer)
	{
		for (int i = strlen(buffer) - 1; i >= 0; i--)
			buffer[i] = tolower(buffer[i]); // here we modify the buffer

		return buffer;
	}

	inline size_t strhash(const Utils::String& str, bool CaseInsensitive = false)
	{
		int hash1 = (5381 << 16) + 5381;
		int hash2 = hash1;

		const size_t length = str.length();
		for (size_t i = 0; i < length; i += 2)
		{
			hash1 = ((hash1 << 5) + hash1) ^ (CaseInsensitive ? tolower(str[i]) : str[i]);
			if (i == length - 1)
				break;

			hash2 = ((hash2 << 5) + hash2) ^ (CaseInsensitive ? tolower(str[i + 1]) : str[i + 1]);
		}

		return hash1 + (hash2 * 1566083941);
	}

	inline String format(const char* fmt, ...)
	{
		String string;

		va_list va;
		va_start(va, fmt);

		string.formatV(fmt, va);

		va_end(va);

		return string;
	}

	inline String FloatStringNoTralingZeros(double value)
	{
		String str;
		str.format("%f", value);

		char* dot = str.strrchr('.');
		if (dot)
		{
			char* end = str.data() + str.length() - 1;
			while (end > dot && *end == '0' && (end - 1) != dot)
				*end-- = '\0';
		}

		str.resize();

		return str;
	}

	inline bool contains(const char* str1, const char* str2, bool caseSensitive = false)
	{
		if (!caseSensitive)
			return strstr(str1, str2) != nullptr;

		size_t strLen1 = strlen(str1);
		size_t strLen2 = strlen(str2);

		bool foundMatch = true;
		for (int i = 0; i < (int)strLen1; i++)
		{
			foundMatch = true;
			for (int j = 0; j < (int)strLen2; j++)
			{
				if (tolower(str1[i + j]) != tolower(str2[j]))
				{
					foundMatch = false;
					break;
				}
			}
			if (foundMatch)
				break;
		}

		return foundMatch;
	}

	inline std::vector<char> FetchURL(const char* url)
	{
		std::vector<char> data;
		HINTERNET hInternet = InternetOpenA("Mozilla/5.0", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
		if (hInternet)
		{
			HINTERNET hFile = InternetOpenUrlA(hInternet, url, nullptr, 0, INTERNET_FLAG_RELOAD, 0);
			if (hFile)
			{
				char buffer[1024];
				DWORD bytesRead;
				while (InternetReadFile(hFile, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
				{
					data.insert(data.end(), buffer, buffer + bytesRead);
				}
				InternetCloseHandle(hFile);
			}
			InternetCloseHandle(hInternet);
		}
		return data;
	}
}

#pragma warning(pop)