﻿#pragma once
#include "FileStream.h"

class BinaryWriter
{
public:
	explicit BinaryWriter(FileStream& Stream);

	void Write(const void* Data, size_t SizeInBytes) const;

	template<typename T>
	void Write(const T& Object) const
	{
		Write(&Object, sizeof(T));
	}

	template<>
	void Write<std::string>(const std::string& Object) const
	{
		Write(Object.data(), static_cast<UINT>(Object.size() * sizeof(char)));
	}

	template<>
	void Write<std::wstring>(const std::wstring& Object) const
	{
		Write(Object.data(), static_cast<UINT>(Object.size() * sizeof(wchar_t)));
	}

private:
	FileStream& Stream;
};
