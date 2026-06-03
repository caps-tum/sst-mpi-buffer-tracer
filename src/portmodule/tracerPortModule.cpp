//
// Created by ewelo on 11/25/25.
//

#include "tracerPortModule.h"

#include <sst/core/sst_config.h>
#include <sst/elements/memHierarchy/memEvent.h>
#include <bitset>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "../tracercomponent/customTracer.h"
#include "../tracer_common.h"


TracerPortModule::TracerPortModule(SST::Params &params) : PortModule() {
    out = new SST::Output("TracerPortModule[@f:@l:@p] ", 1, 0, SST::Output::STDOUT);

    //std::string traceFile = "mpi-portmodule-traces-" + params.find<std::string>("data_src", "UNKNOWN") + ".txt";
    //traceOut = new SST::Output("", 1, 0, SST::Output::FILE, traceFile);

    pm_dataSrc = nameToDataSrc(params.find<std::string>("data_src", "UNKNOWN").c_str());

    stat_traced_events = registerStatistic<uint64_t>("traced_events");

    out->verbose(CALL_INFO, 1, 0, "Tracer PortModule constructed with dataSrc=%d.\n", pm_dataSrc);
}

TracerPortModule::~TracerPortModule() {

}

uintptr_t TracerPortModule::registerLinkAttachTool(const SST::AttachPointMetaData& mdata){
    return 0;
}

void TracerPortModule::eventSent(uintptr_t key, SST::Event*& ev) {
    // bool installOnSend() needs to return true for this to be called

    auto *me = dynamic_cast<SST::MemHierarchy::MemEvent *>(ev);
    if (me == nullptr) return;

    if (pm_dataSrc == L2) CustomTracer::removeInFlightL2Address(me->getBaseAddr());
    if (pm_dataSrc == L3) CustomTracer::removeInFlightL3Address(me->getBaseAddr());
}

uintptr_t TracerPortModule::registerHandlerIntercept(const SST::AttachPointMetaData& mdata) {
    // Fetching the name of the component of the port this portModule is attached to for DEBUG output.
    if (typeid(mdata) == typeid(SST::EventHandlerMetaData)) {
        const auto* meta = dynamic_cast<const SST::EventHandlerMetaData*>(&mdata);
        out->verbose(CALL_INFO, 1, 0, "PortModule is attached to port '%s' of component '%s'.\n", meta->port_name.c_str(), meta->comp_name.c_str());
    } else {
        out->fatal(CALL_INFO, 1, "Received non-EventHandlerMetaData type.\n");
    }

    return 0;
}

void TracerPortModule::interceptHandler(uintptr_t key, SST::Event*& ev, bool& cancel) {
    auto *me = dynamic_cast<SST::MemHierarchy::MemEvent *>(ev);

    if (me == nullptr) {
        out->verbose(CALL_INFO, 1, 0, "[WARN] PortModule received non-MemEvent.\n");
        return;
    }

    // check if memory event should be traced
    if (me->getMemFlags() & MEM_FLAG_TRACE) {
        if (me->isResponse()) {
            return; // Should not happen
        }

        stat_traced_events->addData(1);

        CustomTracer::storeDataSrcForID(me->getID(), pm_dataSrc);

        // Track this address as in-flight at the next level up
        if (pm_dataSrc == L2)  CustomTracer::addInFlightL2Address(me->getBaseAddr());
        if (pm_dataSrc == L3)  CustomTracer::addInFlightL3Address(me->getBaseAddr());
        // If the address is already in-flight at the next level, this is an MSHR hit
        if (pm_dataSrc == L1 && CustomTracer::isInFlightL2(me->getBaseAddr())) CustomTracer::markAsMshrHit(me->getID());
        if (pm_dataSrc == L2 && CustomTracer::isInFlightL3(me->getBaseAddr())) CustomTracer::markAsMshrHit(me->getID());

        /*if (mshr.count(me->getAddr()) > 0) {
            CustomTracer::markAsMshrHit(me->getID());
        }*/

        /*traceOut->verbose(
            CALL_INFO, 1, 0,
            "IN  %lu MemEvent ID: %06" PRIu64 "-%d completed. Address: 0x%08lx. Virtual Address: 0x%lx. Src: %s. Dst: %s. pm_dataSrc: %d.\n",
            getCurrentSimTimeNano(), me->getID().first, me->getID().second, me->getAddr(), me->getVirtualAddress(), me->getSrc().c_str(), me->getDst().c_str(), pm_dataSrc);*/
    }

    //mshr.insert(me->getAddr());


}