//
// Created by ewelo on 11/25/25.
//

#ifndef SST_CUSTOM_TRACER_TRACERPORTMODULE_H
#define SST_CUSTOM_TRACER_TRACERPORTMODULE_H

#include <sst/core/portModule.h>
#include <fstream>
#include <vector>
#include <semaphore.h>
#include <sst/elements/memHierarchy/memEvent.h>

#include "../tracer_common.h"

class TracerPortModule : public SST::PortModule {
public:
    SST_ELI_REGISTER_PORTMODULE(
        TracerPortModule,
        "customTracer",
        "portmodules.tracerPortModule",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Port module for tracer")

    SST_ELI_DOCUMENT_PARAMS(
        { "data_src", "The data source level this module is attached to. Options: [L1, L2, L3, MEM]", "UNKNOWN",},
    )

    SST_ELI_DOCUMENT_STATISTICS(
        {"traced_events", "Request Events received with the tracing flag set.", "count", 1}
    )

    explicit TracerPortModule(SST::Params& params);
    TracerPortModule() = default;
    ~TracerPortModule() override;

    uintptr_t registerLinkAttachTool(const SST::AttachPointMetaData& mdata) override;
    void eventSent(uintptr_t key, SST::Event*& ev) override;

    uintptr_t registerHandlerIntercept(const SST::AttachPointMetaData& mdata) override;
    void interceptHandler(uintptr_t key, SST::Event*& ev, bool& cancel) override;

    bool installOnReceive() override { return true; }
    // the L1->L2 port needs this send hook so it can remove the address from the outstanding set on response
    bool installOnSend() override { return pm_dataSrc == L2; }

protected:
    //void serialize_order(SST::Core::Serialization::serializer& ser) override;
    ImplementSerializable(TracerPortModule);

private:
    SST::Output *out;
    DataSrc pm_dataSrc;

    Statistic<uint64_t>* stat_traced_events;

    std::set<SST::MemHierarchy::Addr> mshr;
};


#endif //SST_CUSTOM_TRACER_TRACERPORTMODULE_H