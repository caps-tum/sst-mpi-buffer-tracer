//
// Created by Louis Ewen on 10/26/25.
//
#include "customTracer.h"

#include <sst/core/sst_config.h>
#include <sst/elements/memHierarchy/memEvent.h>
#include <sst/core/interprocess/tunneldef.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <bitset>

#include "../tracer_common.h"
#include "../tracer_ipc.h"

std::map<SST::Event::id_type, DataSrc> CustomTracer::dataSrcs;
std::mutex CustomTracer::dataSrcsMutex;

std::set<SST::Event::id_type> CustomTracer::prefetched;
std::mutex CustomTracer::prefetchedMutex;

std::set<SST::Event::id_type> CustomTracer::mshrEvents;
std::mutex CustomTracer::mshrEventsMutex;

std::unordered_multiset<uint64_t> CustomTracer::inFlightL2Addresses;
std::mutex CustomTracer::inFlightL2AddressesMutex;

std::unordered_multiset<uint64_t> CustomTracer::inFlightL3Addresses;
std::mutex CustomTracer::inFlightL3AddressesMutex;


CustomTracer::CustomTracer(SST::ComponentId_t id, SST::Params &params) : SST::Component(id) {
    out = new SST::Output("CustomTracer[@f:@l:@p] ", 1, 0, SST::Output::STDOUT);

    // Init clock
    frequency = params.find<std::string>("clock", "");
    if (!frequency.empty()) {
        out->debug(CALL_INFO, 1, 0, "Registering CustomTracer clock at %s\n", frequency.c_str());
        registerClock(frequency, new SST::Clock::Handler2<CustomTracer, &CustomTracer::clock>(this));
    } else {
        out->fatal(CALL_INFO, -1, "CustomTracer requires a 'clock' parameter to be set.\n");
    }

    // Init output files
    /*auto debugFile = params.find<std::string>("debugFile", "");
    if (debugFile.empty()) {
        debugOut = new SST::Output("CustomTracer[@f:@l:@p] [DEBUG] ", 1, 0, SST::Output::STDOUT);
    } else {
        debugOut = new SST::Output("", 1, 0, SST::Output::FILE, debugFile);
    }*/

    auto memTraceOut = params.find<std::string>("mem_trace_out", "mem-traces.csv");
    if (!memTraceOut.empty()) {
        memTraceFile.open(memTraceOut);
    } else {
        out->fatal(CALL_INFO, -1, "Could not parse 'mem_trace_out' parameter.\n");
    }

    auto mpiTraceOut = params.find<std::string>("mpi_trace_out", "mpi-traces.csv");
    if (!mpiTraceOut.empty()) {
        mpiTraceFile.open(mpiTraceOut);
    } else {
        out->fatal(CALL_INFO, -1, "Could not parse 'mpi_trace_out' parameter.\n");
    }

    // Write CSV headers
    memTraceFile << getMemTraceCsvHeader() << "\n";
    mpiTraceFile << getMpiTraceCsvHeader() << "\n";

    // Get filter options
    disableFilter = params.find<bool>("disable_filter", false);

    // Get links
    core_count = params.find<int>("corecount", 1);
    for (int i = 0; i < core_count; i++) {
        cpuLinks.push_back(configureLink("cpu_link_" + std::to_string(i)));
        memLinks.push_back(configureLink("mem_link_" + std::to_string(i)));
    }

    nanoTimeConv = getTimeConverter("1ns");

    /* ------------------------------ */
    /* Setup Tunnels
    /* ------------------------------ */

    if (tunnel_mpi_traces_init(&mpiTunnel, true) != 0) {
        out->fatal(CALL_INFO, -1, "Failed to create mpi traces tunnel.\n");
    }

    if (tunnel_simple_mpi_traces_init(&simpleMpiTunnel, true) != 0) {
        out->fatal(CALL_INFO, -1, "Failed to create simple mpi traces tunnel.\n");
    }

    out->verbose(CALL_INFO, 1, 0, "CustomTracer component constructed.\n");

    stopTunnelReader = false;
    tunnelReaderThread = std::thread(&CustomTracer::tunnelReaderLoop, this);
}

