#pragma once
#include <Windows.h>
#include <shared.h>
#include <stdio.h>
#include <cstdio>
#include <assert.h>

#pragma warning(push)

#pragma warning(disable : 4996)

namespace Utils
{
	class String
	{
	private:
		char* m_string = nullptr;
		unsigned short m_length = 0;
		unsigned short m_capacity = 0;

		void allocate(size_t capacity)
		{
			if (!capacity)
				return;

			deallocate();

			m_capacity = (unsigned short)capacity;
			m_string = (char*)malloc(capacity);
			m_length = 0;

			if (m_string)
				m_string[0] = 0;
		}

		void copyFrom(const char* str, size_t length = 0)
		{
			deallocate();

			if (str)
			{
				if (!length)
					length = strlen(str);

				allocate(length + 1);
				m_length = (unsigned short)length;
				
				if (m_string)
				{
					strcpy(m_string, str);
					m_string[m_length] = 0;
				}
			}
		}

		void reallocate(size_t capacity)
		{
			if (m_length > capacity)
				assert(!"New capacity cannot hold old string!");

			// We don't need to set the length of string, we are reallocating the same string but with different capacity

			char* src = m_string;

			m_string = (char*)malloc(capacity);

			if (m_string)
			{
				if (src)
				{
					strcpy(m_string, src);
					free(src);
				}
				m_capacity = (unsigned short)capacity;
			}
		}

		void deallocate()
		{
			if (m_string)
			{
				free(m_string);
				m_string = nullptr;
			}
			m_length = 0;
			m_capacity = 0;
		}

	public:
		String() {}

		String(size_t length) { allocate(length); }

		String(const char* string)
		{
			copyFrom(string);
		}

		String(const char* string, size_t length)
		{
			copyFrom(string, length);
		}

		String(const char* string, int length)
		{
			copyFrom(string, (size_t)length);
		}

		String(const String& other)
		{
			copyFrom(other.m_string, other.m_length);
		}

		String(String&& other) noexcept
		{
			m_string = other.m_string;
			m_length = other.m_length;
			m_capacity = other.m_capacity;

			// Avoid destructing main string
			other.m_string = nullptr;
			other.m_length = 0;
			other.m_length = 0;
		}

		~String()
		{
			deallocate();
		}

		char* strcat(const char* string)
		{
			size_t subStrLength = strlen(string);
			reallocate(m_length + subStrLength + 1);
			if (m_string)
			{
				for (size_t i = 0; i < subStrLength; i++)
					m_string[i + m_length] = string[i];

				m_string[m_length + subStrLength] = 0;
			}

			resize();
			return m_string;
		}

		void append(const char* string)
		{
			strcat(string);
		}

		void append(const Utils::String& string)
		{
			size_t subStrLength = string.length();
			reallocate(m_length + subStrLength + 1);
			if (m_string)
			{
				for (size_t i = 0; i < subStrLength; i++)
					m_string[i + m_length] = string.c_str()[i];

				m_string[m_length + subStrLength] = 0;
			}

			resize();
		}

		String& operator=(const String& other)
		{
			if (this != &other)
			{
				deallocate();
				copyFrom(other.m_string, other.m_length);
			}
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
				other.m_length = 0;
				other.m_capacity = 0;
			}
			return *this;
		}

		String& operator=(const char* str)
		{
			copyFrom(str);
			return *this;
		}

		String& operator+=(const char* str)
		{
			append(str);
			return *this;
		}

		String operator+(const char* str) const
		{
			String result = *this;
			result += str;
			return result;
		}

		char& operator[](size_t index)
		{
			if (index >= m_length)
				assert(!"Index out of range!");

			return m_string[index];
		}

		char& operator[](int index)
		{
			if (index >= m_length)
				assert(!"Index out of range!");

			return m_string[index];
		}

		String& operator/=(const String& lhs)
		{
			if (m_string[m_length - 1] == '\\')
			{
				append(lhs);
				return *this;
			}

			append("\\");
			append(lhs);
			
			return *this;
		}

