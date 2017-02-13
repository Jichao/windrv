// r3.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>

#define kCopyFile CTL_CODE(FILE_DEVICE_UNKNOWN, 0x999, METHOD_BUFFERED, FILE_WRITE_DATA)

int main()
{
	HANDLE hFile = CreateFile(L"\\\\.\\FirstHelloWorld", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);
	if (hFile == INVALID_HANDLE_VALUE) {
		printf("cannot create file\n");
		return -1;
	}

	wchar_t buffer[] = LR"(\??\c:\keke\haha.txt|\??\c:\keke\haha2.txt)";
	DWORD bytes;
	if (!DeviceIoControl(hFile, kCopyFile, buffer, sizeof(buffer), NULL, 0, &bytes, NULL)) {
		printf("error code: %d\n", GetLastError());
	}
	CloseHandle(hFile);
    return 0;
}