CustomTracer::~CustomTracer() {
}

void CustomTracer::init(unsigned int phase) {
    //out->verbose(CALL_INFO, 1, 0, "Component is participating in phase %d of init.\n", phase);

    // Forward init events
    for (int i = 0; i < core_count; i++) {
        SST::Link *cpuLink = cpuLinks[i];
        SST::Link *memLink = memLinks[i];

        while (SST::Event *ev = cpuLink->recvUntimedData()) {
            memLink->sendUntimedData(ev);
        }
        while (SST::Event *ev = memLink->recvUntimedData()) {
            cpuLink->sendUntimedData(ev);
        }
    }
}

void CustomTracer::setup() {
    out->verbose(CALL_INFO, 1, 0, "Component is being setup.\n");
}

void CustomTracer::complete(unsigned int phase) {
}

void CustomTracer::finish() {
    // Stop the tunnel reader thread before cleaning up resources
    stopTunnelReader = true;
    if (tunnelReaderThread.joinable()) {
        tunnelReaderThread.join();
    }

    // Close tunnels
    if (mpiTunnel != nullptr) {
        munmap(mpiTunnel, sizeof(MpiTracesTunnel));
    }

    if (simpleMpiTunnel != nullptr) {
        munmap(simpleMpiTunnel, sizeof(SimpleMpiTracesTunnel));
    }

    // Unlink shared memory and semaphores
    shm_unlink(SHM_NAME_MPI);
    shm_unlink(SHM_NAME_MPI_SIMPLE);

    // Close output files
    memTraceFile.close();
    mpiTraceFile.close();

    if (!dataSrcs.empty()) {
        out->fatal(
            CALL_INFO, -1, "DataSrcs map is not empty at finish, meaning some dataSrcs arrived to late. Size: %zu\n",
            dataSrcs.size());
    }
}

int test = 0;

