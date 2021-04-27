#ifndef MAPREDUCEFRAMEWORK_H
#define MAPREDUCEFRAMEWORK_H

#include "MapReduceClient.h"

typedef void *JobHandle;

enum stage_t {
    UNDEFINED_STAGE = 0, MAP_STAGE = 1, SHUFFLE_STAGE = 2, REDUCE_STAGE = 3
};

typedef struct {
    stage_t stage;
    float percentage;
} JobState;

/**
 * adds the given (key,value) to the current thread's intermediate map.
 * should be called from the clients Map method.
 * @param key - mapped key.
 * @param value - mapped value.
 * @param context - information on the current thread, given by the framework
 */
void emit2(K2 *key, V2 *value, void *context);

/**
 * adds the given (key,value) to output vector.
 * should be called from the clients Reduce method.
 * @param key - reduced key.
 * @param value - reduced value.
 * @param context - information on the current thread, given by the framework
 */
void emit3(K3 *key, V3 *value, void *context);

/**
 * initialize the job handler, and starts all the threads.
 * @param client - reference to the MapReduce client.
 * @param inputVec - reference to the input vector.
 * @param outputVec - reference to the output vector.
 * @param multiThreadLevel - the number of threads to allocate for the job.
 * @return pointer to the framework context (for passing it back later on).
 */
JobHandle startMapReduceJob(const MapReduceClient & client,
                            const InputVec & inputVec,
                            OutputVec & outputVec,
                            int multiThreadLevel);

/**
 *  waits for all the threads to finish.
 * @param job - the returned value from startMapReduceJob method.
 */
void waitForJob(JobHandle job);


/**
 * update the given JobState according to the given JobHandle state.
 * @param job - the returned value from startMapReduceJob method.
 * @param state - pointer to the JobState to be updated.
 */
void getJobState(JobHandle job, JobState *state);

/**
 * delete all the allocated memory associated to the given JobHandle.
 * @param job - the returned value from startMapReduceJob method.
 */
void closeJobHandle(JobHandle job);


#endif //MAPREDUCEFRAMEWORK_H
