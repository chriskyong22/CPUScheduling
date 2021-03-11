// File:	rpthread.c

// List all group member's name: Christopher Yong (cy287), Joshua Ross (jjr276)
// username of iLab: 
// iLab Server: cp.cs.rutgers.edu

#include "rpthread.h"

// INITAILIZE ALL YOUR VARIABLES HERE
// YOUR CODE HERE
#define THREAD_STACK_SIZE SIGSTKSZ
#define MAX_PRIORITY 4
#define BOOST_AFTER_TIME_SLICE threadID * 2

rpthread_t threadID = 1;
uint mutexID = 1;

Queue* blockedQueue = NULL;
Queue* exitQueue = NULL;
Queue* joinQueue = NULL;
Queue* terminatedAndJoinedQueue = NULL;

schedulerNode* scheduleInfo = NULL; 
ucontext_t scheduler = {0}; // Scheduler context
ucontext_t exitContext = {0}; // Exit context
tcb* current = NULL; // Current non-scheduler tcb (including context)

volatile int blockedQueueMutex = 0;
volatile int threadIDMutex = 0;
volatile int mutexIDMutex = 0;

struct itimerval timer = {0};
struct itimerval zero = {0};
struct sigaction signalHandler = {0};

static void sched_mlfq();
static void sched_rr();
static void schedule();
static void initializeContext(ucontext_t*, ucontext_t*);
static Queue* initializeQueue();
static void initializeScheduleQueues();
static tcb* initializeTCB();
static tcb* initializeTCBHeaders();
static void initializeScheduler();
static void enqueue(Queue*, tcb* threadControlBlock);
static tcb* dequeue(Queue*);
static void freeQueue(Queue* queue);
static tcb* findFirstOfJoinQueue(Queue* queue, rpthread_t thread);
static tcb* findFirstOfQueue(Queue* queue, rpthread_t thread);
static int checkExistBlockedQueue(Queue* queue, int mutexID);
static int checkExistQueue(Queue* queue, int threadId);
static int checkExistJoinQueue(Queue* queue, int threadId);
static void initializeTimer();
static void initializeSignalHandler();
static void timer_interrupt_handler(int signum);
static void startTimer();
static void disableTimer();
static void pauseTimer();
static void resumeTimer();
static void printQueue(Queue* queue);

// Hashtable functions
#define HASHTABLE_LEN 10000

linkedList *hashTable[HASHTABLE_LEN];
volatile int hashMutex = 0;
static void hash_init();
static int hash(int key);
static tcb *hash_search(int key);
static void hash_insert(tcb *toAdd);
static QueueNode *hash_remove(int threadID);

/*
** rpthread functions
*/

/* create a new thread */
int rpthread_create(rpthread_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

	// create Thread Control Block
	// create and initialize the context of this thread
	// allocate space of stack for this thread to run
	// after everything is all set, push this thread int
	// YOUR CODE HERE
	
	// First time being run, therefore it will have to create the main thread, 
	// new thread, and the scheduler thread.
	if (scheduler.uc_stack.ss_size == 0) {
		//printf("[D]: Initial Setup\n");
		initializeScheduler();
		initializeScheduleQueues();

		initializeContext(&exitContext, NULL);
		makecontext(&exitContext, (void*) rpthread_exit, 1, NULL);

		tcb* mainTCB = initializeTCB();
		//printf("Main thread %d\n", mainTCB->id);
		current = mainTCB;
		current->status = READY;
		hash_init();
		hash_insert(mainTCB);

		/**
			The main context does not need to be malloced technically but I
			am mallocing it so if the user decides to pthread_exit the main
			context, we don't run into a free issue where it tries to free
			the context stack (I am not entirely sure what happens if you 
			try to free the main context or if the main context is malloced) 
			so I will be mallocing the main context stack. Note this will not 
			affect the outcome because the main context should swapcontext
			to exit out and thus will save the current context and therefore
			the behavior should be the same. 
		*/
		initializeSignalHandler();
		initializeTimer();
	}
	//printf("[D]: Creating new child thread.\n");
	tcb* newThreadTCB = initializeTCB();
	(*thread) = newThreadTCB->id;
	makecontext(&(newThreadTCB->context), (void*)function, 1, arg);
	enqueue(&scheduleInfo->priorityQueues[newThreadTCB->priority - 1], newThreadTCB);
	hash_insert(newThreadTCB);
	//printf("[D]: Done creating the child thread, resuming!\n");
	//if (current != NULL) printf("Current id %d\n", current->id);
	//pauseTimer();
	//printQueue(&scheduleInfo->priorityQueues[MAX_PRIORITY-1]);
	//resumeTimer();
	return 0;
};