bool CustomTracer::clock(SST::Cycle_t current) {
    //auto nanoseconds = nanoTimeConv.convertFromCoreTime(getCurrentSimCycle());

    auto currentCycle = current;

    for (int i = 0; i < core_count; i++) {
        // TODO maybe run in parallel?
        SST::Link *cpuLink = cpuLinks[i];
        SST::Link *memLink = memLinks[i];

        // Process events coming from the cpu side
        while (SST::Event *ev = cpuLink->recv()) {
            // Check if it's a MemEvent
            auto *me = dynamic_cast<SST::MemHierarchy::MemEvent *>(ev);
            if (me == nullptr) {
                out->fatal(CALL_INFO, -1, "CustomTracer received non-MemEvent from cpuLink.\n");
            }

            /*debugOut->verbose(
                CALL_INFO, 1, 0,
                "[REQ] [CPU -> MEM] Addr: 0x%lx  Command: %-14s  Payload size: %-4zu  Payload content: %s\n",
                me->getVirtualAddress(),
                SST::MemHierarchy::CommandString[static_cast<int>(me->getCmd())],
                me->getPayload().size(),
                vectorToHexString(me->getPayload()).c_str()
            );*/

            // Only trace reads
            if (me->getCmd() != SST::MemHierarchy::Command::GetS && me->getCmd() != SST::MemHierarchy::Command::GetSX) {
                memLink->send(me);
                continue;
            }


            // check if the address of the mem event is in the list of traced MPI addresses (if the filter is not disabled)
            if (disableFilter || isAddrInFilter(me->getVirtualAddress())) {
                // print warning if the event is not a request (should not happen)
                if (!me->isDataRequest()) {
                    out->debug(CALL_INFO, 1, 0, "[WARN] Received non-request from cpuLink: %d.\n",
                               static_cast<int>(me->getCmd()));
                    continue;
                }

                // store timestamp of request, so we can later calculate latency when the response comes back
                requestTimestamps[me->getID()] = currentCycle;

                // set a custom flag in the memory event, we later use this flag in our PortModule to filter the mem events that should be traced
                //me->setFlag(MEM_FLAG_TRACE);
                me->setMemFlags(me->getMemFlags() | MEM_FLAG_TRACE);
            }

            // Forward to memory side
            memLink->send(me);
        }

        // Process events coming from the memory side
        while (SST::Event *ev = memLink->recv()) {
            // Check if it's a MemEvent
            auto *me = dynamic_cast<SST::MemHierarchy::MemEvent *>(ev);
            if (me == nullptr) {
                out->fatal(CALL_INFO, -1, "CustomTracer received non-MemEvent from memLink.\n");
            }

            /*debugOut->verbose(
                CALL_INFO, 1, 0,
                "[RES] [MEM -> CPU] Addr: 0x%lx  Command: %-14s  Payload size: %-4zu  Payload content: %s\n",
                me->getVirtualAddress(),
                SST::MemHierarchy::CommandString[static_cast<int>(me->getCmd())],
                me->getPayload().size(),
                vectorToHexString(me->getPayload()).c_str()
            );*/

            auto entry = requestTimestamps.find(me->getID());
            if (entry != requestTimestamps.end()) {
                uint64_t startTime = entry->second;
                uint32_t latency = currentCycle - startTime;

                auto me_level = getDataSrcForID(me->getID());

                bool wasMshr = wasMshrHit(me->getID());

                // bool wasPrefetched = CustomTracer::wasPrefetched(me->getID());
                bool wasPrefetched = false;

                /*traceOut->verbose(
                    CALL_INFO, 1, 0,
                    "%lu MemEvent ID: %06" PRIu64 "-%d completed. Latency: %04" PRIu32
                    " ns. Address: 0x%08lx. Virtual Address: 0x%lx. Core: %02d. Src: %s. Dst: %s. Level: %d.\n",
                    getCurrentSimTimeNano(), me->getID().first, me->getID().second, latency, me->getAddr(), me->getVirtualAddress(),
                    i, me->getSrc().c_str(), me->getDst().c_str(), me_level);*/

                // Sore trace in buffer
                MemTrace memTrace = {
                    .core = static_cast<uint32_t>(i),
                    .dataSource = me_level,
                    .loadLatency = latency,
                    .timestamp = startTime,
                    .mpiProcessNr = 0,
                    .memAddr = me->getVirtualAddress(),
                    .prefetched = wasPrefetched,
                    .wasMshrHit = wasMshr,
                    .command = ""
                };

                // Copy Command to trace
                strncpy(memTrace.command, SST::MemHierarchy::CommandString[static_cast<int>(me->getCmd())],
                        sizeof(memTrace.command) - 1);
                memTrace.command[sizeof(memTrace.command) - 1] = '\0';

                storeMemTrace(memTrace);

                // Remove from map
                requestTimestamps.erase(entry);
            }

            // Forward to cpu side
            cpuLink->send(me);
        }
    }

    return false;
}

std::string CustomTracer::vectorToHexString(const std::vector<uint8_t> &vec) {
    std::ostringstream oss;
    for (size_t i = 0; i < vec.size(); ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(vec[i]);
        if (i != vec.size() - 1) {
            oss << " ";
        }
    }
    return oss.str();
}

bool CustomTracer::isAddrInFilter(uintptr_t addr) const {
    std::lock_guard<std::mutex> lock(filterMutex);

    // Find the first range that starts <= address
    auto it = mpiFilterAddresses.upper_bound(addr);

    if (it == mpiFilterAddresses.begin()) {
        return false;
    }

    --it; // Go to the largest start <= address
    return addr >= it->first && addr <= it->second;
}


