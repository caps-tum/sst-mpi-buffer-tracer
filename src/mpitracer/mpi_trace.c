#include <stdio.h>
#include "../tracer_ipc.h"
#include "../tracer_common.h"
#include <semaphore.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <arielapi.h>
#include <dlfcn.h>

int get_MPI_buffer_size(int count, MPI_Datatype datatype);

MpiTracesTunnel *mpi_traces_tunnel = NULL;
SimpleMpiTracesTunnel *simple_mpi_traces_tunnel = NULL;

int init_tunnels() {
    if (!mpi_traces_tunnel) {
        if (tunnel_mpi_traces_init(&mpi_traces_tunnel, false) != 0) {
            fprintf(stderr, "_init_tunnels: Could not open MPI tracer tunnel\n");
            return -1;
        }
    }

    if (!simple_mpi_traces_tunnel) {
        if (tunnel_simple_mpi_traces_init(&simple_mpi_traces_tunnel, false) != 0) {
            fprintf(stderr, "_init_tunnels: Could not open simple MPI tracer tunnel\n");
            return -1;
        }
    }

    return 0;
}

int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status) {
    if (init_tunnels() != 0) {
        return MPI_ERR_OTHER;
    }

    SimpleMpiTrace simple_mpi_trace = {
        .buffAddr = (uint64_t) buf,
        .buffMinSize = get_MPI_buffer_size(count, datatype),
    };

    int rank;
    MPI_Comm_rank(comm, &rank);

    tunnel_simple_mpi_traces_send(simple_mpi_traces_tunnel, &simple_mpi_trace, rank % MAX_PRODUCERS);

    struct timespec start_t, end_t;
    clock_gettime(CLOCK_MONOTONIC, &start_t);

    int ret = PMPI_Recv(buf, count, datatype, source, tag, comm, status);

    clock_gettime(CLOCK_MONOTONIC, &end_t);

    MpiTrace mpi_trace = {
        .callRank = rank,
        .function = MPI_RECV,
        .buffAddr = (uint64_t) buf,
        .buffMinSize = get_MPI_buffer_size(count, datatype),
        .count = count,
        .datatype = datatype,
        .targetRank = source,
        .comm = comm,
        .tag = tag,
        .startTimestamp = (uint64_t) start_t.tv_sec * 1000000000ULL + start_t.tv_nsec,
        .endTimestamp = (uint64_t) end_t.tv_sec * 1000000000ULL + end_t.tv_nsec,
        .callIdentifier = (uintptr_t) __builtin_return_address(0),
    };

    tunnel_mpi_traces_send(mpi_traces_tunnel, &mpi_trace, rank % MAX_PRODUCERS);

    return ret;
}

int MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
    if (init_tunnels() != 0) {
        return MPI_ERR_OTHER;
    }

    SimpleMpiTrace simple_mpi_trace = {
        .buffAddr = (uint64_t) buf,
        .buffMinSize = get_MPI_buffer_size(count, datatype),
    };

    int rank;
    MPI_Comm_rank(comm, &rank);

    tunnel_simple_mpi_traces_send(simple_mpi_traces_tunnel, &simple_mpi_trace, rank % MAX_PRODUCERS);

    struct timespec start_t, end_t;
    clock_gettime(CLOCK_MONOTONIC, &start_t);

    int ret = PMPI_Send(buf, count, datatype, dest, tag, comm);

    clock_gettime(CLOCK_MONOTONIC, &end_t);

    MpiTrace mpi_trace = {
        .callRank = rank,
        .function = MPI_SEND,
        .buffAddr = (uint64_t) buf,
        .buffMinSize = get_MPI_buffer_size(count, datatype),
        .count = count,
        .datatype = datatype,
        .targetRank = dest,
        .comm = comm,
        .tag = tag,
        .startTimestamp = (uint64_t) start_t.tv_sec * 1000000000ULL + start_t.tv_nsec,
        .endTimestamp = (uint64_t) end_t.tv_sec * 1000000000ULL + end_t.tv_nsec,
        .callIdentifier = (uintptr_t) __builtin_return_address(0),
    };

    tunnel_mpi_traces_send(mpi_traces_tunnel, &mpi_trace, rank % MAX_PRODUCERS);

    return ret;
}

int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request) {
    if (init_tunnels() != 0) {
        return MPI_ERR_OTHER;
    }

    SimpleMpiTrace simple_mpi_trace = {
        .buffAddr = (uint64_t) buf,
        .buffMinSize = get_MPI_buffer_size(count, datatype),
    };

    int rank;
    MPI_Comm_rank(comm, &rank);

    tunnel_simple_mpi_traces_send(simple_mpi_traces_tunnel, &simple_mpi_trace, rank % MAX_PRODUCERS);

    struct timespec start_t, end_t;
    clock_gettime(CLOCK_MONOTONIC, &start_t);

    int ret = PMPI_Irecv(buf, count, datatype, source, tag, comm, request);

    clock_gettime(CLOCK_MONOTONIC, &end_t);

    // We send the size (a plain int) through the tunnel
    int dtSize;
    MPI_Type_size(datatype, &dtSize);

    MpiTrace mpi_trace = {
        .callRank = rank,
        .function = MPI_IRECV,
        .buffAddr = (uint64_t) buf,
        .request = (uintptr_t) request,
        .buffMinSize = get_MPI_buffer_size(count, datatype),
        .count = count,
        .datatype = datatype,
        .datatypeSize = dtSize,
        .targetRank = source,
        .comm = comm,
        .tag = tag,
        .startTimestamp = (uint64_t) start_t.tv_sec * 1000000000ULL + start_t.tv_nsec,
        .endTimestamp = (uint64_t) end_t.tv_sec * 1000000000ULL + end_t.tv_nsec,
        .callIdentifier = (uintptr_t) __builtin_return_address(0),
    };

    tunnel_mpi_traces_send(mpi_traces_tunnel, &mpi_trace, rank % MAX_PRODUCERS);

    return ret;
}

