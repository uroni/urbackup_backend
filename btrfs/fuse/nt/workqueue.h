#pragma once

#include "windef.h"

typedef struct _IO_WORKITEM {
    PDEVICE_OBJECT DeviceObject;
} IO_WORKITEM;
typedef IO_WORKITEM* PIO_WORKITEM;

typedef void(IO_WORKITEM_ROUTINE)(PDEVICE_OBJECT DeviceObject, PVOID Context);
typedef IO_WORKITEM_ROUTINE* PIO_WORKITEM_ROUTINE;

typedef enum _WORK_QUEUE_TYPE {
    DelayedWorkQueue
} WORK_QUEUE_TYPE;

PIO_WORKITEM IoAllocateWorkItem(PDEVICE_OBJECT DeviceObject);

void IoQueueWorkItem(PIO_WORKITEM WorkItem, PIO_WORKITEM_ROUTINE Routine, WORK_QUEUE_TYPE QueueType, PVOID Context);

void IoFreeWorkItem(PIO_WORKITEM WorkItem);

void ExQueueWorkItem(PWORK_QUEUE_ITEM Wqi, WORK_QUEUE_TYPE Type);