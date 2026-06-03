//
// Created by ewelo on 10/26/25.
//

#ifndef SST_CUSTOM_TRACER_CUSTOMTRACER_H
#define SST_CUSTOM_TRACER_CUSTOMTRACER_H

#include <fstream>
#include <sst/core/component.h>
#include <semaphore.h>
#include <time.h>
#include <thread>
#include <mutex>
#include <atomic>

#include "../tracer_common.h"
#include "../tracer_ipc.h"

#include <set>
#include <map>
#include <unordered_set>

#define MEM_TRACE_BUFFER_SIZE 1024
#define MPI_TRACE_BUFFER_SIZE 1024

class CustomTracer : public SST::Component {

public:
    SST_ELI_REGISTER_COMPONENT(
        CustomTracer,
        "customTracer",
        "customTracer",
        SST_ELI_ELEMENT_VERSION(1,0,0), // TODO
        "Custom tracer for tracing memory load accesses to MPI buffers",
        COMPONENT_CATEGORY_UNCATEGORIZED
    )

    SST_ELI_DOCUMENT_PARAMS(
        { "clock", "Frequency, same as system clock frequency", "" },
        { "corecount", "Number of CPU cores of the system", "1"},
        //{ "traceFile", "File where traces are logged to. (If empty, traces are logged to STDOUT", "" },
        //{ "debugFile", "File where debug output is logged to. (If empty, debug output is logged to STDOUT", "" }
        { "mpi_trace_out", "File where MPI traces are logged to", "mpi-traces.csv" },
        { "mem_trace_out", "File where memory traces are logged to", "mem-traces.csv" },
        { "disable_filter", "Disable filtering of memory addresses based on MPI buffer addresses", "false" }
    )

    SST_ELI_DOCUMENT_PORTS(
        { "cpu_link_%(corecount)d", "Connect towards cpu side", { "memHierarchy.MemEvent", "" } },
        { "mem_link_%(corecount)d", "Connect towards memory side", { "memHierarchy.MemEvent", "" } }
    )

    SST_ELI_DOCUMENT_STATISTICS(
        {"TracedL1Hits", "DEBUG: Number of traced Memory Events that HIT in L1", "count", 1},
        {"TracedL2Hits", "DEBUG: Number of traced Memory Events that HIT in L2", "count", 1},
        {"TracedL3Hits", "DEBUG: Number of traced Memory Events that HIT in L3", "count", 1},
        {"TracedMemHits", "DEBUG: Number of traced Memory Events that HIT in Memory", "count", 1},
        {"TotalMemTraced", "DEBUG: Total number of traced mem events", "count", 1},
    )

    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS()

    CustomTracer(SST::ComponentId_t id, SST::Params &params);

    ~CustomTracer() override;

    void init(unsigned int phase) override;

    void setup() override;

    void complete(unsigned int phase) override;

    void finish() override;

    bool clock(SST::Cycle_t current);

    void storeMemTrace(const MemTrace &trace);
    void storeMpiTrace(const MpiTrace &trace);

    void tunnelReaderLoop();

    static DataSrc getDataSrcForID(SST::Event::id_type id) {
        std::lock_guard<std::mutex> lock(dataSrcsMutex);

        DataSrc ret = DataSrc::UNKNOWN_DATA_SRC;

        if (dataSrcs.find(id) != dataSrcs.end()) {
            ret = dataSrcs[id];
            dataSrcs.erase(id);
        }
        return ret;
    }

    static void storeDataSrcForID(SST::Event::id_type id, DataSrc dataSrc) {
        std::lock_guard<std::mutex> lock(dataSrcsMutex);

        if (dataSrc == L1) {
            if (dataSrcs.find(id) == dataSrcs.end()) {
                //
            } else {
                std::cout << "[WARN] Received L1 dataSrc for ID " << id.first << ":" << id.second << " but it already has a dataSrc of " << dataSrcNames[dataSrcs[id]] << "\n";
            }
        } else {
            if (dataSrcs.find(id) != dataSrcs.end()) {
                // There is already a dataSrc for this ID
                if (dataSrcs[id] != dataSrc - 1) {
                    std::cout << "[WARN] Received " << dataSrcNames[dataSrc] << " dataSrc for ID " << id.first << ":" << id.second << " but it already has a dataSrc of " << dataSrcNames[dataSrcs[id]] << "\n";
                }
            } else {
                std::cout << "[WARN] Received " << dataSrcNames[dataSrc] << " dataSrc for ID " << id.first << ":" << id.second << " but it does not have a dataSrc yet. Meaning one or more cache levels got missed\n";
            }
        }

        // Only store the highest dataSrc for the given ID
        if (dataSrcs.find(id) == dataSrcs.end() || dataSrcs[id] < dataSrc) {
            dataSrcs[id] = dataSrc;
        }
    }

    static bool wasPrefetched(SST::Event::id_type id) {
        std::lock_guard<std::mutex> lock(prefetchedMutex);

        bool wasPrefetched = false;

        if (prefetched.find(id) != prefetched.end()) {
            wasPrefetched = true;
            prefetched.erase(id);
        }

        return wasPrefetched;
    }

