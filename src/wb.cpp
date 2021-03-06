#include <stdio.h>
#include <memory.h>
#include <Windows.h>
#include <list>
#include "wb.h"

tv blockDim;
tv blockIdx; 
tv threadIdx;

// A thread queue entry
struct qentry { 
	tv    threadIdx;
	int   id;						// Thread id (for debugging)
	void* fiber;					// System fiber id
	enum  { started, done } state;	// Thread state, needed for cleanup
};

std::list<qentry> queue;			// Fiber queue, first is the active one
void* fiber_main;					// The dispatch fiber

//** Start and cleanup after a user-passed function (in a closure pointed by LPVOID)
void CALLBACK q_start(LPVOID param) 
{
	// printf("entered\n");

	((tclosure*)param)->call();

	// Assuming first entry in queue
	queue.front().state = qentry::done;
	SwitchToFiber(fiber_main);
}

//** Run things in fiber queue until all are done
void run_queue()
{
	while (!queue.empty()) {
		// Compute blockidx, threadidx
		threadIdx = queue.front().threadIdx;

		// Execute the fiber until either returns or yields
		SwitchToFiber(queue.front().fiber);

		if (queue.front().state == qentry::done) {
			// Done, remove from queue
			DeleteFiber(queue.front().fiber);
			queue.pop_front();
		} else {
			// Reschedule
			queue.push_back(queue.front());
			queue.pop_front();
		}
	}
}

//** Schedule threads on a given block/grid size, doing
//** one block at a time.
void run_scheduler(tv& szblk, tv& szgrid, tclosure& closure)
{
	fiber_main = ConvertThreadToFiber(0);

	blockIdx.x  = blockIdx.y  = blockIdx.z  = blockIdx.w  = 0;
	threadIdx.x = threadIdx.y = threadIdx.z = threadIdx.w = 0;

	blockDim = szblk;

	for (int k = 0; k < szgrid.z; k++) {
		for (int j = 0; j < szgrid.y; j++) {
			for (int i = 0; i < szgrid.x; i++) {
				blockIdx.x = i;
				blockIdx.y = j;
				blockIdx.z = k;

				for (int w = 0; w < szblk.z; w++) {
					for (int v = 0; v < szblk.y; v++) {
						for (int u = 0; u < szblk.x; u++) {
							int id = u + v*szblk.y + w*szblk.x*szblk.y;
							qentry q = { tv(u,v,w,0), id, CreateFiber(1024, q_start, (LPVOID)&closure), qentry::started };
							//queue.push_back(q);
							queue.push_front(q); // in reverse order to not rely on order of scheduling (since this is not really parallel)
						}
					}
				}
				run_queue();
			}
		}
	}
}

//** Sync between threads by yielding to others. Since threads are scheduled
//** in a consistent order, this is enough to actually sync the threads.
void __syncthreads() {
	SwitchToFiber(fiber_main);
}
