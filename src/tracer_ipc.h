//
// Created by ewelo on 19.01.26.
//

#ifndef SST_CUSTOM_TRACER_TRACER_IPC2_H
#define SST_CUSTOM_TRACER_TRACER_IPC2_H


#ifndef __cplusplus
#include <stdatomic.h>
#include <stdbool.h>
#else
#include <cstdint>
#include <cstdbool>
#endif
#include <stdint.h>
#include <pthread.h>

#include "tracer_common.h"

#define SHM_NAME_MPI "/sst_tracer_shm_mpi"
#define SHM_NAME_MPI_SIMPLE "/sst_tracer_shm_mpi_simple"
#define RING_BUFFER_SIZE 1024
#define MAX_PRODUCERS 64

// Ring buffer for each producer
typedef struct {
    pthread_mutex_t lock;
    unsigned int    head;
    unsigned int    tail;
    char            _pad[64 - sizeof(pthread_mutex_t) - 2 * sizeof(unsigned int)];
    MpiTrace        traces[RING_BUFFER_SIZE];
} ProducerQueueMpi;

typedef struct {
    pthread_mutex_t lock;
    unsigned int    head;
    unsigned int    tail;
    char            _pad[64 - sizeof(pthread_mutex_t) - 2 * sizeof(unsigned int)];
    SimpleMpiTrace  traces[RING_BUFFER_SIZE];
} ProducerQueueSimple;

// Tunnels
typedef struct {
    //unsigned int num_producers;
    ProducerQueueMpi queues[MAX_PRODUCERS];
} MpiTracesTunnel;

typedef struct {
    //unsigned int num_producers;
    ProducerQueueSimple queues[MAX_PRODUCERS];
} SimpleMpiTracesTunnel;


#ifdef __cplusplus
extern "C" {
#endif


int tunnel_mpi_traces_init(MpiTracesTunnel **tunnel, bool create);
int tunnel_mpi_traces_send(MpiTracesTunnel* tunnel, MpiTrace* trace, unsigned int producer_id);
int tunnel_mpi_traces_recv(MpiTracesTunnel* tunnel, MpiTrace* trace, unsigned int *producer_id);

int tunnel_simple_mpi_traces_init(SimpleMpiTracesTunnel **tunnel, bool create);
int tunnel_simple_mpi_traces_send(SimpleMpiTracesTunnel* tunnel, SimpleMpiTrace* trace, unsigned int producer_id);
int tunnel_simple_mpi_traces_recv(SimpleMpiTracesTunnel* tunnel, SimpleMpiTrace* trace, unsigned int *producer_id);

#ifdef __cplusplus
}
#endif

#endif //SST_CUSTOM_TRACER_TRACER_IPC2_H