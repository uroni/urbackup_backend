extern "C"
{
#include <workqueue.h>
}
#include <queue>

namespace
{
	struct SWorkItem
	{
		PIO_WORKITEM WorkItem;
		PIO_WORKITEM_ROUTINE Routine;
		PVOID Context;
	};

	std::queue<SWorkItem> work;
}

PIO_WORKITEM IoAllocateWorkItem(PDEVICE_OBJECT DeviceObject)
{
	PIO_WORKITEM ret = new IO_WORKITEM;

	ret->DeviceObject = DeviceObject;

	return ret;
}

void IoQueueWorkItem(PIO_WORKITEM WorkItem, PIO_WORKITEM_ROUTINE Routine, WORK_QUEUE_TYPE QueueType, PVOID Context)
{
	Routine(WorkItem->DeviceObject, Context);
	/*
	SWorkItem item;
	item.WorkItem = WorkItem;
	item.Routine = Routine;
	item.Context = Context;

	work.push(item);*/
}

void IoFreeWorkItem(PIO_WORKITEM WorkItem)
{
	delete WorkItem;
}

void ExQueueWorkItem(PWORK_QUEUE_ITEM Wqi, WORK_QUEUE_TYPE Type)
{
}
