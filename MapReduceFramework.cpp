#include "MapReduceFramework.h"
#include "MapReduceClient.h"
#include <pthread.h>
#include <vector>
#include <atomic>
#include <stdio.h>
#include "Barrier.h"
#include <bitset>
#include <unistd.h>
#include <iostream>

// get 31 most loft bits mask.
#define MASK31 (uint64_t(0xffffffff/2))

using std::vector;
using std::atomic;

struct JobContext;

struct Handler {
    // shared to all threads:
    const MapReduceClient *client;
    const InputVec *inputVec;
    OutputVec *outputVec;
    pthread_mutex_t outputVecMutex;

    atomic<uint64_t> *sync;
    atomic<int> *currIdx;
    atomic<int> *emit2Counter;
    atomic<int> *gotToBarrier;

    IntermediateMap intermediateMap;
    vector<K2 *> intermediateMapKeys;

    vector<IntermediateMap> selfIntermediates;
    vector<pthread_mutex_t> selfIntermediateMutexes;

    Barrier barrier;

    vector<JobContext> *contexts;
    vector<pthread_t> *threads;
};

struct JobContext {
    int pid;
    Handler *handler;
};

/**
 * Safe lock a mutex.
 */
void lock_mutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_lock(mutex) != 0) {
        fprintf(stderr, "system error: error on pthread_mutex_lock\n");
        exit(1);
    }
}

/**
 * Safe unlock a mutex.
 */
void unlock_mutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_unlock(mutex) != 0) {
        fprintf(stderr, "system error: error on pthread_mutex_unlock\n");
        exit(1);
    }
}

/**
 * check if two K2 pointers points on two equal values.
 */
bool is_equal(const K2 *first, const K2 *second) {
    K2PointerComp comp;
    return ((!comp(first, second)) && (!comp(second, first)));
}

/**
 * return the location of the given pointer in the given map according to its value.
 */
K2 *find_location(const IntermediateMap & map, K2 *key) {
    for ( auto const & item:map ) {
        if (is_equal(key, item.first)) {
            return item.first;
        }
    }
    return key;
}

/**
 * copies all the threads self intermediate maps to the shared one, and clear them.
 */
void merge_threads_maps(Handler *handler, int & progress, bool & isInMap) {
    for ( unsigned long i = 1; i < handler->selfIntermediates.size(); ++i ) {
        lock_mutex(&(handler->selfIntermediateMutexes)[i]);
        const auto & map = (handler->selfIntermediates)[i];
        for ( auto pair:map ) {
            K2 *key = find_location(map, pair.first);

            vector<V2 *> *value = &pair.second;
            (handler->intermediateMap)[key].insert((handler->intermediateMap)[key].end(), value->begin(),
                                                   value->end());
            progress += value->size();
            if (!isInMap) {
                *handler->sync += uint64_t(value->size());
            }
            (handler->selfIntermediates)[i][pair.first].clear();
        }
        unlock_mutex(&(handler->selfIntermediateMutexes)[i]);
    }
}

/**
 * does the shuffle job.
 */
void shuffle_func(JobContext *context) {
    Handler *handler = context->handler;
    int progress = 0;
    bool isInMap = true;

    while (isInMap) {
        usleep(200);
        // if Map stage is still at sync and map is finished
        if ((unsigned long) handler->gotToBarrier->load() == (handler->threads->size() - 1)) {
            isInMap = false;
            *handler->sync = (uint64_t(SHUFFLE_STAGE) << 62) | (uint64_t(*handler->emit2Counter) << 31) |
                             ((uint64_t(progress) & MASK31));
        }
        merge_threads_maps(handler, progress, isInMap);
    } // finished collecting from all threads
}

/**
 * does one thread map job.
 */
void map_func(JobContext *context) {
    unsigned int idx = 0;
    Handler *handler = context->handler;
    while ((idx = (*handler->currIdx)++) < ((*handler->inputVec).size())) {
        handler->client->map((*handler->inputVec)[idx].first, (*handler->inputVec)[idx].second, context);

        *handler->sync += uint64_t(1);
    }
    // ToDo: remove
    std::cout << "Thread " << context->pid << "  finish map" << std::endl;
}