/* give CPU possession to other user-level threads voluntarily */
int rpthread_yield() {
	// change thread state from Running to Ready
	// save context of this thread to its thread control block
	// switch from thread context to scheduler context
	disableTimer();
	//printf("Entered PThread Yield\n");
	//if (current != NULL) printf("Current id %d\n", current->id);
	swapcontext(&(current->context), &(scheduler));

	return 0;
};

/* terminate a thread */
void rpthread_exit(void *value_ptr) {
	// Deallocated any dynamic memory created when starting this thread
	// YOUR CODE HERE
	disableTimer();
	//printf("Entered PThread Exit\n");
	//printf("Current id %d\n", current->id);
	//printQueue(&scheduleInfo->priorityQueues[MAX_PRIORITY - 1]);
	current->exitValue = value_ptr;
	
	//printf("[D]: Attempting to find a thread that is waiting on this thread %d\n", current->id);
	//tcb* joinThread = findFirstOfJoinQueue(joinQueue, current->id);

	if (current->joinTID != 0) {
		//printf("In id %d exit, trying to join %d\n", current->id, current->joinTID);
		tcb *joinThread = hash_search(current->joinTID);
		if (joinThread != NULL) {
			joinThread->status = READY;
			joinThread->exitValue = current->exitValue;
			//printf("[D]: Found a thread %d that is waiting on this exiting thread %d, putting it on the ready queue\n", joinThread->id, current->id);
			enqueue(&scheduleInfo->priorityQueues[joinThread->priority - 1], joinThread);
			//printQueue(&scheduleInfo->priorityQueues[MAX_PRIORITY - 1]);
		}
	} 
	current->status = EXITED;
	// Free stack
	//printf("EXITED %d\n", current->id);
	free(current->context.uc_stack.ss_sp); 
	current->context.uc_stack.ss_sp = NULL;
	current->context.uc_stack.ss_size = 0;
	current = NULL;
	// Swap to scheduler
	setcontext(&(scheduler));
};


/* Wait for thread termination */
int rpthread_join(rpthread_t thread, void **value_ptr) {
	
	// wait for a specific thread to terminate
	// de-allocate any dynamic memory created by the joining thread
	//pauseTimer();
	tcb *toJoin = hash_search(thread);

	// Check if the thread is invalid or already being joined
	if (thread == current->id || toJoin == NULL || toJoin->joinTID != 0) {
		return -1;
	}

	// Never release mutex since a thread can only be joined once
	if (__sync_lock_test_and_set(&(toJoin->joinMutex), 1)) return -1;

	// Check if the thread has not already exited
	if (toJoin->status != EXITED) { // Need to join the thread
		toJoin->joinTID = current->id;
		current->status = WAITING;
		swapcontext(&(current->context), &(scheduler));
	}
	//printf("Joining %d from %d\n", toJoin->id, current->id);
	if (value_ptr != NULL) *value_ptr = toJoin->exitValue;
	//toJoin->status = KILLED; // Temporary
	free(hash_remove(toJoin->id));
	// TODO: Remove from hashtable instead of setting status
	return 0;
};

/*
** Mutex Functions
*/

/* initialize the mutex lock */
int rpthread_mutex_init(rpthread_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//initialize data structures for this mutex
	// YOUR CODE HERE
	//printf("Entered Mutex Init\n");
	if (mutex == NULL){
		return -1;
	}
	
	//Assuming rpthread_mutex_t has already been malloced otherwise we would have to return a pointer (since we would be remallocing it)
	while (__sync_lock_test_and_set(&(mutexIDMutex), 1)) rpthread_yield();
	mutex->id = mutexID++;
	__sync_lock_release(&(mutexIDMutex)); 
	mutex->tid = 0; 
	mutex->lock = 0;
	mutex->waitingThreadID = 0;
	
	return 0;
};