		String& operator/=(const char* string)
		{
			if (m_string[m_length - 1] == '\\')
			{
				append(string);
				return *this;
			}

			append("\\");
			append(string);

			return *this;
		}

		String operator/(const char* str) const
		{
			String result = *this;

			if (result.m_string[result.m_length - 1] == '\\')
				return result + str;

			return result + "\\" + str;
		}

		String operator/(const String& lhs) const
		{
			String result = *this;

			if (result.m_string[result.m_length - 1] == '\\')
				return result + lhs;

			return result + "\\" + lhs;
		}

		operator bool() const
		{
			return m_length > 0 || m_string != nullptr;
		}

		bool operator==(const String& other) const
		{
			return m_length == other.m_length && strcmp(m_string, other.m_string) == 0;
		}

		bool operator==(const char* lhs) const
		{
			return m_length == strlen(lhs) && strcmp(m_string, lhs) == 0;
		}

		bool operator!=(const char* lhs) const
		{
			return !(*this == lhs);
		}

		bool operator!=(const String& other) const
		{
			return !(*this == other);
		}

		size_t length() const
		{
			return m_length;
		}

		size_t size() const
		{
			return length();
		}

		int format(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);

			int length = formatV(fmt, args);

			va_end(args);

			return length;
		}

		int formatV(const char* fmt, va_list args)
		{
			int length = vsnprintf(nullptr, 0, fmt, args);

			reallocate(length + 1);

			vsnprintf(m_string, m_capacity, fmt, args);

			return length;
		}

		char* data()
		{
			return m_string;
		}

		void resize()
		{
			m_length = (unsigned short)strlen(m_string);
		}

		const char* c_str() const
		{
			return m_string ? m_string : "";
		}

		operator const char* () const
		{
			return c_str();
		}

		size_t capacity() const
		{
			return m_capacity;
		}

		void clear()
		{
			if (m_string)
				m_string[0] = 0;

			m_length = 0;
		}

		bool empty() const
		{
			return m_length == 0;
		}

		void resize(size_t newsize)
		{
			reallocate(newsize);
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

		String substr(size_t pos, size_t len = -1) const
		{
			if (pos > length())
				return "";

			if (len == -1 || pos + len > length())
				len = length() - pos;

			return String(m_string + pos, len);
		}

		const char* lower()
		{
			for (size_t i = 0; i < m_length; i++)
				m_string[i] = tolower(m_string[i]);

			return c_str();
		}

		const char* upper()
		{
			for (size_t i = 0; i < m_length; i++)
				m_string[i] = toupper(m_string[i]);

			return c_str();
		}

		// trims whitespaces from both ends
		const char* trim()
		{
			if (!length())
				return nullptr;

			const char* begin = m_string;
			const char* end = m_string + length() - 1;
			
			while (isspace(*begin)) begin++;
			while (isspace(*end)) end--;

			Utils::String copy = { begin, end - begin };
			*this = copy;

			return c_str();
		}

		void reserve(size_t size)
		{
			reallocate(size);
		}

		void shrink_to_fit()
		{
			resize();
			reallocate(m_length + 1);
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

		strcpy(buff, buffer);

		while (char* chr = strchr(buff, '/'))
			*chr = '\\';

		return buff;
	}

	inline void formatPathGI(char* buffer, size_t bufferSize, const char* data)
	{
		((void(__cdecl*)(char*, size_t, const char*))(shared::base + 0x9F8090))(buffer, bufferSize, data);
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

		char* chr = strrchr(str.data(), '0');

		if (chr)
		{
			while (chr)
			{
				if (!*chr)
					break;

				if (*chr != '0' || chr[-1] == '.')
					break;

				if (chr[1] && (chr[1] != '0' && chr[1] != '.'))
					break;

				*chr = 0;

				chr = strrchr(str.data(), '0');
			}
		}

		str.resize();

		return str;
	}
}

#pragma warning(pop)