#include "tracerCacheListener.h"

#include "../tracercomponent/customTracer.h"


TracerCacheListener::TracerCacheListener(ComponentId_t id, Params &params) : CacheListener(id, params) {
    out = new Output("TracerCacheListener[@f:@l:@p] ", 1, 0, Output::STDOUT);
    out->verbose(CALL_INFO, 1, 0, "Tracer CacheListener was constructed for Cache '%s'.\n", getParentComponentName().c_str());
}

void TracerCacheListener::notifyAccess(const MemHierarchy::CacheListenerNotification& notify) {
    const MemHierarchy::NotifyAccessType accessType = notify.getAccessType();
    const MemHierarchy::NotifyResultType resultType = notify.getResultType();
    const MemHierarchy::Addr addr = notify.getPhysicalAddress();
    const SST::Event::id_type id = notify.getEventID();

    // Hardware prefetcher speculatively loaded this address — record it.
    // When the CPU later demands it, we can flag that access as a prefetch hit.
    if (accessType == MemHierarchy::PREFETCH && resultType == MemHierarchy::MISS) {
        std::lock_guard<std::mutex> lock(prefetchedCacheLinesMutex);
        prefetchedCacheLines.insert(addr);
    }

    // Demand read or write hit — check if the line was prefetched.
    if ((accessType == MemHierarchy::READ || accessType == MemHierarchy::WRITE) && resultType == MemHierarchy::HIT) {
        std::lock_guard<std::mutex> lock(prefetchedCacheLinesMutex);
        if (prefetchedCacheLines.count(addr)) {
            CustomTracer::markAsPrefetched(id);
            // Remove — the line has been demanded, no longer just prefetched.
            prefetchedCacheLines.erase(addr);
        }
    }

    // Cache line evicted — remove from tracking so future accesses start fresh.
    if (accessType == MemHierarchy::EVICT) {
        std::lock_guard<std::mutex> lock(prefetchedCacheLinesMutex);
        prefetchedCacheLines.erase(addr);
    }
}
