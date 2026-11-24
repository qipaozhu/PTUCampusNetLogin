#include "winregistry.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#endif

namespace WinRegistry
{
#ifdef Q_OS_WIN
	namespace Detail
	{
		/// 把根键字符串映射成 HKEY；不认识的一律当作当前用户
		HKEY rootHandle(const QString& root)
		{
			if (root.compare(QLatin1String(kLocalMachine), Qt::CaseInsensitive) == 0)
				return HKEY_LOCAL_MACHINE;
			return HKEY_CURRENT_USER;
		}

		/// QString → wchar_t 缓冲区，调用期间保持有效
		std::wstring wide(const QString& text)
		{
			return text.toStdWString();
		}
	}
#endif

	/// 读 DWORD
	bool readDword(const QString& root, const QString& subKey,
		const QString& valueName, quint32* value)
	{
		if (!value)
			return false;

#ifdef Q_OS_WIN
		DWORD type = 0;
		DWORD data = 0;
		DWORD size = static_cast<DWORD>(sizeof(data));
		const std::wstring key = Detail::wide(subKey);
		const std::wstring name = Detail::wide(valueName);

		const LSTATUS status = RegGetValueW(Detail::rootHandle(root), key.c_str(),
			name.c_str(), RRF_RT_REG_DWORD, &type, &data, &size);
		if (status != ERROR_SUCCESS)
			return false;

		*value = static_cast<quint32>(data);
		return true;
#else
		Q_UNUSED(root)
		Q_UNUSED(subKey)
		Q_UNUSED(valueName)
		return false;
#endif
	}

	/// 写 DWORD
	bool writeDword(const QString& root, const QString& subKey,
		const QString& valueName, quint32 value)
	{
#ifdef Q_OS_WIN
		const std::wstring key = Detail::wide(subKey);
		const std::wstring name = Detail::wide(valueName);

		HKEY handle = nullptr;
		if (RegCreateKeyExW(Detail::rootHandle(root), key.c_str(), 0, nullptr, 0,
			KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &handle, nullptr) != ERROR_SUCCESS)
		{
			return false;
		}

		const DWORD data = static_cast<DWORD>(value);
		const LSTATUS status = RegSetValueExW(handle, name.c_str(), 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&data), static_cast<DWORD>(sizeof(data)));
		RegCloseKey(handle);
		return status == ERROR_SUCCESS;
#else
		Q_UNUSED(root)
		Q_UNUSED(subKey)
		Q_UNUSED(valueName)
		Q_UNUSED(value)
		return false;
#endif
	}

	/// 读字符串
	QString readString(const QString& root, const QString& subKey,
		const QString& valueName)
	{
#ifdef Q_OS_WIN
		const std::wstring key = Detail::wide(subKey);
		const std::wstring name = Detail::wide(valueName);

		DWORD type = 0;
		DWORD size = 0;
		// 第一次只问需要多大缓冲区
		if (RegGetValueW(Detail::rootHandle(root), key.c_str(), name.c_str(),
			RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS || size == 0)
		{
			return QString();
		}

		std::wstring buffer(size / sizeof(wchar_t), L'\0');
		if (RegGetValueW(Detail::rootHandle(root), key.c_str(), name.c_str(),
			RRF_RT_REG_SZ, &type, buffer.data(), &size) != ERROR_SUCCESS)
		{
			return QString();
		}

		// 缓冲区末尾带 \0，转 QString 时去掉
		return QString::fromWCharArray(buffer.c_str());
#else
		Q_UNUSED(root)
		Q_UNUSED(subKey)
		Q_UNUSED(valueName)
		return QString();
#endif
	}

	/// 写字符串
	bool writeString(const QString& root, const QString& subKey,
		const QString& valueName, const QString& value)
	{
#ifdef Q_OS_WIN
		const std::wstring key = Detail::wide(subKey);
		const std::wstring name = Detail::wide(valueName);
		const std::wstring data = Detail::wide(value);

		HKEY handle = nullptr;
		if (RegCreateKeyExW(Detail::rootHandle(root), key.c_str(), 0, nullptr, 0,
			KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &handle, nullptr) != ERROR_SUCCESS)
		{
			return false;
		}

		const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
		const LSTATUS status = RegSetValueExW(handle, name.c_str(), 0, REG_SZ,
			reinterpret_cast<const BYTE*>(data.c_str()), bytes);
		RegCloseKey(handle);
		return status == ERROR_SUCCESS;
#else
		Q_UNUSED(root)
		Q_UNUSED(subKey)
		Q_UNUSED(valueName)
		Q_UNUSED(value)
		return false;
#endif
	}

	/// 删除一个值；值或键本来就不存在也算成功
	bool removeValue(const QString& root, const QString& subKey,
		const QString& valueName)
	{
#ifdef Q_OS_WIN
		const std::wstring key = Detail::wide(subKey);
		const std::wstring name = Detail::wide(valueName);

		HKEY handle = nullptr;
		if (RegOpenKeyExW(Detail::rootHandle(root), key.c_str(), 0,
			KEY_SET_VALUE | KEY_WOW64_64KEY, &handle) != ERROR_SUCCESS)
		{
			// 键都不存在，自然也就不用删了
			return true;
		}

		const LSTATUS status = RegDeleteValueW(handle, name.c_str());
		RegCloseKey(handle);
		return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
#else
		Q_UNUSED(root)
		Q_UNUSED(subKey)
		Q_UNUSED(valueName)
		return true;
#endif
	}
}