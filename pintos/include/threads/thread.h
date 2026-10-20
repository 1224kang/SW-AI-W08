#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

bool priority_sort(const struct list_elem *a,const struct list_elem *b, void *aux UNUSED);
void priority_thread_yield(void);

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;
	struct list_elem sleep_elem;		/* List element(연결 리스트 형태). */

	int my_priority;					// 내 우선순위 보존용
	struct list donation_list;			// 기부받은 스레드 목록
	struct lock *lock_to_stop;			// 어떤 lock때문에 멈춰 있는지
	struct list_elem donation_elem;		// 다른 스레드의 기부해준 스레드 목록에 들어가기 위함 

	int nice;							// nice 정의 (다른 스레드에게 얼마나 양보적인지)(범위는 -20 ~ 20)
	int recent_cpu;						// recent_cpu 정의 (각 프로세스가 최근에 cpu 시간을 얼마나 사용했는지)
	struct list_elem allelem;			// all_list에 넣기 위함

	/* process_wait용 (#113) */
	struct list child_record_list;		// 내가 만든 자식들의 기록(struct record) 목록. 안에 든 건 thread가 아니라 기록이에요
	struct thread *parents;				// 나를 만든 부모. 부모가 먼저 끝나면 NULL(고아 표시)이 돼요
	bool exit_called;					// SYS_EXIT로 끝났으면 true. false면 예외로 죽은 거라 종료 코드를 -1로 봐요
	int exit_status;					// exit()에 넘긴 종료 코드

	struct fd_table *fdt;
	struct file *running_file;			// 지금 실행 중인 프로그램 파일 (rox, #115). 쓰기 금지를 건 바로 그 struct file이라야 풀 수 있어서 들고 있어요
	

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif
	int64_t timer_tick;

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

struct fd_table {
	struct list fd_list; //해당 프로세스가 열어둔 파일들의 목록
	int next_fd_index; //다음에 할당받을 fd index
};

struct file_fd_mapping{
	int index; //파일에 할당된 fd 번호
	struct file *mapping_file; //매핑된 실제 파일 포인터 
	struct list_elem file_fd_elem; //fd_list에 연결하기 위한 고리
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
// 커널 옵션으로 -mlfqs를 전달하면 thread_mlfqs가 true로 설정됨. 기본값은 false
// thread_mlfqs가 false이면, 우선순위 스케줄러 + 기부(Donation) 방식으로 작동함
// thread_mlfqs가 true이면 MLFQS(다단계 피드백 큐 스케줄러)방식으로 작동함
extern bool thread_mlfqs;

extern int load_avg;					// load_avg 정의

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void mlfqs_calculate_priority (struct thread *t);
void mlfqs_calculate_recent_cpu (struct thread *t);
void mlfqs_calculate_load_avg (void);
void mlfqs_increment_recent_cpu (void);
void mlfqs_recalculate_all_priority (void);
void mlfqs_recalculate_all_recent_cpu (void);

void do_iret (struct intr_frame *tf);
/* 자식 기록 리스트와 parent 포인터를 지키는 전역 락이에요. 본체는 thread.c에 하나만 있어요.
   리스트에 넣고 빼는 곳, 자식이 부모 리스트를 도는 곳은 모두 이 락 안에서 해요. */
extern struct lock process_lock;

#endif /* threads/thread.h */