/* aquire the mutex lock */
int rpthread_mutex_lock(rpthread_mutex_t *mutex) {
        // use the built-in test-and-set atomic function to test the mutex
        // if the mutex is acquired successfully, enter the critical section
        // if acquiring mutex fails, push current thread into block list and 
        // context switch to the scheduler thread

		if (mutex == NULL) return -1;

		while (__sync_lock_test_and_set(&(mutex->lock), 1)) {
			disableTimer(); // Need rest of block to execute continuously and we yield anyway
			//printf("[D]: Current thread %d wants a mutex %d but it is already locked by thread %d.\n", current->id, mutex->id, mutex->tid);
			current->desiredMutex = mutex->id;
			//printf("[D]: Adding current thread to the block queue\n");
			enqueue(blockedQueue, current); 
			current->status = BLOCKED;
			//printf("[D]: Succesfully added current thread %d to the block queue\n", current->id);

			//swapcontext(&(current->context), &(scheduler)); // The blocked thread will return here and thus we will have to check if the mutex is still locked or not.
			rpthread_yield();

			//printf("[D]: Resuming thread %d, Mutex %d was unlocked and now can maybe be obtained!\n", current->id, mutex->id);
			/*
			if (mutex->waitingThreadID != current->id) {
        		printf("[D]: ERROR in waitingThreadID %d, should always be the current->id or this thread %d\n", mutex->waitingThreadID, current->id);
        	}
        	*/
			mutex->waitingThreadID = 0;
			
        } 
    	//printf("[D]: This thread %d is locking this mutex!\n", current->id);
    	mutex->tid = current->id;
    	__sync_lock_release(&(blockedQueueMutex)); 
        
        
        return 0;
};

/* release the mutex lock */
int rpthread_mutex_unlock(rpthread_mutex_t *mutex) {
	// Release mutex and make it available again. 
	// Put threads in block list to run queue 
	// so that they could compete for mutex later.

	// Checking if the thread has the mutex so other threads cannot just unlock 
	// mutexes they do not have or if the mutex is unlocked already
	if (mutex == NULL || mutex->tid != current->id || mutex->lock == 0) {
		// char* debug = "[D]: Entered a bad place\n";
		// write(1, &debug, sizeof(debug));
		return -1;
	}

	mutex->tid = 0;
	__sync_lock_release(&(mutex->lock));

	//If we do not have a thread in the readyQueues, already waiting for this mutex, find and add one. 
	if (mutex->waitingThreadID == 0) {
		// First come, first serve implementation of a mutex.
		tcb* unblockedThread = findFirstOfBlockedQueue(blockedQueue, mutex->id);
		if (unblockedThread != NULL) {
			mutex->waitingThreadID = unblockedThread->id;// The reason for this, is so that we can only have one THREAD (FIRST COME FIRST SERVE) where if the mutex is unlocked, can MAYBE obtain the mutex however if the current thread still needs the mutex and has time slice remaining, allow it to potentially lock the mutex again.
			//__sync_lock_release(&(blockedQueueMutex)); 
			//printf("[D]: Succesfully found a thread %d that is waiting on this mutex %d which is locked by thread %d, adding the thread to the ready queue\n", unblockedThread->id, mutex->id, current->id);
			unblockedThread->status = READY;
			unblockedThread->desiredMutex = 0;
			enqueue(&scheduleInfo->priorityQueues[unblockedThread->priority - 1], unblockedThread);
		} 
	}

	//printf("[D]: Mutex is now unlocked.\n");

	return 0;
};

/* destroy the mutex */
int rpthread_mutex_destroy(rpthread_mutex_t *mutex) {
	// Deallocate dynamic memory created in rpthread_mutex_init
	pauseTimer();
	//printf("Entered Mutex Destroy\n");
	if (mutex == NULL) {
		resumeTimer();
		return -1;
	}

	int threadWaiting = checkExistBlockedQueue(blockedQueue, mutex->id);
	if (threadWaiting == 1 || mutex->lock == 1 || mutex->waitingThreadID != 0) {
		// A thread is waiting or using this mutex, what to do? 
		// Currently going to just return -1 or error. 
		resumeTimer();
		return -1;
	}

	// No thread is waiting for this mutex, can destroy freely 
	mutex->id = 0;
	mutex->tid = 0; 
	mutex->lock = 0;
	mutex->waitingThreadID = 0;

	resumeTimer();
	return 0;
}

static void freeQueue(Queue* queue) {
	QueueNode* current = queue->head;
	while (current != NULL) {
		QueueNode* temp = current;
		free(temp->node);
		current = current->next;
		free(temp);
	}
	free(queue);
}

