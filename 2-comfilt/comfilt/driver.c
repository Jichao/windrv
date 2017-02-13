#include <ntddk.h>
#include <Ntstrsafe.h>
#include "basecore/driverutils.h"

#define kMemTag 2333
#define kPortMaxNum 32

PDEVICE_OBJECT g_target_objects[kPortMaxNum] = { 0 };
PDEVICE_OBJECT g_filter_objects[kPortMaxNum] = { 0 };
PDEVICE_OBJECT g_next_objects[kPortMaxNum] = { 0 };

VOID DriverUnload(PDRIVER_OBJECT obj)
{
	UNREFERENCED_PARAMETER(obj);
	for (auto i = 0; i < kPortMaxNum; ++i) {
		if (g_filter_objects[i]) {
			IoDetachDevice(g_target_objects[i]);
			IoDeleteDevice(g_filter_objects[i]);
		}
	}
}

NTSTATUS DriverDispatch(PDEVICE_OBJECT dev_obj, PIRP irp)
{
	if (!dev_obj || !irp)
		return STATUS_INVALID_PARAMETER;

	PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
	for (int i = 0; i < kPortMaxNum; ++i) {
		while (dev_obj == g_filter_objects[i]) {
			if (isl->MajorFunction == IRP_MJ_POWER) {
				PoStartNextPowerIrp(irp);
				IoSkipCurrentIrpStackLocation(irp);
				return PoCallDriver(g_next_objects[i], irp);
			} else if (isl->MajorFunction == IRP_MJ_WRITE) {
				char* buffer = NULL;
				if (irp->MdlAddress) {
					buffer = (char*)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
				} else if (irp->UserBuffer) {
					buffer = (char*)irp->UserBuffer;
				} else {
					buffer = (char*)irp->AssociatedIrp.SystemBuffer;
				}
				for (ULONG j = 0; j < isl->Parameters.Write.Length; ++j) {
					KdPrint(("serial port %d: char: %x", i, buffer[i]));
				}
			}
			IoSkipCurrentIrpStackLocation(irp);
			return PoCallDriver(g_next_objects[i], irp);
		}
	}

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS HookSerialPorts(PDRIVER_OBJECT driver_object)
{
	wchar_t name[256] = { 0 };
	NTSTATUS status;

	int success_count = 0;
	for (int i = 0; i < kPortMaxNum; i++) {
		PFILE_OBJECT file_object = NULL;
		UNICODE_STRING u8_device_name = { 0 };
		PDEVICE_OBJECT target_object = NULL;

		RtlStringCchPrintfW(name, 256, L"\\Device\\Serial%d", i);
		RtlInitUnicodeString(&u8_device_name, name);
		status = IoGetDeviceObjectPointer(&u8_device_name, FILE_ALL_ACCESS, &file_object, &target_object);
		if (!NT_SUCCESS(status)) {
			continue;
		}
		ObReferenceObject(target_object);
		ObDereferenceObject(&file_object);

		status = jjAttachDeviceByPointer(driver_object, target_object, &g_filter_objects[i], &g_next_objects[i]);
		if (!NT_SUCCESS(status)) {
			ObDereferenceObject(target_object);
			target_object = NULL;
			continue;
		}

		g_target_objects[i] = target_object;
		success_count++;
	}
	
	if (!success_count)
		return STATUS_FLT_DO_NOT_ATTACH;
	return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING regPath)
{
	UNREFERENCED_PARAMETER(regPath);

	driver_object->DriverUnload = DriverUnload;
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		driver_object->MajorFunction[i] = &DriverDispatch;
	}
	return HookSerialPorts(driver_object);
}