    static void markAsPrefetched(SST::Event::id_type id) {
        std::lock_guard<std::mutex> lock(prefetchedMutex);

        prefetched.insert(id);
    }
    
    static bool wasMshrHit(SST::Event::id_type id) {
        std::lock_guard<std::mutex> lock(mshrEventsMutex);

        bool hit = false;

        if (mshrEvents.find(id) != mshrEvents.end()) {
            hit = true;
            mshrEvents.erase(id);
        }

        return hit;
    }

    static void markAsMshrHit(SST::Event::id_type id) {
        std::lock_guard<std::mutex> lock(mshrEventsMutex);

        mshrEvents.insert(id);
    }


    // Methods for MSHR detection in port modules. For each boundary (L1->L2, L2->L3) we
    // track addresses currently in-flight so that a second request arriving at the lower level
    // while the first is still pending can be flagged as an MSHR hit.

    static void addInFlightL2Address(uint64_t addr) {
        std::lock_guard<std::mutex> lock(inFlightL2AddressesMutex);
        inFlightL2Addresses.insert(addr);
    }
    static void removeInFlightL2Address(uint64_t addr) {
        std::lock_guard<std::mutex> lock(inFlightL2AddressesMutex);
        auto it = inFlightL2Addresses.find(addr);
        if (it != inFlightL2Addresses.end()) inFlightL2Addresses.erase(it);
    }
    static bool isInFlightL2(uint64_t addr) {
        std::lock_guard<std::mutex> lock(inFlightL2AddressesMutex);
        return inFlightL2Addresses.find(addr) != inFlightL2Addresses.end();
    }

    static void addInFlightL3Address(uint64_t addr) {
        std::lock_guard<std::mutex> lock(inFlightL3AddressesMutex);
        inFlightL3Addresses.insert(addr);
    }
    static void removeInFlightL3Address(uint64_t addr) {
        std::lock_guard<std::mutex> lock(inFlightL3AddressesMutex);
        auto it = inFlightL3Addresses.find(addr);
        if (it != inFlightL3Addresses.end()) inFlightL3Addresses.erase(it);
    }
    static bool isInFlightL3(uint64_t addr) {
        std::lock_guard<std::mutex> lock(inFlightL3AddressesMutex);
        return inFlightL3Addresses.find(addr) != inFlightL3Addresses.end();
    }


private:
    SST::Output *out;
    //SST::Output *debugOut;

    // Params
    bool disableFilter;
    uint32_t core_count;
    std::ofstream memTraceFile;
    std::ofstream mpiTraceFile;
    std::string frequency;

    // Links
    std::vector<SST::Link*> cpuLinks;
    std::vector<SST::Link*> memLinks;

    // Statistics (for debugging)
    Statistic<uint64_t>* statTracedL1Hits = registerStatistic<uint64_t>("TracedL1Hits");
    Statistic<uint64_t>* statTracedL2Hits = registerStatistic<uint64_t>("TracedL2Hits");
    Statistic<uint64_t>* statTracedL3Hits = registerStatistic<uint64_t>("TracedL3Hits");
    Statistic<uint64_t>* statTracedMemHits = registerStatistic<uint64_t>("TracedMemHits");
    Statistic<uint64_t>* statTotalMemTraced = registerStatistic<uint64_t>("TotalMemTraced");

    std::map<SST::Event::id_type, uint64_t> requestTimestamps;

    static std::map<SST::Event::id_type, DataSrc> dataSrcs; // The portmodules write the <id, dataSrc> of the MemEvents to this
    static std::mutex dataSrcsMutex;

    static std::unordered_multiset<uint64_t> inFlightL2Addresses;
    static std::mutex inFlightL2AddressesMutex;

    static std::unordered_multiset<uint64_t> inFlightL3Addresses;
    static std::mutex inFlightL3AddressesMutex;


    static std::set<SST::Event::id_type> mshrEvents; // The portModules write the IDs of the events that got a cache hit in their MSHR to this set
    static std::mutex mshrEventsMutex;

    static std::set<SST::Event::id_type> prefetched; // The cacheListeners write the IDs of the MemEvents that caused a cache hit in a cache line that was prefetched
    static std::mutex prefetchedMutex;

    SST::TimeConverter nanoTimeConv;

    MpiTracesTunnel* mpiTunnel = nullptr;
    SimpleMpiTracesTunnel* simpleMpiTunnel = nullptr;

    std::map<uintptr_t, uintptr_t> mpiFilterAddresses;

    std::thread tunnelReaderThread;
    std::atomic<bool> stopTunnelReader;
    mutable std::mutex filterMutex;
    std::mutex mpiBufferMutex;

    static std::string vectorToHexString(const std::vector<uint8_t> &vec);
    bool isAddrInFilter(uintptr_t addr) const;

    static std::string formatMemTraceCsv(const MemTrace& trace) ;
    static std::string formatMpiTraceCsv(const MpiTrace& trace) ;
    static std::string getMemTraceCsvHeader();
    static std::string getMpiTraceCsvHeader();
};

#endif //SST_CUSTOM_TRACER_CUSTOMTRACER_H