/* 
** Scheduling Functions
*/
static void schedule() {
	//if (current != NULL) printf("[D]: Entered Scheduler as %d\n", current->id);
	// Every time when timer interrup happens, your thread library 
	// should be contexted switched from thread context to this 
	// schedule function

	// Invoke different actual scheduling algorithms
	// according to policy (RR or MLFQ)

	// YOUR CODE HERE
	
	//printf("[D]: Entered Scheduler\n");
	//if (current != NULL) printf("Current id %d\n", current->id);

	//NOTE the timer should be disabled or paused before entering this function.
	if (current != NULL && (current->status == WAITING || current->status == BLOCKED)) { 
		// Basically check if the current Thread is now blocked (meaning it is waiting on a mutex)
		// or if it is WAITING (meaning it's waiting on a exit thread), then it 
		// should not be placed in the ready queue aka be scheduled again.
		current = NULL;
	}
	// schedule policy
	#ifndef MLFQ
		// Choose RR
		//printf("[D]: RR\n");
		sched_rr();
	#else 
		// Choose MLFQ
		//printf("[D]: MLFQ\n");
		sched_mlfq();
	#endif
	startTimer();
	setcontext(&(current->context));
}

/* Round Robin (RR) scheduling algorithm */
static void sched_rr() {
	// Your own implementation of RR
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
	
	tcb* nextRun = dequeue(&scheduleInfo->priorityQueues[MAX_PRIORITY - 1]);
	if (nextRun != NULL) {
		//printf("Dequed %d from NULL\n", nextRun->id);
		//printf("[D]: The next thread to run is thread %d\n", nextRun->id);
		if (current != NULL) {
			//printf("Dequed %d from thread %d\n", nextRun->id, current->id);
			enqueue(&scheduleInfo->priorityQueues[MAX_PRIORITY - 1], current);
		}
		current = nextRun;
	}
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// Your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
	//printf("[D]: Entered Scheduler\n");
	scheduleInfo->timeSlices++;
	
	// Boosting the priority every X time slices, should probably change to a timer 
	if (scheduleInfo->timeSlices == BOOST_AFTER_TIME_SLICE) {
		//printf("[D]: Boosting priorities!\n");
		for (int priorityQueueLevel = MAX_PRIORITY - 2; priorityQueueLevel >= 0; priorityQueueLevel--) {
			tcb* readyTCB;
			while ((readyTCB = dequeue(&scheduleInfo->priorityQueues[priorityQueueLevel])) != NULL) {
				readyTCB->priority += 1;
				enqueue(&scheduleInfo->priorityQueues[readyTCB->priority - 1], readyTCB);
			}
		}
		scheduleInfo->timeSlices = 0;
	}
	
	//If the current thread used the whole time slice, decrease its priority.
	if (scheduleInfo->usedEntireTimeSlice == '1') {
		if (current != NULL && current->priority != 1) {
			current->priority -= 1;
		}
		scheduleInfo->usedEntireTimeSlice = '0';
	}
	 
	//Find the next thread to run by searching through the highest priority queue. 
	for (int priorityQueueLevel = MAX_PRIORITY - 1; priorityQueueLevel >= 0; priorityQueueLevel--) { 
		tcb* nextRun = dequeue(&scheduleInfo->priorityQueues[priorityQueueLevel]);
		if (nextRun != NULL) {
			if (current != NULL) {
				enqueue(&scheduleInfo->priorityQueues[current->priority - 1], current);
			}
			current = nextRun;
			break;
		}
	}
}

/*
** Initialization Functions
*/

/*
	Mallocs and initializes only the headers of the TCB struct but not the
	context. 
*/
static tcb* initializeTCBHeaders() {
	tcb* threadControlBlock = malloc(sizeof(tcb));
	if (threadControlBlock == NULL) {
		perror("[D]: Failed to allocate space for the TCB.\n");
		exit(-1);
	}
	// Initializes the attributes of the TCB.
	// Critical Section (Set and increment thread id)
	while (__sync_lock_test_and_set(&(threadIDMutex), 1)) {
		//printf("8\n");
		rpthread_yield();
	}
	threadControlBlock->id = threadID++; 
	__sync_lock_release(&(threadIDMutex)); 
	
	threadControlBlock->joinTID = 0;  
	threadControlBlock->priority = MAX_PRIORITY;
	threadControlBlock->status = READY;
	threadControlBlock->desiredMutex = 0;
	threadControlBlock->exitValue = NULL;
	return threadControlBlock;
}

