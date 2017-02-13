#include <ntddk.h>

#define kMemTag 2333
#define kControlDeviceName L"\\Device\\FirstHelloWorld"
#define kSymbolName L"\\??\\FirstHelloWorld"

#define kCopyFile CTL_CODE(FILE_DEVICE_UNKNOWN, 0x999, METHOD_BUFFERED, FILE_WRITE_DATA)

PDEVICE_OBJECT g_cdo = NULL;

wchar_t *wcstok_s(wchar_t * s1, const wchar_t * s2, wchar_t ** ptr) {
	wchar_t *p;

	if (s1 == NULL)
		s1 = *ptr;
	while (*s1 && wcschr(s2, *s1))
		s1++;
	if (!*s1) {
		*ptr = s1;
		return NULL;
	}
	for (p = s1; *s1 && !wcschr(s2, *s1); s1++)
		continue;
	if (*s1)
		*s1++ = L'\0';
	*ptr = s1;
	return p;
}

NTSTATUS MyCopyFile(PUNICODE_STRING dstFilename, PUNICODE_STRING srcFilename);
VOID DriverUnload(PDRIVER_OBJECT obj)
{
	UNREFERENCED_PARAMETER(obj);
	KdPrint(("current process id: %d, current thread id: %d\n", PsGetCurrentProcessId(), PsGetCurrentThreadId()));

	UNICODE_STRING symbol_name = RTL_CONSTANT_STRING(kSymbolName);
	IoDeleteSymbolicLink(&symbol_name);
	IoDeleteDevice(g_cdo);
}

NTSTATUS DriverDispatch(PDEVICE_OBJECT dev_obj, PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
	while (dev_obj == g_cdo) {
		if (isl->MajorFunction == IRP_MJ_CREATE || isl->MajorFunction == IRP_MJ_CLOSE)
			break;

		if (isl->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
			if (isl->Parameters.DeviceIoControl.IoControlCode != kCopyFile) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			wchar_t* next_token = NULL;
			LPCWSTR src_filename = wcstok_s(irp->AssociatedIrp.SystemBuffer, L"|", &next_token);
			KdPrint(("src file name %ws", src_filename));
			if (!src_filename) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			LPCWSTR dst_filename = wcstok_s(NULL, L"|", &next_token);
			KdPrint(("dst file name %ws", dst_filename));
			if (!dst_filename) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			UNICODE_STRING ustr_src_filename = { 0 };
			UNICODE_STRING ustr_dst_filename = { 0 };
			RtlInitUnicodeString(&ustr_src_filename, src_filename);
			RtlInitUnicodeString(&ustr_dst_filename, dst_filename);
			status = MyCopyFile(&ustr_dst_filename, &ustr_src_filename);
		}
		break;
	}

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT obj, PUNICODE_STRING regPath)
{
	UNREFERENCED_PARAMETER(regPath);
	KdPrint(("current process id: %d, current thread id: %d\n", PsGetCurrentProcessId(), PsGetCurrentThreadId()));

	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING device_name = RTL_CONSTANT_STRING(kControlDeviceName);
	UNICODE_STRING symbol_name = RTL_CONSTANT_STRING(kSymbolName);

	status = IoCreateDevice(obj, 0, &device_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_cdo);
	if (!NT_SUCCESS(status)) {
		KdPrint(("create device failed"));
		goto out;
	}

	status = IoCreateSymbolicLink(&symbol_name, &device_name);
	if (!NT_SUCCESS(status)) {
		KdPrint(("create symbol link failed"));
		goto out;
	}

	obj->DriverUnload = DriverUnload;
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		obj->MajorFunction[i] = &DriverDispatch;
	}

out:
	return status;
}

NTSTATUS MyCopyFile(PUNICODE_STRING dstFilename, PUNICODE_STRING srcFilename)
{
	HANDLE src_file_handle = NULL;
	HANDLE dst_file_handle = NULL;
	NTSTATUS status = STATUS_SUCCESS;
	PVOID buffer = NULL;

	{
		OBJECT_ATTRIBUTES oa;
		InitializeObjectAttributes(&oa, srcFilename, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
		IO_STATUS_BLOCK isb;
		status = ZwCreateFile(
			&src_file_handle,
			GENERIC_READ,
			&oa,
			&isb,
			NULL,
			FILE_ATTRIBUTE_NORMAL,
			FILE_SHARE_READ,
			FILE_OPEN,
			FILE_SYNCHRONOUS_IO_NONALERT | FILE_RANDOM_ACCESS | FILE_NON_DIRECTORY_FILE,
			NULL,
			0);
		if (status != STATUS_SUCCESS) {
			goto out;
		}
	}

	{
		OBJECT_ATTRIBUTES oa;
		InitializeObjectAttributes(&oa, dstFilename, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
		IO_STATUS_BLOCK isb;
		status = ZwCreateFile(&dst_file_handle,
			GENERIC_WRITE,
			&oa,
			&isb,
			NULL,
			FILE_ATTRIBUTE_NORMAL,
			0,
			FILE_SUPERSEDE,
			FILE_SYNCHRONOUS_IO_NONALERT | FILE_RANDOM_ACCESS | FILE_NON_DIRECTORY_FILE,
			NULL,
			0);
		if (status != STATUS_SUCCESS) {
			goto out;
		}
	}

	IO_STATUS_BLOCK isb;
	buffer = ExAllocatePoolWithTag(NonPagedPool, 4 * 1024, kMemTag);
	if (!buffer) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	LARGE_INTEGER offset = { 0 };
	while (1) {
		ULONG len = 4 * 1024;
		status = ZwReadFile(src_file_handle, NULL, NULL, NULL, &isb, buffer, len, &offset, NULL);
		if (!NT_SUCCESS(status)) {
			if (status == STATUS_END_OF_FILE)
				status = STATUS_SUCCESS;
			goto out;
		}
		len = (ULONG)isb.Information;
		status = ZwWriteFile(dst_file_handle, NULL, NULL, NULL, &isb, buffer, len, &offset, NULL);
		if (!NT_SUCCESS(status)) {
			goto out;
		}
		offset.QuadPart += len;
	}

out:
	if (buffer) {
		ExFreePoolWithTag(buffer, kMemTag);
	}

	if (src_file_handle) {
		ZwClose(src_file_handle);
	}
	if (dst_file_handle) {
		ZwClose(dst_file_handle);
	}
	return status;
}

