#include <ntddk.h>
#include <Ntstrsafe.h>
#include <ntddkbd.h>
#include "driver.h"

PDriverDispatchFuncPtr g_old_readptr = NULL;
PDriverDispatchFuncPtr *g_old_preadptr = NULL;

NTSTATUS mjDriverRead(PDEVICE_OBJECT dev_object, PIRP irp)
{
	PIO_STACK_LOCATION irpSp;
	irpSp = IoGetCurrentIrpStackLocation(irp);
	irpSp->CompletionRoutine = &KbdReadCompleted;
	irpSp->Context = NULL;
	irpSp->Control = SL_INVOKE_ON_SUCCESS;
	g_keyNum++;
	return g_old_readptr(dev_object, irp);
}

VOID mjDriverUnload(PDRIVER_OBJECT obj)
{
	UNREFERENCED_PARAMETER(obj);
	LARGE_INTEGER delay = RtlConvertLongToLargeInteger(100 * DELAY_ONE_MILLISECOND);
	*g_old_preadptr = g_old_readptr;
	while (g_keyNum) {
		KeDelayExecutionThread(KernelMode, TRUE, &delay);
	}
}

NTSTATUS HookMajorFunction(PDRIVER_OBJECT driver_object)
{
	UNREFERENCED_PARAMETER(driver_object);
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING driver_name;
	RtlInitUnicodeString(&driver_name, g_className);
	PDRIVER_OBJECT kbd_driver_object;
	status = ObReferenceObjectByName(&driver_name, OBJ_CASE_INSENSITIVE, NULL,
		0, *IoDriverObjectType, KernelMode, 0, &kbd_driver_object);
	if (!NT_SUCCESS(status)) {
		DbgPrint("cannot get kbdclass object");
		return status;
	}
	ObDereferenceObject(kbd_driver_object);
	g_old_readptr = kbd_driver_object->MajorFunction[IRP_MJ_READ];
	g_old_preadptr = &kbd_driver_object->MajorFunction[IRP_MJ_READ];
#pragma warning(push)
#pragma warning(disable:4152)
	InterlockedExchangePointer((PVOID*)&kbd_driver_object->MajorFunction[IRP_MJ_READ], &mjDriverRead);
#pragma warning(pop)
	return STATUS_SUCCESS;
}
