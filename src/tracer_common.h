//
// Created by ewelo on 12/11/25.
//

#ifndef SST_CUSTOM_TRACER_TRACER_COMMON_H
#define SST_CUSTOM_TRACER_TRACER_COMMON_H

#include <mpi.h>
#include <strings.h>

#define MEM_FLAG_TRACE (1 << 25)

typedef enum {
    UNKNOWN_DATA_SRC = 0,
    L1 = 1,
    L2 = 2,
    L3 = 3,
    MEM = 4
} DataSrc;

static const char* dataSrcNames[] = {
    "Unknown",
    "L1",
    "L2",
    "L3",
    "Mem"
};

static DataSrc nameToDataSrc(const char* name) {
    for (int i = 0; i < sizeof(dataSrcNames); i++) {
        if (strcasecmp(name, dataSrcNames[i]) == 0) {
            return (DataSrc) i;
        }
    }

    return UNKNOWN_DATA_SRC;
}

typedef enum {
    MPI_RECV,
    MPI_IRECV,
    MPI_SEND,
    MPI_WAIT,
    MPI_WAITALL,
    MPI_INIT,
    MPI_FINALIZE
} MpiFunction;

static const char* mpiFunctionNames[] = {
    "MPI_Recv",
    "MPI_Irecv",
    "MPI_Send",
    "MPI_Wait",
    "MPI_Waitall",
    "MPI_Init",
    "MPI_Finalize"
};

typedef struct {
    uint32_t core;
    DataSrc dataSource;
    uint32_t loadLatency;
    uint64_t timestamp;
    uint32_t mpiProcessNr;
    uintptr_t memAddr;
    bool prefetched;
    bool wasMshrHit;
    char command[32];
} MemTrace;

// Detailed MPI trace
typedef struct {
    uint32_t callRank;
    MpiFunction function;
    uintptr_t buffAddr;
    uintptr_t request;
    uint32_t buffMinSize;
    int count;
    MPI_Datatype datatype;     
    int datatypeSize;          // portable: element size in bytes
    int targetRank;
    MPI_Comm comm;
    int tag;
    uint64_t startTimestamp;
    uint64_t endTimestamp;
    uintptr_t callIdentifier;
} MpiTrace;

// Simple version of MpiTrace that gets used for filtering the mem traces.
typedef struct {
    uint64_t buffAddr;
    uint32_t buffMinSize;
} SimpleMpiTrace;

#endif //SST_CUSTOM_TRACER_TRACER_COMMON_H