#include "DocRequestHandle.h"
#include <atomic>

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocRequestHandle)

namespace DocRequestHandle::Private
{
	static std::atomic<int64> GNextOperationId{ 0 };
	static std::atomic<int32> GNextEpoch{ 0 };
}

int64 FDocHandleAllocator::NextOperationId()
{
	return DocRequestHandle::Private::GNextOperationId.fetch_add(1, std::memory_order_relaxed) + 1;
}

int32 FDocHandleAllocator::NextEpoch()
{
	return DocRequestHandle::Private::GNextEpoch.fetch_add(1, std::memory_order_relaxed) + 1;
}
