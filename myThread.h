#ifndef EX2_MYTHREAD_H
#define EX2_MYTHREAD_H

#include <csetjmp>
#include "uthreads.h"

#define READY 0
#define RUNNING 1
#define BLOCKED 2


class myThread{

private:
    int tid, state, envIdx, quantum, syncedTid;
    char stack[STACK_SIZE];
    void (*func)(void);
    bool isBlockedNotBySynced = false;

public:
    sigjmp_buf env;

    myThread(int id, void (*f)(void));
    int getTid() const;
    int getState() const;
    char* getStack();
    void setState(int newState);
    int getEnvIdx() const;
    void setEnvIdx(int envIdx);
    int getQuantum() const;
    void setQuantum(int quantum);
    int getSyncedTid() const;
    void setSyncedTid(int syncedTid);
    bool getIsBlockedNotBySynced() const;
    void setIsBlockedNotBySynced(bool isBlockedNotBySynced);
};

#endif