void CustomTracer::storeMemTrace(const MemTrace &trace) {
    memTraceFile << formatMemTraceCsv(trace) << std::endl;

    // Update statistics for debugging purposes
    statTotalMemTraced->addData(1);
    switch (trace.dataSource) {
        case DataSrc::L1:
            statTracedL1Hits->addData(1);
            break;
        case DataSrc::L2:
            statTracedL2Hits->addData(1);
            break;
        case DataSrc::L3:
            statTracedL3Hits->addData(1);
            break;
        case DataSrc::MEM:
            statTracedMemHits->addData(1);
            break;
        default:
            break;
    }
}

void CustomTracer::storeMpiTrace(const MpiTrace &trace) {
    std::lock_guard<std::mutex> lock(mpiBufferMutex); // I cannot remember why I put this here, but now I'm too afraid to remove it.
    mpiTraceFile << formatMpiTraceCsv(trace) << std::endl;
}

void CustomTracer::tunnelReaderLoop() {
    SimpleMpiTrace simpleMpiTrace;
    MpiTrace mpiTrace;

    while (!stopTunnelReader) {
        // Get the simple traces from the tunnel (if there are any). We use those traces to filter the addresses
        for (unsigned int i = 0; i < MAX_PRODUCERS; ++i) {
            while (tunnel_simple_mpi_traces_recv(simpleMpiTunnel, &simpleMpiTrace, &i) == 0) {
                if (simpleMpiTrace.buffAddr == 0 || simpleMpiTrace.buffMinSize == 0) {
                    out->fatal(CALL_INFO, -1, "Received invalid MPI buffer address or size from tunnel.\n");
                }

                {
                    std::lock_guard<std::mutex> lock(filterMutex);
                    //mpiFilterAddresses.emplace_back(simpleMpiTrace.buffAddr, simpleMpiTrace.buffAddr + simpleMpiTrace.buffMinSize - 1);
                    if (mpiFilterAddresses.count(simpleMpiTrace.buffAddr) == 0 || mpiFilterAddresses[simpleMpiTrace.
                            buffAddr] < simpleMpiTrace.buffAddr + simpleMpiTrace.buffMinSize - 1) {
                        mpiFilterAddresses[simpleMpiTrace.buffAddr] =
                                simpleMpiTrace.buffAddr + simpleMpiTrace.buffMinSize - 1;
                    }
                }
            }
        }

        // Get the full traces from the tunnel (if there are any). These traces will be saved to the trace file.
        for (unsigned int i = 0; i < MAX_PRODUCERS; ++i) {
            while (tunnel_mpi_traces_recv(mpiTunnel, &mpiTrace, &i) == 0) {
                storeMpiTrace(mpiTrace);
            }
        }

        // Small sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string CustomTracer::getMemTraceCsvHeader() {
    return "timestamp,address,core,dataSource,loadLatency,prefetched,mpiProcessNr,command,mshr";
}

std::string CustomTracer::formatMemTraceCsv(const MemTrace &trace) {
    std::ostringstream oss;
    oss << trace.timestamp << ","
            << "0x" << std::hex << trace.memAddr << std::dec << ","
            << trace.core << ","
            << dataSrcNames[trace.dataSource] << ","
            << trace.loadLatency << ","
            << (trace.prefetched ? "true" : "false") << ","
            << trace.mpiProcessNr << ","
            << trace.command << ","
            << (trace.wasMshrHit ? "true" : "false");
    return oss.str();
}

std::string CustomTracer::getMpiTraceCsvHeader() {
    return "callRank,function,callIdentifier,startTimestamp,endTimestamp,buffAddr,count,datatype,targetRank,comm,tag";
}

std::string CustomTracer::formatMpiTraceCsv(const MpiTrace &trace) {
    std::ostringstream oss;
    oss << trace.callRank << ","
            << mpiFunctionNames[trace.function] << ","
            << "0x" << std::hex << trace.callIdentifier << std::dec << ","
            << trace.startTimestamp << ","
            << trace.endTimestamp << ","
            << "0x" << std::hex << trace.buffAddr << std::dec << ","
            << trace.count << ","
            << trace.datatype << ","
            << trace.targetRank << ","
            << trace.comm << ","
            << trace.tag;
    return oss.str();
}
