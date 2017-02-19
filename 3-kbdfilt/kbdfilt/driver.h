#pragma once

#include <ntddk.h>
#include <Ntstrsafe.h>
#include <ntddkbd.h>

//system struct
extern POBJECT_TYPE *IoDriverObjectType;
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

typedef NTSTATUS(*PDriverDispatchFuncPtr)(PDEVICE_OBJECT dev_obj, PIRP irp);

typedef VOID(*PKeyboardClassServiceCallbackFuncPtr)(
	IN PDEVICE_OBJECT DeviceObject,
	IN PKEYBOARD_INPUT_DATA InputDataStart,
	IN PKEYBOARD_INPUT_DATA InputDataEnd,
	IN OUT PULONG InputDataConsumed);

//my own struct
extern WCHAR g_className[];
extern volatile LONG g_keyNum;

#define  DELAY_ONE_MICROSECOND  (-10)
#define  DELAY_ONE_MILLISECOND (DELAY_ONE_MICROSECOND*1000)
#define  DELAY_ONE_SECOND (DELAY_ONE_MILLISECOND*1000)

void DecodeScancode(PVOID buffer, ULONG size);
void print_keystroke(UCHAR sch);

NTSTATUS jjAttachDevice(PDRIVER_OBJECT driver_object,
	PDEVICE_OBJECT target_object, ULONG extension_size,
	PDEVICE_OBJECT* ppfilter_object);

enum {
	kDriverType_Filter,
	kDriverType_Mj,
	kDriverType_Callback
};

//filter driver
NTSTATUS KbdReadCompleted(PDEVICE_OBJECT device_object, PIRP irp, PVOID context);
NTSTATUS classDriverDispatch(PDEVICE_OBJECT dev_obj, PIRP irp);
VOID classDriverUnload(PDRIVER_OBJECT obj);
NTSTATUS HookKdbClass(PDRIVER_OBJECT driver_object);

//mj driver
VOID mjDriverUnload(PDRIVER_OBJECT obj);
NTSTATUS HookMajorFunction(PDRIVER_OBJECT driver_object);

//callback driver
VOID callbackDriverUnload(PDRIVER_OBJECT obj);
NTSTATUS HookServiceCallbackFunc();
