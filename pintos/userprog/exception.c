#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* 처리한 page fault 개수예요. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 유저 프로그램이 일으킬 수 있는 인터럽트들의 핸들러를 등록해요.

   진짜 유닉스 계열 OS에서는 이런 인터럽트 대부분을 시그널 형태로
   유저 프로세스에게 넘겨줘요([SV-386] 3-24, 3-25에 설명돼 있어요).
   하지만 여기서는 시그널을 구현하지 않아요. 대신 그냥 유저
   프로세스를 죽이게 해요.

   page fault는 예외예요. 여기서는 다른 예외들과 똑같이 다루지만,
   가상 메모리를 구현하려면 이 부분을 바꿔야 해요.

   각 예외의 설명은 [IA32-v3a] 5.15절 "Exception and Interrupt
   Reference"를 참고하세요. */
void
exception_init (void) {
	/* 이 예외들은 유저 프로그램이 INT, INT3, INTO, BOUND 같은 명령으로
	   직접 일으킬 수 있어요. 그래서 DPL==3으로 둬서, 유저 프로그램이
	   이 명령들로 부르는 걸 허용해요. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 이 예외들은 DPL==0이라 유저 프로세스가 INT 명령으로 부를 수 없어요.
	   그래도 간접적으로는 일어날 수 있어요. 예를 들어 #DE는 0으로
	   나누면 생겨요. */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트를 켠 채로 처리해도 돼요.
	   page fault만은 인터럽트를 꺼야 해요. fault가 난 주소가 CR2에
	   저장되는데, 그 값을 지켜야 하거든요. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계를 출력해요. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* (아마도) 유저 프로세스가 일으킨 예외의 핸들러예요. */
static void
kill (struct intr_frame *f) {
	/* 이 인터럽트는 (아마도) 유저 프로세스가 일으킨 거예요.
	   예를 들어 매핑되지 않은 가상 메모리에 접근하려 했을 수 있어요
	   (page fault). 지금은 그냥 유저 프로세스를 죽여요. 나중에는
	   커널에서 page fault를 처리하게 될 거예요. 진짜 유닉스 계열 OS는
	   대부분의 예외를 시그널로 프로세스에게 돌려주지만, 여기서는
	   시그널을 구현하지 않아요. */

	/* 인터럽트 프레임의 코드 세그먼트 값을 보면 예외가 어디서
	   일어났는지 알 수 있어요. */
	switch (f->cs) {
		case SEL_UCSEG:
			/* 유저 코드 세그먼트예요. 예상대로 유저 예외라서
			   유저 프로세스를 죽여요. */
			printf ("%s: dying due to interrupt %#04llx (%s).\n",
					thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
			/* 커널 코드 세그먼트예요. 커널 버그라는 뜻이에요.
			   커널 코드는 예외를 던지면 안 돼요. (page fault는 커널에서도
			   일어날 수 있지만, 여기까지 오면 안 돼요.)
			   그 점을 분명히 하려고 커널을 panic시켜요. */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* 다른 코드 세그먼트라고요? 일어나면 안 되는 일이에요.
			   커널을 panic시켜요. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* page fault 핸들러예요. 가상 메모리를 구현하려면 채워 넣어야 하는
   뼈대예요. project 2 풀이에 따라서는 이 코드를 고쳐야 할 수도 있어요.

   들어올 때 fault가 난 주소는 CR2(Control Register 2)에 있고,
   fault 정보는 F의 error_code 멤버에 들어 있어요. 그 형식은
   exception.h의 PF_* 매크로에 설명돼 있어요. 아래 예시 코드가
   그 정보를 해석하는 방법을 보여줘요. 둘 다 더 자세한 내용은
   [IA32-v3a] 5.15절 "Exception and Interrupt Reference"의
   "Interrupt 14--Page Fault Exception (#PF)" 설명에 있어요. */
static void
page_fault (struct intr_frame *f) {
	bool not_present;  /* true: 없는 페이지, false: 읽기 전용 페이지에 쓰기. */
	bool write;        /* true: 쓰기 접근, false: 읽기 접근. */
	bool user;         /* true: 유저가 접근, false: 커널이 접근. */
	void *fault_addr;  /* fault가 난 주소. */

	/* fault를 일으킨 주소, 즉 접근하려던 가상 주소를 가져와요.
	   코드를 가리킬 수도, 데이터를 가리킬 수도 있어요.
	   fault를 일으킨 명령의 주소와 같다는 보장은 없어요
	   (그건 f->rip예요). */

	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 켜요. (CR2 값이 바뀌기 전에 확실히 읽으려고
	   잠깐 꺼 뒀던 거예요.) */
	intr_enable ();


	/* 원인을 알아내요. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* project 3부터 써요. */
	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif

	/* page fault 개수를 세요. */
	page_fault_cnt++;

	/* 진짜 fault면 정보를 보여주고 끝내요. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
			fault_addr,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
	kill (f);
}