/*
	Initializes a thread context
*/
static void initializeContext(ucontext_t* threadContext, ucontext_t* uc_link) {
	if (getcontext(threadContext) == -1) {
		perror("[D]: Failed to initialize context.\n");
		exit(-1);
	}
	// Check to make sure that the context is actually unintialized
	if (threadContext->uc_link == NULL) {
		// Set uc_link to NULL only if context being initialized is the scheduler
		threadContext->uc_link = uc_link; //(scheduler.uc_stack.ss_size == 0) ? NULL : &(scheduler);
		threadContext->uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
		if (threadContext->uc_stack.ss_sp == NULL) {
			perror("[D]: Failed to allocate space for the stack in context.\n");
			exit(-1);
		}
		threadContext->uc_stack.ss_size = THREAD_STACK_SIZE;
		threadContext->uc_stack.ss_flags = SS_DISABLE; //Can either be SS_DISABLE or SS_ONSTACK
	}
}

/**
	Mallocs and initializes the TCB struct, including the context.
*/
static tcb* initializeTCB() {
	tcb* threadControlBlock = initializeTCBHeaders();
	initializeContext(&(threadControlBlock->context), &exitContext);
	return threadControlBlock;
}

/**
	Mallocs and initializes all the queues required for the scheduling.
	terminatedAndJoinedQueue queue indicates which threads that were already
	joined on.
	Join queue indicates which threads are waiting for another thread to 
	terminate.
	Blocked queue indicates which threads are waiting for mutex.
	Exit queue indicates which threads have exited but not been joined yet.
*/
static void initializeScheduleQueues() {
	terminatedAndJoinedQueue = initializeQueue();
	joinQueue = initializeQueue();
	blockedQueue = initializeQueue();
	exitQueue = initializeQueue();
}

/**
	Mallocs and initializes the global scheduler struct.
*/
static void initializeScheduler() {
	scheduleInfo = malloc(sizeof(schedulerNode));
	if (scheduleInfo == NULL) {
		perror("[D]: Failed to allocate space for the schedule Info\n");
		exit(-1);
	}
	scheduleInfo->numberOfQueues = MAX_PRIORITY;
	scheduleInfo->priorityQueues = calloc(MAX_PRIORITY, sizeof(Queue)); // Hoping this zeros out all the queues, if not have to traverse each and memset to '0'
	if (scheduleInfo->priorityQueues == NULL) {
		perror("[D]: Failed to allocate space for the priority queues\n");
		exit(-1);
	}
	scheduleInfo->usedEntireTimeSlice = '0';
	scheduleInfo->timeSlices = 0; 

	// Create scheduler context
	initializeContext(&scheduler, NULL);
	makecontext(&scheduler, (void*) schedule, 0);
}

/**
	Registers the SIGPROF signal to be handled with the timer_interrupt_handler
	function. 
*/
static void initializeSignalHandler() {
	signalHandler.sa_handler = &timer_interrupt_handler;
	if (sigaction(SIGPROF, &signalHandler, NULL) == -1) {
		perror("[D]: Could not initialize the timer interrupt handler!\n");
		exit(-1);
	}
}

/*
	Starts the timer with no interval, just sets it to the time slice because
	once the timer runs out and sends the signal (SIGPROF), the timer interrupt
	handler automatically swaps to the scheduler context and the schedule 
	context automatically restarts the timer. 
*/
static void initializeTimer() {
	//The initial and reset values of the timer. 
	//timer.it_interval.tv_sec = (TIMESLICE * 1000) / 1000000;
	//timer.it_interval.tv_usec = (TIMESLICE * 1000) % 1000000;
	
	//How long the timer should run before outputting a SIGPROF signal. 
	timer.it_value.tv_sec = (TIMESLICE * 1000) / 1000000;
	timer.it_value.tv_usec = (TIMESLICE * 1000) % 1000000;

	//printf("[D]: The timer has been initialized.\n Time interval is %ld seconds, %ld microseconds.\n The Time remaining is %ld seconds, %ld microseconds.\n", timer.it_interval.tv_sec, timer.it_interval.tv_usec, 		timer.it_value.tv_sec, timer.it_value.tv_usec);
	
	setitimer(ITIMER_PROF, &timer, NULL);
}

