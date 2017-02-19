#include <ntddk.h>
#include <Ntstrsafe.h>
#include <ntddkbd.h>
#include "driver.h"

int g_kDriverType = kDriverType_Callback;

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING regPath)
{
	UNREFERENCED_PARAMETER(regPath);
	if (g_kDriverType == kDriverType_Filter) {
		driver_object->DriverUnload = classDriverUnload;
		for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
			driver_object->MajorFunction[i] = &classDriverDispatch;
		}
		return HookKdbClass(driver_object);
	} else if (g_kDriverType == kDriverType_Mj) {
		driver_object->DriverUnload = &mjDriverUnload;
		return HookMajorFunction(driver_object);
	} else if (g_kDriverType == kDriverType_Callback) {
		driver_object->DriverUnload = &callbackDriverUnload;
		return HookServiceCallbackFunc();
	}
	return STATUS_UNSUCCESSFUL;
}
