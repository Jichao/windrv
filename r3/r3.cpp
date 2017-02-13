// r3.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>

#define kCopyFile CTL_CODE(FILE_DEVICE_UNKNOWN, 0x999, METHOD_BUFFERED, FILE_WRITE_DATA)

int main()
{
	HANDLE hFile = CreateFile(L"\\\\.\\FirstHelloWorld", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, 0);
	//if (hFile == INVALID_HANDLE_VALUE) {
	//	printf("cannot create file\n");
	//	return -1;
	//}
	wchar_t buffer[] = LR"(c:\keke\haha.txt|c:\keke\haha2.txt)";
	wchar_t* next_token = NULL;
	LPCWSTR src_filename = wcstok_s(buffer, L"|", &next_token);
	printf("src file name %ws", src_filename);
	LPCWSTR dst_filename = wcstok_s(NULL, L"|", &next_token);
	printf("dst file name %ws", dst_filename);

	DWORD bytes;
	auto status = DeviceIoControl(hFile, kCopyFile, buffer, sizeof(buffer), NULL, 0, &bytes, NULL);
	CloseHandle(hFile);
    return status;
}