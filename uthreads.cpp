#include <iostream>
#include <vector>
#include "uthreads.h"
#include "myThread.h"
#include <csignal>
#include <sys/time.h>

#define ERROR (-1)
#define ERROR_LIB_MSG "thread library error: "
#define ERROR_SYS_MSG "system error: "
#define READY 0
#define RUNNING 1
#define BLOCKED 2
#define EXPIRED_TIME 0
#define BLOCKED_THREAD_ITSELF 1

using std::cerr;
using std::vector;

vector<myThread*> gReadyThreadsList; // ready threads as objects
// all current threads as objects (without terminated):
myThread* gCurrentThreadsList[MAX_THREAD_NUM] = {nullptr};
// binary array - 1 tells that this idx is full by thread, 0 other:
int envBinaryThreadPlaces[MAX_THREAD_NUM] = {0};
int tidCounter = 0;
myThread *runningThread = nullptr;
int totalQuantum = 0;
bool blockCalledFromSync = false;

struct sigaction sa;
struct itimerval timer;
sigset_t set;

/**
 * releases the synced threads
 * @param tid terminated tid
 */
void releaseSynced(int tid)
{
    for (auto& thread : gCurrentThreadsList)
    {
        if (thread == nullptr)
            continue;
        if (thread->getSyncedTid() == tid)
        {
            if (!thread->getIsBlockedNotBySynced())
            {
                thread->setState(READY);
                thread->setSyncedTid(-1);
                gReadyThreadsList.push_back(thread);
            }
        }
    }
}

/**
 * delete all the threads
 */
void deleteAllThreads()
{
    for (auto &thread : gCurrentThreadsList)
    {
        if (thread != nullptr)
        {
            delete thread;
            thread = nullptr;
        }
    }
}

/**
 * block signals
 */
void blockSignals()
{
    if (sigprocmask(SIG_BLOCK, &set, nullptr))
    {
        cerr << ERROR_SYS_MSG << "signal mask error\n";
        deleteAllThreads();
        exit(ERROR);
    }
}

/**
 * unblock signals
 */
void unBlockSignals()
{
    if (sigprocmask(SIG_UNBLOCK, &set, nullptr))
    {
        cerr << ERROR_SYS_MSG << "signal mask error\n";
        deleteAllThreads();
        exit(ERROR);
    }
}

/**
 * responsible to valid if the tid exists
 * @return true if exists, false otherwise
 */
bool isExistTid(int tid)
{
    for (auto& thread : gCurrentThreadsList)
    {
        if (thread == nullptr)
            continue;
        if (thread->getTid() == tid)
            return true;
    }
    return false;
}

/**
 * responsible to return the number of all current threads
 * @return the number of all current threads
 */
int getCurrentThreadsNumber()
{
    int result = 0;
    for (int binaryThreadPlace : envBinaryThreadPlaces)
    {
        result += binaryThreadPlace;
    }
    return result;
}

/**
 * responsible to return the first free place in env array
 * @return the first free place in env array (if there is no free place - return -1)
 */
int getLowerFreePlace()
{
    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        if (envBinaryThreadPlaces[i] == 0)
            return i;
    }
    return -1;
}

/**
 * responsible to return the index of thread by tid
 * @param tid given tid
 * @return the index of thread by tid (if not exists - return -1)
 */
int getIndexOfThreadByTid(int tid)
{
    if (!isExistTid(tid))
        return -1;
    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        if (gCurrentThreadsList[i] != nullptr)
            if (gCurrentThreadsList[i]->getTid() == tid)
                return i;
    }
    return -1;
}

/**
 * deletes thread from ready vector by tid
 * @param tid - given tid
 * @return true if found and deleted, false otherwise
 */
bool deleteThreadFromReadyVector(int tid)
{
    bool isFound = false;
    int idx = 0;
    for (auto &i : gReadyThreadsList)
    {
        if (i)
        {
            if (i->getTid() != tid)
                ++idx;
            else
            {
                isFound = true;
                break;
            }
        }
    }
    if (isFound)
        gReadyThreadsList.erase(gReadyThreadsList.begin() + (idx));
    else
        cerr << "thread not found\n";
    return isFound;
}

/**
 * return true if switched
 * @param caseOfSwitch reason why to switch
 * @return true if switched, false - otherwise
 */
bool switchThreads(int caseOfSwitch)
{
    int ret_val;
    ++totalQuantum;
    switch (caseOfSwitch)
    {
        case EXPIRED_TIME:
            ret_val = sigsetjmp(runningThread->env, 1);
            if (ret_val == 1)
            {
                return false;
            }
            runningThread->setState(READY);
            gReadyThreadsList.push_back(runningThread);
            runningThread = gReadyThreadsList.front();
            runningThread->setState(RUNNING);
            runningThread->setQuantum(runningThread->getQuantum()+1);
            gReadyThreadsList.erase(gReadyThreadsList.begin());
            siglongjmp(runningThread->env, 1);

        case BLOCKED_THREAD_ITSELF:
            ret_val = sigsetjmp(runningThread->env, 1);
            if (ret_val == 1)
            {
                return false;
            }
            runningThread->setState(BLOCKED);
            runningThread = gReadyThreadsList.front();
            runningThread->setState(RUNNING);
            runningThread->setQuantum(runningThread->getQuantum()+1);
            gReadyThreadsList.erase(gReadyThreadsList.begin());
            siglongjmp(runningThread->env, 1);
            return true;

        default:
            return false;
    }
}

