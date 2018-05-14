
#include <csignal>
#include "myThread.h"

//code from demo_jmp.c
//sigjmp_buf env[MAX_THREAD_NUM]; // all current threads os data (without terminated)

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
            "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif
//////////


myThread::myThread(int id, void (*f)(void)) : tid(id), state(READY), quantum(0), syncedTid(-1), func(f)
{
    address_t sp, pc;
    sp = (address_t)stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t)f;
    sigsetjmp(env, 1);
    (env->__jmpbuf)[JB_SP] = translate_address(sp);
    (env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&env->__saved_mask);
}

int myThread::getTid() const
{
    return tid;
}

int myThread::getState() const
{
    return state;
}

void myThread::setState(int newState)
{
    state = newState;
}

char *myThread::getStack()
{
    return stack;
}

int myThread::getEnvIdx() const
{
    return envIdx;
}

void myThread::setEnvIdx(int envIdx)
{
    myThread::envIdx = envIdx;
}

int myThread::getQuantum() const
{
    return quantum;
}

void myThread::setQuantum(int quantum)
{
    myThread::quantum = quantum;
}

int myThread::getSyncedTid() const
{
    return syncedTid;
}

void myThread::setSyncedTid(int syncedTid)
{
    myThread::syncedTid = syncedTid;
}

bool myThread::getIsBlockedNotBySynced() const
{
    return isBlockedNotBySynced;
}

void myThread::setIsBlockedNotBySynced(bool isBlockedNotBySynced)
{
    myThread::isBlockedNotBySynced = isBlockedNotBySynced;
}