/**
 * does one thread reduce job.
 */
void reduce_func(JobContext *context) {
    Handler *handler = context->handler;
    unsigned int idx = 0;
    while ((idx = (*handler->currIdx)++) < (handler->intermediateMapKeys.size())) {
        K2 *key = (handler->intermediateMapKeys)[idx];
        handler->client->reduce(key, (handler->intermediateMap)[key], context);

        *handler->sync += uint64_t(1);
    }
}

/**
 * the job of the threads that does the map and reduce functions.
 */
void *map_reduce(void *arg) {
    auto context = (JobContext *) arg;
    map_func(context);

    (*context->handler->gotToBarrier)++;

    //lock
    context->handler->barrier.barrier();

    reduce_func(context);
    return nullptr;
}

/**
 * the job of the single one thread that does the shuffle and reduce functions.
 */
void *shuffle_reduce(void *arg) {
    auto context = (JobContext *) arg;
    Handler *handler = context->handler;

    shuffle_func(context);

    // reset currIdx
    *context->handler->currIdx = 0;

    // calc intermediate keys
    for ( const auto & item : context->handler->intermediateMap ) {
        context->handler->intermediateMapKeys.push_back(item.first);
    }

    *handler->sync = (uint64_t(REDUCE_STAGE) << 62) | (uint64_t(handler->intermediateMapKeys.size()) << 31) |
                     ((uint64_t(0) & MASK31));

    // broadcast
    context->handler->barrier.barrier();

    reduce_func(context);
    return nullptr;
}

/**
 * allocates and initializes all the needed structures to the handler object.
 */
Handler *allocate_handler(const MapReduceClient & client,
                          const InputVec & inputVec, OutputVec & outputVec,
                          int multiThreadLevel) {
    try {
        auto *sync = new atomic<uint64_t>(0);
        auto *currIdx = new atomic<int>(0);
        auto *emit2Counter = new atomic<int>(0);
        auto *gotToBarrier = new atomic<int>(0);

        *sync = (uint64_t(1) << 62) | (uint64_t(inputVec.size()) << 31) | (uint64_t(0));

        auto handler = new Handler{
                .client= &client,
                .inputVec= &inputVec,
                .outputVec= &outputVec,
                .outputVecMutex = PTHREAD_MUTEX_INITIALIZER,
                .sync = sync,
                .currIdx= currIdx,
                .emit2Counter= emit2Counter,
                .gotToBarrier= gotToBarrier,

                .intermediateMap = IntermediateMap(),
                .intermediateMapKeys = vector<K2 *>(),

                .selfIntermediates = vector<IntermediateMap>(multiThreadLevel),
                .selfIntermediateMutexes=vector<pthread_mutex_t>(multiThreadLevel, PTHREAD_MUTEX_INITIALIZER),

                .barrier = Barrier(multiThreadLevel),
                .contexts = nullptr,
                .threads = nullptr,
        };

        return handler;
    }
    catch (std::bad_alloc & e) {
        fprintf(stderr, "system error: %s\n", e.what());
        exit(1);
    }
}

/**
 * converts int to stage_t enum.
 */
stage_t get_job_stage(uint64_t val) {
    switch (val) {
        case 0:
            return UNDEFINED_STAGE;
        case 1:
            return MAP_STAGE;
        case 2:
            return SHUFFLE_STAGE;
        case 3:
            return REDUCE_STAGE;
        default:
            return UNDEFINED_STAGE;
    }
}


//
// API methods:
//


/**
 * adds the given (key,value) to the current thread's intermediate map.
 * should be called from the clients Map method.
 * @param key - mapped key.
 * @param value - mapped value.
 * @param context - information on the current thread, given by the framework
 */
void emit2(K2 *key, V2 *value, void *context) {
    auto cont = (JobContext *) context;
    Handler *handler = cont->handler;
    int pid = cont->pid;

    lock_mutex(&(handler->selfIntermediateMutexes)[pid]);
    (handler->selfIntermediates)[pid][key].push_back(value);
    unlock_mutex(&(handler->selfIntermediateMutexes)[pid]);
    (*handler->emit2Counter)++;
}