/**
 * round robin signal_handler algorithm
 * @param sig signal number
 */
void signal_handler(int sig)
{
    switchThreads(EXPIRED_TIME);
}

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs)
{
    if (quantum_usecs <= 0)
    {
        cerr << ERROR_LIB_MSG << "quantum length is negative\n";
        return ERROR;
    }
    ++totalQuantum;

    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &signal_handler;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        cerr << ERROR_SYS_MSG << "sigaction failed\n";
        deleteAllThreads();
        exit(EXIT_FAILURE);
    }
    auto* mainThread = new myThread(tidCounter++, nullptr);
    mainThread->setState(RUNNING);
    runningThread = mainThread;
    runningThread->setQuantum(runningThread->getQuantum()+1);
    if (getLowerFreePlace() != -1)
    {
        gCurrentThreadsList[getLowerFreePlace()] = mainThread;
        envBinaryThreadPlaces[getLowerFreePlace()] = 1;
    }
    timer.it_value.tv_sec = 0; // first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs; // first time interval, microseconds part
    timer.it_interval.tv_sec = 0; // following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usecs; // following time intervals, microseconds part

    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer (ITIMER_VIRTUAL, &timer, nullptr))
    {
        cerr << ERROR_SYS_MSG << "setitimer failed\n";
        deleteAllThreads();
        exit(ERROR);
    }
    return EXIT_SUCCESS;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void))
{
    blockSignals();
    if (getCurrentThreadsNumber() == MAX_THREAD_NUM)
    {
        cerr << ERROR_LIB_MSG << "too much threads available\n";
        unBlockSignals();
        return ERROR;
    }
    if (f == nullptr)
    {
        cerr << ERROR_LIB_MSG << "entry point function is null\n";
        unBlockSignals();
        return ERROR;
    }
    tidCounter = getLowerFreePlace();
    auto* newThread = new myThread(tidCounter, f);
    newThread->setState(READY);
    gReadyThreadsList.push_back(newThread);
    if (getLowerFreePlace() != -1)
    {
        gCurrentThreadsList[getLowerFreePlace()] = newThread;
        envBinaryThreadPlaces[getLowerFreePlace()] = 1;
    }
    unBlockSignals();
    return tidCounter;
}

/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered as an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{
    blockSignals();
    if (tid < 0)
    {
        cerr << ERROR_LIB_MSG << "tid is not valid\n";
        unBlockSignals();
        return ERROR;
    }
    if (tid == 0) // main thread
    {
        deleteAllThreads();
        unBlockSignals();
        exit(EXIT_SUCCESS);
    }
    if (!isExistTid(tid))
    {
        cerr << ERROR_LIB_MSG << "tid is not exists\n";
        unBlockSignals();
        return ERROR;
    }
    int indexOfDeletedThread = getIndexOfThreadByTid(tid);
    if (indexOfDeletedThread == -1)
    {
        cerr << ERROR_LIB_MSG << "tid is not exists\n";
        unBlockSignals();
        return ERROR;
    }
    if (gCurrentThreadsList[indexOfDeletedThread]->getState() == READY)
    {
        deleteThreadFromReadyVector(tid);
    }
    if (runningThread->getTid() == tid) // case terminate itself
    {
        int ret_val = sigsetjmp(runningThread->env, 1);
        if (ret_val == 1)
        {
            return false;
        }
        runningThread = gReadyThreadsList.front();
        runningThread->setState(RUNNING);
        runningThread->setQuantum(runningThread->getQuantum()+1);
        gReadyThreadsList.erase(gReadyThreadsList.begin());
        delete gCurrentThreadsList[indexOfDeletedThread];
        gCurrentThreadsList[indexOfDeletedThread] = nullptr;
        envBinaryThreadPlaces[indexOfDeletedThread] = 0;
        releaseSynced(tid);
        unBlockSignals();
        tidCounter = getLowerFreePlace();
        if (setitimer (ITIMER_VIRTUAL, &timer, nullptr))
        {
            cerr << ERROR_SYS_MSG << "setitimer failed\n";
            deleteAllThreads();
            exit(ERROR);
        }
        siglongjmp(runningThread->env, 1);
        return 0;
    }
    delete gCurrentThreadsList[indexOfDeletedThread];
    gCurrentThreadsList[indexOfDeletedThread] = nullptr;
    envBinaryThreadPlaces[indexOfDeletedThread] = 0;
    releaseSynced(tid);
    unBlockSignals();
    tidCounter = getLowerFreePlace();
    return 0;
}

