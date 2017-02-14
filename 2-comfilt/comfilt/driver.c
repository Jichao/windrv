#include <ntddk.h>
#include <Ntstrsafe.h>
#include "basecore/driverutils.h"

#define kPortMaxNum 32
#define CCP_MAX_COM_ID 32

PDEVICE_OBJECT s_fltobj[kPortMaxNum] = { 0 };
PDEVICE_OBJECT s_nextobj[kPortMaxNum] = { 0 };

// 打开一个端口设备
PDEVICE_OBJECT ccpOpenCom(ULONG id, NTSTATUS *status)
{
	UNICODE_STRING name_str;
	static WCHAR name[32] = { 0 };
	PFILE_OBJECT fileobj = NULL;
	PDEVICE_OBJECT devobj = NULL;

	// 输入字符串。
	memset(name, 0, sizeof(WCHAR) * 32);
	RtlStringCchPrintfW(
		name, 32,
		L"\\Device\\Serial%d", id);
	RtlInitUnicodeString(&name_str, name);

	// 打开设备对象
	*status = IoGetDeviceObjectPointer(&name_str, FILE_ALL_ACCESS, &fileobj, &devobj);
	if (*status == STATUS_SUCCESS)
		ObDereferenceObject(fileobj);

	return devobj;
}

NTSTATUS
ccpAttachDevice(
	PDRIVER_OBJECT driver,
	PDEVICE_OBJECT oldobj,
	PDEVICE_OBJECT *fltobj,
	PDEVICE_OBJECT *next)
{
	NTSTATUS status;
	PDEVICE_OBJECT topdev = NULL;

	// 生成设备，然后绑定之。
	status = IoCreateDevice(driver,
		0,
		NULL,
		oldobj->DeviceType,
		0,
		FALSE,
		fltobj);

	if (status != STATUS_SUCCESS)
		return status;

	// 拷贝重要标志位。
	if (oldobj->Flags & DO_BUFFERED_IO)
		(*fltobj)->Flags |= DO_BUFFERED_IO;
	if (oldobj->Flags & DO_DIRECT_IO)
		(*fltobj)->Flags |= DO_DIRECT_IO;
	if (oldobj->Flags & DO_BUFFERED_IO)
		(*fltobj)->Flags |= DO_BUFFERED_IO;
	if (oldobj->Characteristics & FILE_DEVICE_SECURE_OPEN)
		(*fltobj)->Characteristics |= FILE_DEVICE_SECURE_OPEN;
	(*fltobj)->Flags |= DO_POWER_PAGABLE;
	// 绑定一个设备到另一个设备上
	topdev = IoAttachDeviceToDeviceStack(*fltobj, oldobj);
	if (topdev == NULL)
	{
		// 如果绑定失败了，销毁设备，重新来过。
		IoDeleteDevice(*fltobj);
		*fltobj = NULL;
		status = STATUS_UNSUCCESSFUL;
		return status;
	}
	*next = topdev;

	// 设置这个设备已经启动。
	(*fltobj)->Flags = (*fltobj)->Flags & ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}

// 这个函数绑定所有的串口。
void ccpAttachAllComs(PDRIVER_OBJECT driver)
{
	ULONG i;
	PDEVICE_OBJECT com_ob;
	NTSTATUS status;
	for (i = 0; i < CCP_MAX_COM_ID; i++)
	{
		// 获得object引用。
		com_ob = ccpOpenCom(i, &status);
		if (com_ob == NULL)
			continue;
		// 在这里绑定。并不管绑定是否成功。
		ccpAttachDevice(driver, com_ob, &s_fltobj[i], &s_nextobj[i]);
		// 取消object引用。
	}
}



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

NTSTATUS ccpDriverDispatch(PDEVICE_OBJECT dev_obj, PIRP irp)
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
					KdPrint(("serial port %d: char: %x", i, buffer[i]));
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

NTSTATUS DriverDispatch(PDEVICE_OBJECT device, PIRP irp)
{
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(irp);
	ULONG i, j;

	// 首先得知道发送给了哪个设备。设备一共最多CCP_MAX_COM_ID
	// 个，是前面的代码保存好的，都在s_fltobj中。
	for (i = 0; i < CCP_MAX_COM_ID; i++)
	{
		if (s_fltobj[i] == device)
		{
			// 所有电源操作，全部直接放过。
			if (irpsp->MajorFunction == IRP_MJ_POWER)
			{
				// 直接发送，然后返回说已经被处理了。
				PoStartNextPowerIrp(irp);
				IoSkipCurrentIrpStackLocation(irp);
				return PoCallDriver(s_nextobj[i], irp);
			}
			// 此外我们只过滤写请求。写请求的话，获得缓冲区以及其长度。
			// 然后打印一下。
			if (irpsp->MajorFunction == IRP_MJ_WRITE)
			{
				// 如果是写，先获得长度
				ULONG len = irpsp->Parameters.Write.Length;
				// 然后获得缓冲区
				PUCHAR buf = NULL;
				if (irp->MdlAddress != NULL)
					buf =
					(PUCHAR)
					MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
				else
					buf = (PUCHAR)irp->UserBuffer;
				if (buf == NULL)
					buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;

				// 打印内容
				for (j = 0; j < len; ++j)
				{
					DbgPrint("comcap: Send Data: %2x\r\n",
						buf[j]);
				}
			}

			// 这些请求直接下发执行即可。我们并不禁止或者改变它。
			IoSkipCurrentIrpStackLocation(irp);
			return IoCallDriver(s_nextobj[i], irp);
		}
	}

	// 如果根本就不在被绑定的设备中，那是有问题的，直接返回参数错误。
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}


NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING regPath)
{
	UNREFERENCED_PARAMETER(regPath);

	driver_object->DriverUnload = DriverUnload;
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		driver_object->MajorFunction[i] = &DriverDispatch;
	}
	ccpAttachAllComs(driver_object);
	return STATUS_SUCCESS;
	//return HookSerialPorts(driver_object);
}
