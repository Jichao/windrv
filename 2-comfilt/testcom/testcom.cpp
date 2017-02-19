// testcom.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>
#include <strsafe.h>

int main(int argc, wchar_t** argv)
{
	wchar_t name[20];
	StringCchPrintf(name, 20, L"\\\\.\\COM%s", argv[1]);
	HANDLE hcom = CreateFile(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, NULL, NULL);
	if (hcom == INVALID_HANDLE_VALUE) {
		printf("failed to open com, error code = %x\n", GetLastError());
		return -1;
	}
	DWORD bytes;
	if (!WriteFile(hcom, L"keke", 8, &bytes, NULL) || bytes != 8) {
		printf("failed to write com, error code = %x\n", GetLastError());
		CloseHandle(hcom);
		return -1;
	}
	CloseHandle(hcom);
    return 0;
}