int MPI_Wait(MPI_Request *request, MPI_Status *status) {
    if (init_tunnels() != 0) {
        return MPI_ERR_OTHER;
    }

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    struct timespec start_t, end_t;
    clock_gettime(CLOCK_MONOTONIC, &start_t);

    int ret = PMPI_Wait(request, status);

    clock_gettime(CLOCK_MONOTONIC, &end_t);

    MpiTrace mpi_trace = {
        .callRank = rank,
        .function = MPI_WAIT,
        .buffAddr = (uint64_t) request,
        .buffMinSize = 1,
        .count = 1,
        .datatype = MPI_DATATYPE_NULL,
        .targetRank = -1,
        .comm = MPI_COMM_NULL,
        .tag = -1,
        .startTimestamp = (uint64_t) start_t.tv_sec * 1000000000ULL + start_t.tv_nsec,
        .endTimestamp = (uint64_t) end_t.tv_sec * 1000000000ULL + end_t.tv_nsec,
        .callIdentifier = (uintptr_t) __builtin_return_address(0),
    };

    tunnel_mpi_traces_send(mpi_traces_tunnel, &mpi_trace, rank % MAX_PRODUCERS);

    return ret;
}

int MPI_Waitall(int count, MPI_Request array_of_requests[], MPI_Status array_of_statuses[]) {
    if (init_tunnels() != 0) {
        return MPI_ERR_OTHER;
    }

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    struct timespec start_t, end_t;
    clock_gettime(CLOCK_MONOTONIC, &start_t);

    int ret = PMPI_Waitall(count, array_of_requests, array_of_statuses);

    clock_gettime(CLOCK_MONOTONIC, &end_t);

    MpiTrace mpi_trace = {
        .callRank = rank,
        .function = MPI_WAITALL,
        .buffAddr = (uint64_t) array_of_requests,
        .buffMinSize = count,
        .count = count,
        .datatype = MPI_DATATYPE_NULL,
        .targetRank = -1,
        .comm = MPI_COMM_NULL,
        .tag = -1,
        .startTimestamp = (uint64_t) start_t.tv_sec * 1000000000ULL + start_t.tv_nsec,
        .endTimestamp = (uint64_t) end_t.tv_sec * 1000000000ULL + end_t.tv_nsec,
        .callIdentifier = (uintptr_t) __builtin_return_address(0),
    };

    tunnel_mpi_traces_send(mpi_traces_tunnel, &mpi_trace, rank % MAX_PRODUCERS);

    return ret;
}

int MPI_Init(int *argc, char ***argv) {
    struct timespec start_t, end_t;

    clock_gettime(CLOCK_MONOTONIC, &start_t);

    // Here we call PMPI_Init first and then get the rank, since MPI_Comm_rank only works after Init
    int ret = PMPI_Init(argc,argv);
    clock_gettime(CLOCK_MONOTONIC, &end_t);

    int tret = init_tunnels();
    if (tret != 0) {
        return MPI_ERR_OTHER;
    }

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    MpiTrace mpi_trace = {
        .callRank = rank,
        .function = MPI_INIT,
        .startTimestamp = (uint64_t) start_t.tv_sec * 1000000000ULL + start_t.tv_nsec,
        .endTimestamp = (uint64_t) end_t.tv_sec * 1000000000ULL + end_t.tv_nsec,
        .callIdentifier = (uintptr_t) __builtin_return_address(0),
    };

    tunnel_mpi_traces_send(mpi_traces_tunnel, &mpi_trace, rank % MAX_PRODUCERS);

    return ret;
}


int MPI_Finalize(void) {
    if (init_tunnels() != 0) {
        return MPI_ERR_OTHER;
    }

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    struct timespec start_t, end_t;
    clock_gettime(CLOCK_MONOTONIC, &start_t);

    int ret = PMPI_Finalize();

    clock_gettime(CLOCK_MONOTONIC, &end_t);
    
    MpiTrace mpi_trace = {
        .callRank = rank,
        .function = MPI_FINALIZE,
        .startTimestamp = (uint64_t) start_t.tv_sec * 1000000000ULL + start_t.tv_nsec,
        .endTimestamp   = (uint64_t) end_t.tv_sec * 1000000000ULL + end_t.tv_nsec,
        .callIdentifier = (uintptr_t) __builtin_return_address(0),
    };

    tunnel_mpi_traces_send(mpi_traces_tunnel, &mpi_trace, rank % MAX_PRODUCERS);
    return ret;
}

int get_MPI_buffer_size(int count, MPI_Datatype datatype) {
    // Get size
    int datatype_size;
    MPI_Type_size(datatype, &datatype_size);

    // Get extent
    MPI_Aint datatype_extent, datatype_lb;
    MPI_Type_get_extent(datatype, &datatype_lb, &datatype_extent);

    // https://enccs.github.io/intermediate-mpi/derived-datatypes-pt2/
    return (count - 1) * (int) datatype_extent + datatype_size;
}