/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered as an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    blockSignals();
    if (tid < 0)
    {
        cerr << ERROR_LIB_MSG << "tid is not valid\n";
        unBlockSignals();
        return ERROR;
    }
    if (tid == 0) // main thread
    {
        cerr << ERROR_LIB_MSG << "you can't block main thread\n";
        unBlockSignals();
        return ERROR;
    }
    if (!isExistTid(tid))
    {
        cerr << ERROR_LIB_MSG << "tid is not exists\n";
        unBlockSignals();
        return ERROR;
    }
    if (runningThread->getTid() == tid) // thread block itself
    {
        if (!blockCalledFromSync)
            runningThread->setIsBlockedNotBySynced(true);
        blockCalledFromSync = false;
        if (setitimer (ITIMER_VIRTUAL, &timer, nullptr))
        {
            cerr << ERROR_SYS_MSG << "setitimer failed\n";
            deleteAllThreads();
            exit(ERROR);
        }
        switchThreads(BLOCKED_THREAD_ITSELF);
    }
    else // thread block other
    {
        int threadPlace = getIndexOfThreadByTid(tid);
        if (threadPlace != -1)
            if (gCurrentThreadsList[threadPlace]->getState() == READY)
            {
                gCurrentThreadsList[threadPlace]->setState(BLOCKED);
                gCurrentThreadsList[threadPlace]->setIsBlockedNotBySynced(true);
                deleteThreadFromReadyVector(tid);
            }
    }
    unBlockSignals();
    return 0;
}

/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered as an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
    blockSignals();
    if (tid < 0)
    {
        cerr << ERROR_LIB_MSG << "tid is not valid\n";
        unBlockSignals();
        return ERROR;
    }
    if (!isExistTid(tid))
    {
        cerr << ERROR_LIB_MSG << "tid is not exists\n";
        unBlockSignals();
        return ERROR;
    }
    int indexOfResumedThread = getIndexOfThreadByTid(tid);
    if (indexOfResumedThread == -1)
    {
        cerr << ERROR_LIB_MSG << "tid is not exists\n";
        unBlockSignals();
        return ERROR;
    }
    gCurrentThreadsList[indexOfResumedThread]->setIsBlockedNotBySynced(false);
    if (gCurrentThreadsList[indexOfResumedThread]->getState() == BLOCKED)
    {
        if (gCurrentThreadsList[indexOfResumedThread]->getSyncedTid() == -1)
        {
            gCurrentThreadsList[indexOfResumedThread]->setState(READY);
            gReadyThreadsList.push_back(gCurrentThreadsList[indexOfResumedThread]);
        }
    }
    unBlockSignals();
    return 0;
}

/*
 * Description: This function blocks the RUNNING thread until thread with
 * ID tid will terminate. It is considered an error if no thread with ID tid
 * exists, if thread tid calls this function or if the main thread (tid==0) calls this function.
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision
 * should be made.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_sync(int tid)
{
    blockSignals();
    if (tid < 0)
    {
        cerr << ERROR_LIB_MSG << "tid is not valid\n";
        unBlockSignals();
        return ERROR;
    }
    if (runningThread->getTid() == 0) // main thread calls the function is error
    {
        cerr << ERROR_LIB_MSG << "you can't call sync function from main thread\n";
        unBlockSignals();
        return ERROR;
    }
    if (!isExistTid(tid))
    {
        cerr << ERROR_LIB_MSG << "tid is not exists\n";
        unBlockSignals();
        return ERROR;
    }
    if (runningThread->getTid() == tid) // case 'thread tid calls this function'
    {
        cerr << ERROR_LIB_MSG << "thread tid calls this function\n";
        unBlockSignals();
        return ERROR;
    }
    runningThread->setSyncedTid(tid);
    blockCalledFromSync = true;
    if (uthread_block(runningThread->getTid()) == ERROR)
    {
        cerr << ERROR_LIB_MSG << "sync failed\n";
        unBlockSignals();
        return ERROR;
    }
    unBlockSignals();
    return ERROR;
}

/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid()
{
    return runningThread->getTid();
}

/*
 * Description: This function returns the total number of quantums that were
 * started since the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums()
{
    return totalQuantum;
}

/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered as an error.
 * Return value: On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid)
{
    if (tid < 0)
    {
        cerr << ERROR_LIB_MSG << "tid is not valid\n";
        return ERROR;
    }
    if (!isExistTid(tid))
    {
        cerr << ERROR_LIB_MSG << "tid is not exists\n";
        return ERROR;
    }
    int indexOfQuantumedThread = getIndexOfThreadByTid(tid);
    return gCurrentThreadsList[indexOfQuantumedThread]->getQuantum();
}
