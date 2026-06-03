#ifndef SST_CUSTOM_TRACER_TRACERCACHELISTENER_H
#define SST_CUSTOM_TRACER_TRACERCACHELISTENER_H

#include <sst/elements/memHierarchy/cacheListener.h>
#include <unordered_set>
#include <mutex>

class TracerCacheListener : public MemHierarchy::CacheListener {
public:
    SST_ELI_REGISTER_SUBCOMPONENT(
        TracerCacheListener,
        "customTracer",
        "tracerCacheListener",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Cache listener for tracer",
        SST::MemHierarchy::CacheListener
    )

    TracerCacheListener(ComponentId_t id, Params& params);

    void notifyAccess(const MemHierarchy::CacheListenerNotification& notify) override;

private:
    Output* out;

    // Tracks addresses loaded into cache by the prefetcher, not yet demanded.
    // When a READ HIT arrives for one of these addresses, it is a prefetch hit.
    std::unordered_set<MemHierarchy::Addr> prefetchedCacheLines;
    std::mutex prefetchedCacheLinesMutex;
};


#endif //SST_CUSTOM_TRACER_TRACERCACHELISTENER_H
