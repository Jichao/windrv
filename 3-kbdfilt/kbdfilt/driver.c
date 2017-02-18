#include <ntddk.h>
#include <Ntstrsafe.h>

struct kbdFiltExt {
	PDEVICE_OBJECT next_object;
	PIRP irp;
};

PDEVICE_OBJECT s_filter_objects[10] = { 0 };

extern POBJECT_TYPE* IoDriverObjectType;
extern NTSYSAPI
NTSTATUS NTAPI ObReferenceObjectByName(
	IN PUNICODE_STRING ObjectPath,
	IN ULONG Attributes,
	IN PACCESS_STATE PassedAccessState OPTIONAL,
	IN ACCESS_MASK DesiredAccess OPTIONAL,
	IN POBJECT_TYPE ObjectType,
	IN KPROCESSOR_MODE AccessMode,
	IN OUT PVOID ParseContext OPTIONAL,
	OUT PVOID *ObjectPtr
);

NTSTATUS jjAttachDevice(PDRIVER_OBJECT driver_object, PDEVICE_OBJECT target_object, ULONG extension_size,
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
	} else {
		*ppfilter_object = filter_object;
	}
	filter_object->Flags = filter_object->Flags & ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}

VOID DriverUnload(PDRIVER_OBJECT obj)
{
	UNREFERENCED_PARAMETER(obj);
	for (int i = 0; i < 10; ++i) {
		if (s_filter_objects[i]) {
			struct kbdFiltExt* ext = (struct kbdFiltExt*)s_filter_objects[i]->DeviceExtension;
			IoCancelIrp(ext->irp); //is it safe?
			IoDetachDevice(ext->next_object);
			IoDeleteDevice(s_filter_objects[i]);
		}
	}
}

NTSTATUS KbdReadCompleted(PDEVICE_OBJECT device_object, PIRP irp, PVOID context) 
{
	UNREFERENCED_PARAMETER(device_object);
	UNREFERENCED_PARAMETER(context);
	PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
	DbgPrint("read length = %d", isl->Parameters.Read.Length);
	for (ULONG i = 0; i < isl->Parameters.Read.Length; ++i) {
		DbgPrint("read: %2x", ((UCHAR*)irp->AssociatedIrp.SystemBuffer)[i]);
	}
	if (irp->PendingReturned)
		IoMarkIrpPending(irp);
	return STATUS_SUCCESS;
}

NTSTATUS DriverDispatch(PDEVICE_OBJECT dev_obj, PIRP irp)
{
	PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
	for (int i = 0; i < 10; ++i) {
		while (dev_obj == s_filter_objects[i]) {
			struct kbdFiltExt* ext = (struct kbdFiltExt*)s_filter_objects[i]->DeviceExtension;
			if (isl->MajorFunction == IRP_MJ_POWER) {
				PoStartNextPowerIrp(irp);
				IoSkipCurrentIrpStackLocation(irp);
				return PoCallDriver(ext->next_object, irp);
			} else if (isl->MajorFunction == IRP_MJ_READ) {
				ext->irp = irp;
				IoCopyCurrentIrpStackLocationToNext(irp);
				IoSetCompletionRoutine(irp, &KbdReadCompleted, NULL, TRUE, FALSE, FALSE);
				return IoCallDriver(ext->next_object, irp);
			} else {
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

NTSTATUS HookKdbClass(PDRIVER_OBJECT driver_object)
{
	UNREFERENCED_PARAMETER(driver_object);
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING driver_name;
	RtlInitUnicodeString(&driver_name, L"\\Driver\\kbdclass");
	PDRIVER_OBJECT kbd_driver_object;
	status = ObReferenceObjectByName(&driver_name, OBJ_CASE_INSENSITIVE, NULL,
		0, NULL, KernelMode, *IoDriverObjectType, &kbd_driver_object);
	if (!NT_SUCCESS(status)) {
		DbgPrint("cannot get kbdclass object");
		return status;
	}
	int device_count = 0; 
	for (PDEVICE_OBJECT device_object = kbd_driver_object->DeviceObject;
		device_object != NULL; device_object = device_object->NextDevice) {
		if (NT_SUCCESS(HookKdbClass(driver_object))) {
			device_count++;
		}
	}
	if (!device_count)
		return STATUS_UNSUCCESSFUL;
	return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING regPath)
{
	UNREFERENCED_PARAMETER(regPath);
	driver_object->DriverUnload = DriverUnload;
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		driver_object->MajorFunction[i] = &DriverDispatch;
	}
	HookKdbClass(driver_object);
	return STATUS_SUCCESS;
}
