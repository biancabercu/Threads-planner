#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include "so_scheduler.h"

// new,ready,running,waiting, terminated = 0,1,2,3,4
typedef struct {
	tid_t tid;
	// thread_status status;
	unsigned int th_status;
	so_handler *thread_handler;
	unsigned int thread_time;
	unsigned int thread_prio;
	unsigned int thread_io;
	sem_t runState;
} thread;

typedef struct {
	thread *thread_running;
	thread *queue[500];
	thread *total_threads[500];
	unsigned int nr_threads;
	unsigned int size_priorityq;
} scheduler;

static scheduler sched;

unsigned int scheduler_time_q;
unsigned int scheduler_io;
unsigned int scheduler_allocated;

int so_init(unsigned int time_quantum, unsigned int io)
{
	int rc;

	if (time_quantum <= 0 || io > SO_MAX_NUM_EVENTS)
		return -1;
	if (scheduler_allocated)
		return -1;

	scheduler_time_q = time_quantum;
	scheduler_io = io;
	scheduler_allocated = 1;

	sched.nr_threads = 0;
	sched.size_priorityq = 0;
	sched.thread_running = NULL;

	return 0;
}

void *thread_func(void *args)
{
	thread *t;
	int rc;

	t = (thread *)args;

	//wait to be planned
	rc = sem_wait(&t->runState);

	//call its handler
	t->thread_handler(t->thread_prio);

	//anounce scheduler that my thread is finished
	t->th_status = 4;
	refresh_sched();
	return NULL;
}

tid_t so_fork(so_handler *func, unsigned int priority)
{
	int i, j;

	if (priority > SO_MAX_PRIO)
		return INVALID_TID;
	if (func == NULL)
		return INVALID_TID;

	int rc;
	thread *t;

	t = (thread *)malloc(sizeof(thread));

	//initializam si thread-ul pe care l-am creat
	t->tid = INVALID_TID;
	t->thread_io = SO_MAX_NUM_EVENTS;
	t->th_status = 0;
	t->thread_time = scheduler_time_q;
	t->thread_prio = priority;
	t->thread_handler = func;

	//+ semaforul pentru thread
	rc = sem_init(&t->runState, 0, 0);

	//dau un start pentru thread
	rc = pthread_create(&t->tid, NULL, &thread_func, (void *)t);
	//pana aici ar fi initalizarea

	//punem in planificator thread-ul nou
	sched.total_threads[sched.nr_threads++] = t;

	for (i = 0; i < sched.size_priorityq; i++)
		if (sched.queue[i]->thread_prio >= t->thread_prio)
			break;
	for (j = sched.size_priorityq; j > i; j--)
		sched.queue[j] = sched.queue[j - 1];

	sched.size_priorityq++;
	sched.queue[i] = t;
	sched.queue[i]->th_status = 1;

	//putting the scheduler in action
	if (sched.thread_running == NULL)
		refresh_sched();
	else
		so_exec();

	return t->tid;
}

int so_wait(unsigned int io)
{
	if (io >= scheduler_io)
		return -1;

	sched.thread_running->th_status = 0;
	sched.thread_running->th_status = 0;
	sched.thread_running->thread_io = io;
	so_exec();
	return 0;
}

int so_signal(unsigned int io)
{

	int i;
	int k, l;
	int nrth_woke;

	if (io >= scheduler_io)
		return -1;

	nrth_woke = 0;
	i = 0;
	while (i < sched.nr_threads) {
		if (sched.total_threads[i]->thread_io == io &&
			sched.total_threads[i]->th_status == 0) {
			sched.total_threads[i]->thread_io = SO_MAX_NUM_EVENTS;
			sched.total_threads[i]->th_status = 1;

			for (k = 0; k < sched.size_priorityq; k++)
				if (sched.queue[k]->thread_prio >=
				sched.total_threads[i]->thread_prio)
					break;
			for (l = sched.size_priorityq; l > k; l--)
				sched.queue[l] = sched.queue[l - 1];

			sched.size_priorityq++;
			sched.queue[k] = sched.total_threads[i];
			sched.queue[k]->th_status = 1;

			nrth_woke++;
		}
		i++;
	}
	so_exec();
	return nrth_woke;
}

void so_exec(void)
{
	int rc;
	thread *t;

	t = sched.thread_running;
	t->thread_time--;

	//putting the scheduler in action
	refresh_sched();

	//aquire if poss
	rc = sem_wait(&t->runState);
}

void refresh_sched(void)
{
	int rc;
	int i, j;
	thread *nextThread;
	thread *currentThread;

	currentThread = sched.thread_running;
	if (sched.size_priorityq == 0) {
		//release
		sem_post(&currentThread->runState);
	} else {
		//take the next thread from queue
		nextThread = sched.queue[sched.size_priorityq - 1];

		//if no thread is in running state?
		if (sched.thread_running == NULL) {
			sched.thread_running = nextThread;
			start_thread(nextThread);
			return;
		}
		//if the current thread is W or T
		if (currentThread->th_status == 0 ||
			currentThread->th_status == 4) {
			sched.thread_running = nextThread;
			start_thread(nextThread);
			return;
		}

		//if the time is up
		if (currentThread->thread_time <= 0) {
			if (currentThread->thread_prio ==
				nextThread->thread_prio) {

				for (i = 0; i < sched.size_priorityq; i++)
					if (sched.queue[i]->thread_prio >=
					currentThread->thread_prio)
						break;
				for (j = sched.size_priorityq; j > i; j--)
					sched.queue[j] = sched.queue[j - 1];

				sched.size_priorityq++;
				sched.queue[i] = currentThread;
				sched.queue[i]->th_status = 1;

				sched.thread_running = nextThread;
				start_thread(nextThread);
				return;
			}
			currentThread->thread_time = scheduler_time_q;
		}
		sem_post(&currentThread->runState);
	}
}

void start_thread(thread *t)
{
	int rc;

	sched.queue[sched.size_priorityq] = NULL;
	sched.size_priorityq--;
	t->th_status = 2;
	t->thread_time = scheduler_time_q;
	rc = sem_post(&t->runState); //release semaphore
}

void so_end(void)
{
	int i;
	int rc;

	if (!scheduler_allocated)
		return;

	//W after total_threads
	for (i = 0; i < sched.nr_threads; i++)
		rc = pthread_join(sched.total_threads[i]->tid, NULL);

	for (i = 0; i < sched.nr_threads; i++) {
		rc = sem_destroy(&sched.total_threads[i]->runState);
		free(sched.total_threads[i]);
	}

	//can "reallocate"
	scheduler_allocated = 0;
}