/**
 * adds the given (key,value) to output vector.
 * should be called from the clients Reduce method.
 * @param key - reduced key.
 * @param value - reduced value.
 * @param context - information on the current thread, given by the framework
 */
void emit3(K3 *key, V3 *value, void *context) {

    Handler *handler = ((JobContext *) context)->handler;

    lock_mutex(&handler->outputVecMutex);
    handler->outputVec->push_back(OutputPair(key, value));
    unlock_mutex(&handler->outputVecMutex);
}

/**
 * initialize the job handler, and starts all the threads.
 * @param client - reference to the MapReduce client.
 * @param inputVec - reference to the input vector.
 * @param outputVec - reference to the output vector.
 * @param multiThreadLevel - the number of threads to allocate for the job.
 * @return pointer to the framework context.
 */
JobHandle startMapReduceJob(const MapReduceClient & client,
                            const InputVec & inputVec, OutputVec & outputVec,
                            int multiThreadLevel) {
    Handler *handler = allocate_handler(client, inputVec, outputVec, multiThreadLevel);
    vector<JobContext> *contexts = nullptr;
    vector<pthread_t> *threads = nullptr;

    try {
        contexts = new vector<JobContext>(multiThreadLevel);
        threads = new vector<pthread_t>(multiThreadLevel);
    }
    catch (std::exception & e) {
        fprintf(stderr, "system error: %s\n", e.what());
        exit(1);
    }

    for ( int i = 0; i < multiThreadLevel; ++i ) {
        (*contexts)[i] = JobContext{.pid=i, .handler=handler};
    }

    handler->contexts = contexts;
    handler->threads = threads;

    // shuffler
    if (pthread_create(&(*threads)[0], nullptr, shuffle_reduce, &(*contexts)[0]) != 0) {
        fprintf(stderr, "system error: error on pthread_create\n");
        exit(1);
    }

    // maps and reduce
    for ( int i = 1; i < multiThreadLevel; ++i ) {
        if (pthread_create(&(*threads)[i], nullptr, map_reduce, &(*contexts)[i]) != 0) {
            fprintf(stderr, "system error: error on pthread_create\n");
            exit(1);
        }
    }

    return handler;
}

/**
 *  waits for all the threads to finish.
 * @param job - the returned value from startMapReduceJob method.
 */
void waitForJob(JobHandle job) {
    // waits till job is finished
    auto handler = (Handler *) job;

    for ( pthread_t thread : *handler->threads ) {
        pthread_join(thread, nullptr);
    }
}


/**
 * update the given JobState according to the given JobHandle state.
 * @param job - the returned value from startMapReduceJob method.
 * @param state - pointer to the JobState to be updated.
 */
void getJobState(JobHandle job, JobState *state) {
    // update percentage, stage
    auto handler = (Handler *) job;
    uint64_t curSync = handler->sync->load();
    float total = (curSync >> 31) & (MASK31);
    float progress = curSync & (MASK31);
    state->stage = get_job_stage(curSync >> 62);
    state->percentage = progress * 100 / total;
}

/**
 * delete all the allocated memory associated to the given JobHandle.
 * @param job - the returned value from startMapReduceJob method.
 */
void closeJobHandle(JobHandle job) {
    waitForJob(job);
    auto handler = (Handler *) job;

    delete (handler->sync);
    handler->sync = nullptr;

    delete (handler->currIdx);
    handler->currIdx = nullptr;

    delete (handler->emit2Counter);
    handler->emit2Counter = nullptr;

    delete (handler->gotToBarrier);
    handler->gotToBarrier = nullptr;

    delete (handler->threads);
    handler->threads = nullptr;

    delete (handler->contexts);
    handler->contexts = nullptr;

    for ( auto mutex : handler->selfIntermediateMutexes ) {
        if (pthread_mutex_destroy(&mutex) != 0) {
            fprintf(stderr, "system error: error on pthread_cond_destroy\n");
            exit(1);
        }
    }

    if (pthread_mutex_destroy(&handler->outputVecMutex) != 0) {
        fprintf(stderr, "system error: error on pthread_cond_destroy\n");
        exit(1);
    }

    delete (handler);
}
