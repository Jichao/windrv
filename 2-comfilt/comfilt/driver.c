#include <ntddk.h>
#include <Ntstrsafe.h>

NTSTATUS jjAttachDeviceByPointer(PDRIVER_OBJECT driver_object, PDEVICE_OBJECT target_object,
	PDEVICE_OBJECT* ppfilter_object, PDEVICE_OBJECT* pptop_dev)
{
	if ( !target_object || !ppfilter_object || !pptop_dev) {
		return STATUS_INVALID_PARAMETER;
	}

	PDEVICE_OBJECT filter_object;
	NTSTATUS status = STATUS_SUCCESS;

	status = IoCreateDevice(driver_object, 0, NULL,
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

	*pptop_dev = IoAttachDeviceToDeviceStack(filter_object, target_object);
	if (!*pptop_dev) {
		*ppfilter_object = NULL;
		IoDeleteDevice(filter_object);
		status = STATUS_UNSUCCESSFUL;
		return status;
	} else {
		*ppfilter_object = filter_object;
	}
	filter_object->Flags = filter_object->Flags & ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}

#define kPortMaxNum 32
#define CCP_MAX_COM_ID 32

PDEVICE_OBJECT s_fltobj[kPortMaxNum] = { 0 };
PDEVICE_OBJECT s_nextobj[kPortMaxNum] = { 0 };

VOID DriverUnload(PDRIVER_OBJECT obj)
{
	UNREFERENCED_PARAMETER(obj);
	for (auto i = 0; i < kPortMaxNum; ++i) {
		if (s_fltobj[i]) {
			IoDetachDevice(s_nextobj[i]);
			IoDeleteDevice(s_fltobj[i]);
		}
	}
}

NTSTATUS DriverDispatch(PDEVICE_OBJECT dev_obj, PIRP irp)
{
	PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
	for (int i = 0; i < kPortMaxNum; ++i) {
		while (dev_obj == s_fltobj[i]) {
			if (isl->MajorFunction == IRP_MJ_POWER) {
				PoStartNextPowerIrp(irp);
				IoSkipCurrentIrpStackLocation(irp);
				return PoCallDriver(s_nextobj[i], irp);
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
					KdPrint(("serial port %d: char: %x", i, buffer[j]));
				}
			}
			IoSkipCurrentIrpStackLocation(irp);
			return IoCallDriver(s_nextobj[i], irp);
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
		RtlStringCchPrintfW(name, 256, L"\\Device\\Serial%d", i);
		UNICODE_STRING u8_name;
		RtlInitUnicodeString(&u8_name, name);
		PFILE_OBJECT file_object;
		PDEVICE_OBJECT target_object;
		status = IoGetDeviceObjectPointer(&u8_name, FILE_ALL_ACCESS, &file_object, &target_object);
		if (!NT_SUCCESS(status))
			continue;

		status = jjAttachDeviceByPointer(driver_object, target_object, &s_fltobj[i], &s_nextobj[i]);
		ObDereferenceObject(file_object);
		if (NT_SUCCESS(status)) {
			success_count++;
		}
	}
	
	if (!success_count)
		return status;
	return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING regPath)
{
	UNREFERENCED_PARAMETER(regPath);

	driver_object->DriverUnload = DriverUnload;
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		driver_object->MajorFunction[i] = &DriverDispatch;
	}
	HookSerialPorts(driver_object);
	return STATUS_SUCCESS;
}
