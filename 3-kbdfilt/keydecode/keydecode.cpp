// keydecode.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

typedef struct _KEYBOARD_INPUT_DATA {
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    ULONG ExtraInformation;
} KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;

int main()
{
	FILE *fp;
	long file_size;
	char *buffer;

	fopen_s(&fp, R"(C:\Users\jichao\Desktop\keylog.txt)", "rb");
	if (NULL == fp) {
		printf("cannot open the log");
		return -1;
	}

	fseek(fp, 0L, SEEK_END);
	file_size = ftell(fp);
	buffer = (char*)malloc(file_size);
	fseek(fp, 0L, SEEK_SET);
	fread(buffer, file_size, 1, fp);
	fclose(fp);

	while (file_size > 0) {
		PKEYBOARD_INPUT_DATA pkid = (PKEYBOARD_INPUT_DATA)buffer;
		UINT vkey = MapVirtualKey(pkid->MakeCode, MAPVK_VSC_TO_VK);
		WCHAR ch = LOWORD(MapVirtualKey(vkey, MAPVK_VK_TO_CHAR));
		printf("makecode = %d, ch = %wc\n", pkid->MakeCode, ch);
		buffer += sizeof(KEYBOARD_INPUT_DATA);
		file_size -= sizeof(KEYBOARD_INPUT_DATA);
	}
    return 0;
}