/**
	Allocates memory for the queue struct and sets the default values, 
	returns the pointer to the malloced queue. 
*/
static Queue* initializeQueue() {
	Queue* queue = malloc(sizeof(Queue)); 
	if (queue == NULL) {
		perror("[D]: Failed to allocate space for a queue.\n");
		exit(-1);
	}
	queue->size = 0;
	queue->head = NULL;
	queue->tail = NULL;
	queue->mutex = 0;
	return queue;
}

/*
** Timer Functions
*/

static void timer_interrupt_handler(int signum) {
	//disableTimer();
	//char debug[] = "Timer Interrupt Happened\n";
	//write(1, &debug, sizeof(debug));
	//if (current != NULL) printf("Current id (TIMER) %d\n", current->id);

	if (signum == SIGPROF) {
		scheduleInfo->usedEntireTimeSlice = '1';
		swapcontext(&(current->context), &(scheduler));
	} else {
		char error[] = "[E]: ERROR SIGNAL OCCURRED IN TIMER INTERRUPT THAT WAS NOT SIGPROF\n";
		write(1, &error, sizeof(error)); // Using write because it's thread safe
	}
}

static void disableTimer() {
	//To disable, set it_value to 0, regardless of it_interval. (According to the man pages)
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 0;
	setitimer(ITIMER_PROF, &timer, NULL);
	//printf("[D]: The timer has been disabled!\n");
} 

static void startTimer() { //Should it just call initialize timer instead?
	
	timer.it_value.tv_sec = (TIMESLICE * 1000) / 1000000;
	timer.it_value.tv_usec = (TIMESLICE * 1000) % 1000000;

	//printf("[D]: The timer is starting again. Time interval is %ld seconds, %ld microseconds.\n", timer.it_value.tv_sec, timer.it_value.tv_usec);
	setitimer(ITIMER_PROF, &timer, NULL);
} 

static void pauseTimer() {
	setitimer(ITIMER_PROF, &zero, &timer);
	
	//printf("[D]: Time Paused, time left: %ld seconds, %ld microseconds\n", timer.it_value.tv_sec, timer.it_value.tv_usec);
	//printf("[D]: The timer has been paused!\n");
}

static void resumeTimer() { 
	//printf("[D]: Time resuming, time left: %ld seconds, %ld microseconds\n", timer.it_value.tv_sec, timer.it_value.tv_usec);

	//How long the timer should run before outputting a SIGPROF signal. 
	// (The timer will count down from this value and once it hits 0, output a signal and reset to the IT_INTERVAL value)
	
	timer.it_value.tv_sec = (((TIMESLICE * 1000) / 1000000) == 0) ? 0 : ((TIMESLICE * 1000) / 1000000) - (timer.it_value.tv_sec % ((TIMESLICE * 1000) / 1000000));
	timer.it_value.tv_usec = (((TIMESLICE * 1000) % 1000000) == 0) ? 0 : ((TIMESLICE * 1000) % 1000000) - (timer.it_value.tv_usec % ((TIMESLICE * 1000) % 1000000));

	setitimer(ITIMER_PROF, &timer, NULL);
	//printf("[D]: The timer is resuming!\n");
}

/*
** Queue Functions
*/

// Must initialize the queue via initializeQueue() before calling this method
static void enqueue(Queue* queue, tcb* threadControlBlock) {
	//if (current != NULL) printf("THREAD %d call enqueue\n", current->id);
	if (queue == NULL || threadControlBlock == NULL) {
		return;
	}
	QueueNode* newNode = malloc(sizeof(QueueNode));
	if (newNode == NULL) {
		perror("[D]: Failed to allocate space for a queueNode.\n");
		exit(-1);
	}
	newNode->node = threadControlBlock;
	newNode->next = NULL;

	// Critical section (Adding the node and changing the size)
	while (__sync_lock_test_and_set(&(queue->mutex), 1)) rpthread_yield();

	if (queue->size++ == 0) {
		queue->head = queue->tail = newNode;
	} else {
		queue->tail->next = newNode;
		queue->tail = newNode;
	}
	__sync_lock_release(&(queue->mutex));
}

static tcb* dequeue(Queue* queue) {
	//if (current != NULL) printf("THREAD %d call dequeue\n", current->id);
	// Check to make sure queue is initialized
	if (queue == NULL) return NULL;

	// Critical section (Removing the node and changing size)
	while (__sync_lock_test_and_set(&(queue->mutex), 1)) rpthread_yield();

	if (queue->head == NULL) {
		__sync_lock_release(&(queue->mutex));
		return NULL;
	}
	tcb* popped = queue->head->node;
	QueueNode* temp = queue->head;
	queue->head = queue->head->next;
	if (--(queue->size) == 0) queue->tail = NULL;
	__sync_lock_release(&(queue->mutex));

	// Free and return
	free(temp);
	return popped;
}

static tcb* findFirstOfBlockedQueue(Queue* queue, int mutexID) {
	if (queue == NULL || queue->head == NULL) {
		return NULL;
	}
	QueueNode* prev = NULL;
	while (__sync_lock_test_and_set(&(queue->mutex), 1)) rpthread_yield();
	for (QueueNode* currentNode = queue->head; currentNode != NULL; currentNode = currentNode->next) {
		if (currentNode->node->desiredMutex == mutexID) {
			tcb* threadControlBlock = currentNode->node;
			QueueNode* temp = currentNode;
			
			// Check if first node
			if (prev == NULL) queue->head = currentNode;
			else prev->next = currentNode->next;
			// Check if last node
			if (currentNode == queue->tail) queue->tail = prev;

			queue->size--;
			__sync_lock_release(&(queue->mutex));
			free(temp);
			return threadControlBlock;
		}
		prev = currentNode;
	}
	__sync_lock_release(&(queue->mutex));
	return NULL;
}

static int checkExistBlockedQueue(Queue* queue, int mutexID) {
	if (queue == NULL || queue->head == NULL) {
		return -1;
	}
	while (__sync_lock_test_and_set(&(queue->mutex), 1)) rpthread_yield();
	QueueNode* currentNode = queue->head;
	while (currentNode != NULL) {
		if (currentNode->node->desiredMutex == mutexID) {
			__sync_lock_release(&(queue->mutex));
			return 1;
		}
		currentNode = currentNode->next;
	}
	__sync_lock_release(&(queue->mutex));
	return -1;
}

static void printQueue(Queue* queue) {
	QueueNode* currentNode = queue->head;
	while (currentNode != NULL) {
		printf("Thread ID %d\n", currentNode->node->id);
		currentNode = currentNode->next;
	}
}

/*
** Hashtable functions
*/

static void hash_init() {
	for (int i = 0; i < HASHTABLE_LEN; i++) {
		hashTable[i] = (linkedList *) malloc(sizeof(linkedList));
		hashTable[i]->head = NULL;
		hashTable[i]->mutex = 0;
	}
}

static int hash(int key) {
	return (key - 1) % HASHTABLE_LEN;
}

static tcb *hash_search(int threadID) {
	int hashKey = hash(threadID);
	linkedList *hashList = hashTable[hashKey];

	while (__sync_lock_test_and_set(&(hashList->mutex), 1)) rpthread_yield();
	for (QueueNode *curr = hashList->head; curr != NULL; curr = curr->next) {
		if (curr->node->id == threadID) {
			__sync_lock_release(&(hashList->mutex));
			return curr->node;
		}
	}
	__sync_lock_release(&(hashList->mutex));
	return NULL;
}

static void hash_insert(tcb *toAdd) {
	int hashKey = hash(toAdd->id);
	QueueNode *toInsert = (QueueNode *) malloc(sizeof(QueueNode));
	toInsert->node = toAdd;
	toInsert->next = NULL;
	linkedList *hashList = hashTable[hashKey];
	// Critical section (Add node to hashtable)
	while (__sync_lock_test_and_set(&(hashList->mutex), 1)) rpthread_yield();
	toInsert->next = hashList->head;
	hashList->head = toInsert;
	__sync_lock_release(&(hashList->mutex));
}

static QueueNode *hash_remove(int threadID) {
	int hashKey = hash(threadID);
	linkedList *hashList = hashTable[hashKey];

	while (__sync_lock_test_and_set(&(hashList->mutex), 1)) rpthread_yield();
	QueueNode *prev = NULL;
	for (QueueNode *curr = hashList->head; curr != NULL; curr = curr->next) {
		if (curr->node->id == threadID) {
			// Remove node
			if (prev == NULL) hashList->head = curr->next;
			else prev->next = curr->next;
			__sync_lock_release(&(hashList->mutex));
			return curr;
		}
	}
	__sync_lock_release(&(hashList->mutex));
	return NULL;
}