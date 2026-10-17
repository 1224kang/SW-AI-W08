#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_, char **arg, int argc);
static void initd (void *f_name);
static void __do_fork (void *);
static int parse_words(char *arg[], char *word,const char *parse_word);
static int user_stack_push (char *arg, struct intr_frame *if_);
static void user_stack_args_push (char *arg[], int argc, struct intr_frame *if_);



/* initd와 다른 프로세스들이 함께 쓰는 프로세스 초기화 함수예요. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* FILE_NAME에서 불러온 첫 번째 유저 프로그램 "initd"를 시작해요.
 * 새 스레드는 process_create_initd()가 돌아오기 전에
 * 스케줄될 수도 있고, 심지어 끝나 버릴 수도 있어요. initd의
 * 스레드 id를 돌려주고, 스레드를 못 만들면 TID_ERROR를 돌려줘요.
 * 이 함수는 꼭 한 번만 불러야 해요. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* FILE_NAME을 복사해 둬요.
	 * 안 그러면 호출한 쪽과 load() 사이에 경쟁 상태(race)가 생겨요. */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* FILE_NAME을 실행할 새 스레드를 만들어요. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* 첫 번째 유저 프로세스를 띄우는 스레드 함수예요. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* 현재 프로세스를 `name`이라는 이름으로 복제해요. 새 프로세스의 스레드 id를 돌려주고,
 * 스레드를 못 만들면 TID_ERROR를 돌려줘요. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* 현재 스레드를 새 스레드로 복제해요. */
	return thread_create (name,
			PRI_DEFAULT, __do_fork, thread_current ());
}

#ifndef VM
/* 이 함수를 pml4_for_each에 넘겨서 부모의 주소 공간을 복제해요.
 * 프로젝트 2에서만 쓰여요. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: parent_page가 커널 페이지면 바로 return 해요. */

	/* 2. 부모의 pml4(page map level 4)에서 VA를 찾아요. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: 자식용으로 새 PAL_USER 페이지를 할당하고, 그 결과를
	 *    TODO: NEWPAGE에 넣어요. */

	/* 4. TODO: 부모의 페이지를 새 페이지로 복제하고,
	 *    TODO: 부모의 페이지가 쓰기 가능한지 확인해요 (그 결과에 맞게
	 *    TODO: WRITABLE을 정해요). */

	/* 5. 새 페이지를 자식의 페이지 테이블에 VA 주소로, WRITABLE
	 *    권한을 붙여서 추가해요. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: 페이지를 넣는 데 실패하면 에러 처리를 해요. */
	}
	return true;
}
#endif

/* 부모의 실행 문맥(context)을 복사하는 스레드 함수예요.
 * 힌트) parent->tf에는 그 프로세스의 유저 영역 문맥이 들어 있지 않아요.
 *       그래서 process_fork의 두 번째 인자를 이 함수까지
 *       넘겨 줘야 해요. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: 어떻게든 parent_if를 넘겨 받아요. (즉 process_fork()의 if_) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. CPU 문맥을 지역 스택으로 읽어 와요. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. 페이지 테이블(PT)을 복제해요 */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: 여기에 코드를 써요.
	 * TODO: 힌트) 파일 객체를 복제할 때는 include/filesys/file.h의
	 * TODO:       `file_duplicate`를 써요. 그리고 이 함수가 부모의 자원을
	 * TODO:       다 복제하는 데 성공하기 전까지는 부모가 fork()에서
	 * TODO:       돌아오면 안 돼요. */

	process_init ();

	/* 마지막으로, 새로 만든 프로세스로 전환해요. */
	if (succ)
		do_iret (&if_);
error:
	thread_exit ();
}

/* 현재 실행 문맥을 f_name으로 바꿔요.
 * 실패하면 -1을 돌려줘요. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* 여기서는 thread 구조체 안의 intr_frame을 쓸 수 없어요.
	 * 현재 스레드가 다시 스케줄될 때
	 * 실행 정보를 그 멤버에 저장하기 때문이에요. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* 먼저 지금 문맥을 없애요 */
	process_cleanup ();

	/* 명령줄을 공백 기준으로 쪼개요. arg[0]은 프로그램 이름이고, arg[1]부터 인수예요.
	 * 명령줄은 128바이트까지라서 단어는 많아야 64개예요. 그래서 칸을 64개 잡았어요. */
	char *arg[64] = {0};
	int argc;
	argc =  parse_words(arg,file_name," ");

	/* 스레드 이름을 프로그램 이름(arg[0])으로 바꿔요.
	 * 처음 이름은 명령줄 앞 15글자라서, 그대로 두면 인수까지 섞여 있어요. */
	 strlcpy (thread_current()->name, arg[0], sizeof thread_current()->name);

	/* 그다음 바이너리를 불러와요 */
	success = load (file_name, &_if, arg, argc);

	/* 불러오기에 실패하면 끝내요. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* 바뀐 프로세스를 시작해요. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* 스레드 TID가 끝날 때까지 기다렸다가 그 종료 상태(exit status)를 돌려줘요.
 * 커널이 끝낸 경우(즉 예외 때문에
 * 죽은 경우)에는 -1을 돌려줘요. TID가 잘못됐거나,
 * 호출한 프로세스의 자식이 아니거나,
 * 그 TID로 process_wait()이 이미 한 번 성공적으로 불렸다면
 * 기다리지 않고 바로 -1을 돌려줘요.
 *
 * 이 함수는 문제 2-2에서 구현해요. 지금은
 * 아무 일도 안 해요. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: 힌트) process_wait (initd)를 하면 pintos가 종료돼요. process_wait를
	 * XXX:       구현하기 전에는 여기에 무한 루프를
	 * XXX:       넣어 두기를 추천해요. */
	while (1) /* TODO: 임시로 계속 기다려요. 진짜 wait를 만들면 바꿔요. */
	{

	}

	return -1;
}

/* 프로세스를 끝내요. 이 함수는 thread_exit ()가 불러요. */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: 여기에 코드를 써요.
	 * TODO: 프로세스 종료 메시지를 구현해요
	 * TODO: (project2/process_termination.html 참고).
	 * TODO: 프로세스 자원 정리도 여기서 구현하기를 추천해요. */

	process_cleanup ();
}

/* 현재 프로세스의 자원을 해제해요. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 디렉터리를 없애고,
	 * 커널 전용 페이지 디렉터리로 다시 바꿔요. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서는 순서가 정말 중요해요. 페이지 디렉터리를 바꾸기 전에
		 * cur->pagedir를 NULL로 만들어야 해요. 그래야 타이머 인터럽트가
		 * 프로세스 페이지 디렉터리로 되돌려 놓지 못해요.
		 * 그리고 프로세스의 페이지 디렉터리를 없애기 전에
		 * 기본 페이지 디렉터리를 활성화해야 해요. 안 그러면
		 * 지금 활성화된 페이지 디렉터리가 이미 해제된(그리고 지워진)
		 * 디렉터리가 돼 버려요. */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* 다음(next) 스레드에서 유저 코드를 돌릴 수 있게 CPU를 준비해요.
 * 이 함수는 문맥 전환 때마다 불려요. */
void
process_activate (struct thread *next) {
	/* 스레드의 페이지 테이블을 활성화해요. */
	pml4_activate (next->pml4);

	/* 인터럽트를 처리할 때 쓸 스레드의 커널 스택을 설정해요. */
	tss_update (next);
}

/* 여기서는 ELF 바이너리를 불러와요. 아래 정의들은 ELF 명세 [ELF1]에서
 * 거의 그대로 가져온 거예요.  */

/* ELF 타입들이에요. [ELF1] 1-2 참고. */
#define EI_NIDENT 16

#define PT_NULL    0            /* 무시해요. */
#define PT_LOAD    1            /* 불러올 수 있는 세그먼트. */
#define PT_DYNAMIC 2            /* 동적 링크 정보. */
#define PT_INTERP  3            /* 동적 로더 이름. */
#define PT_NOTE    4            /* 보조 정보. */
#define PT_SHLIB   5            /* 예약됨. */
#define PT_PHDR    6            /* 프로그램 헤더 테이블. */
#define PT_STACK   0x6474e551   /* 스택 세그먼트. */

#define PF_X 1          /* 실행 가능. */
#define PF_W 2          /* 쓰기 가능. */
#define PF_R 4          /* 읽기 가능. */

/* 실행 파일 헤더예요. [ELF1] 1-4 ~ 1-8 참고.
 * ELF 바이너리의 맨 앞에 나와요. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* 줄임말 */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* FILE_NAME의 ELF 실행 파일을 현재 스레드로 불러와요.
 * 실행 파일의 진입점(entry point)은 *RIP에,
 * 초기 스택 포인터는 *RSP에 저장해요.
 * ARG와 ARGC는 process_exec에서 쪼갠 명령줄 단어들과 그 개수예요.
 * 유저 스택에 인자를 쌓을 때 써요.
 * 성공하면 true, 아니면 false를 돌려줘요. */
static bool
load (const char *file_name, struct intr_frame *if_, char **arg, int argc) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* 페이지 디렉터리를 할당하고 활성화해요. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* 실행 파일을 열어요. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* 실행 파일 헤더를 읽고 검사해요. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* 프로그램 헤더들을 읽어요. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* 이 세그먼트는 무시해요. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* 일반 세그먼트예요.
						 * 앞부분은 디스크에서 읽고, 나머지는 0으로 채워요. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* 전부 0이에요.
						 * 디스크에서는 아무것도 안 읽어요. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* 스택을 준비해요. */
	if (!setup_stack (if_))
		goto done;

	/* 시작 주소예요. */
	if_->rip = ehdr.e_entry;

	/* 명령줄 인자를 유저 스택에 쌓아요.
	 * 다 쌓고 나면 if_->rsp는 가짜 돌아올 주소 칸을, if_->R.rsi는 argv[0] 칸을 가리켜요.
	 * main(argc, argv)의 첫 번째 인자는 RDI로 들어가니까 argc를 넣어 둬요.
	 * 지금 CPU 레지스터는 커널이 쓰고 있어서 intr_frame에 적어 두기만 해요.
	 * 진짜 레지스터로는 do_iret이 유저 모드로 넘어갈 때 한꺼번에 옮겨 줘요. */
	user_stack_args_push (arg, argc, if_);
	if_->R.rdi = argc;

	success = true;

done:
	/* 불러오기가 성공하든 실패하든 여기로 와요. */
	file_close (file);
	return success;
}


/* PHDR이 FILE 안의 올바르고 불러올 수 있는 세그먼트를 가리키는지 확인해요.
 * 그렇다면 true, 아니면 false를 돌려줘요. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset과 p_vaddr은 페이지 안 오프셋이 같아야 해요. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 FILE 안을 가리켜야 해요. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz는 적어도 p_filesz만큼은 커야 해요. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트가 비어 있으면 안 돼요. */
	if (phdr->p_memsz == 0)
		return false;

	/* 가상 메모리 영역은 시작과 끝이 모두
	   유저 주소 공간 범위 안에 있어야 해요. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 영역이 커널 가상 주소 공간을 가로질러
	   "한 바퀴 돌아" 넘어가면 안 돼요. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 0번 페이지는 매핑하지 못하게 해요.
	   0번 페이지를 매핑하는 것 자체도 좋지 않지만, 이걸 허용하면
	   시스템 콜에 널 포인터를 넘긴 유저 코드가 memcpy() 같은 곳의
	   널 포인터 assertion을 거쳐 커널을 panic 시킬 가능성이
	   꽤 커져요. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* 괜찮아요. */
	return true;
}
/* WORD를 PARSE_WORD에 든 구분자 기준으로 쪼개서 ARG에 차례로 담고, 단어 개수를 돌려줘요.
 * strtok_r가 WORD 안의 구분자를 '\0'으로 바꿔요. ARG의 칸들은 새 문자열이 아니라 WORD 안을 가리켜요. */
static int parse_words(char *arg[], char *word,const char *parse_word)
{
	char *token, *save_ptr;
	int n = 0;
	for (token = strtok_r (word, parse_word, &save_ptr); token != NULL;
token = strtok_r (NULL, parse_word, &save_ptr))
	{
		arg[n] = token;
		n++;
	}

	return n;
}

/* ARG 문자열을 '\0'까지 통째로 유저 스택에 쌓고, 쌓은 바이트 수를 돌려줘요.
 * 스택은 아래로 자라니까 if_->rsp를 먼저 내리고, 내려간 자리부터 복사해요.
 * strlen은 '\0'을 세지 않아서 +1 했어요. */
static int user_stack_push (char *arg, struct intr_frame *if_)
{
	size_t n = strlen(arg) + 1;
	if_->rsp -= n;
	void *user_rsp = (void *)if_->rsp;

	memcpy(user_rsp, arg, n);
	return (int)n;
}

/* 명령줄 인자들을 매뉴얼 Argument Passing 표와 같은 모양으로 유저 스택에 쌓아요.
 * 높은 주소부터 낮은 주소 순서로 이렇게 쌓여요.
 * 아래 주소는 'args-single onearg'를 돌려서 GDB로 확인한 값이에요.
 *
 *   0x4747fff4  "args-single\0"   문자열들
 *   0x4747ffed  "onearg\0"
 *   0x4747ffe8  00 00 00 00 00    8의 배수로 맞추는 빈칸
 *   0x4747ffe0  0                 argv[argc], 끝 표시
 *   0x4747ffd8  0x4747ffed        argv[1]
 *   0x4747ffd0  0x4747fff4        argv[0]            <- if_->R.rsi
 *   0x4747ffc8  0                 가짜 돌아올 주소   <- if_->rsp
 */
static void user_stack_args_push (char *arg[], int argc, struct intr_frame *if_)
{
	/* 문자열마다 유저 스택에 놓인 주소를 기억해 둬요.
	 * 마지막 칸 save_ptr[argc]는 argv[argc] = 0 자리예요.
	 * n은 지금까지 쌓은 문자열 바이트의 합이에요. */
	char *save_ptr[argc + 1];
	int n = 0;

	/* 문자열을 하나씩 쌓아요. 쌓은 직후의 rsp가 그 문자열의 시작 주소예요. */
	for (int i = 0; i < argc; i++)
	{
		n += user_stack_push (arg[i], if_);
		save_ptr[i] = (char*) if_->rsp;
	}
	save_ptr[argc] = 0;

	/* 다음에 쌓을 주소 칸들이 8바이트 경계에 오도록 0으로 빈칸을 채워요.
	 * 출발점 USER_STACK이 8의 배수라서, 쌓은 바이트 합 n만 보면 빈칸 크기를 알 수 있어요.
	 * 원본 없이 같은 값을 여러 칸 채우는 거라 memcpy가 아니라 memset을 써요. */
	if (n % 8 != 0)
	{
		if_->rsp -= (8 - (n % 8));
		memset((void*) if_->rsp, 0, (8 - (n % 8)));
	}

	/* argv[argc]부터 argv[0]까지 거꾸로 주소를 쌓아요. 그래야 argv[0]이 가장 낮은 주소에 와요.
	 * memcpy에는 save_ptr[i]가 가리키는 문자열이 아니라 save_ptr[i] 칸 자체(&save_ptr[i])를 넘겨요.
	 * 옮기고 싶은 건 문자열이 아니라 주소 8바이트예요. */
	for (int i = argc; i != -1; i--)
	{
		if_->rsp -= 8;
		memcpy((char *) if_->rsp, (char *) &save_ptr[i], sizeof(char *));
	}

	/* 지금 rsp가 argv[0] 칸이에요. main의 두 번째 인자 argv는 RSI로 들어가요. */
	if_->R.rsi = if_->rsp;

	/* 가짜 돌아올 주소예요. _start는 call로 불린 게 아니라서 돌아갈 곳이 없어요.
	 * 그래도 보통 함수 호출과 스택 모양을 맞추려고 0을 8바이트 넣어 둬요. */
	if_->rsp -= 8;
	memset((void*) if_->rsp, 0, 8);
}

#ifndef VM
/* 이 블록의 코드는 프로젝트 2 동안에만 쓰여요.
 * 프로젝트 2 전체에서 쓸 함수를 구현하고 싶다면
 * #ifndef 매크로 바깥에 구현해요. */

/* load() 도우미 함수들. */
static bool install_page (void *upage, void *kpage, bool writable);

/* FILE의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에
 * 불러와요. 모두 합쳐 READ_BYTES + ZERO_BYTES 바이트의 가상
 * 메모리를 아래처럼 초기화해요.
 *
 * - UPAGE부터 READ_BYTES 바이트는 FILE의 오프셋 OFS부터
 * 읽어 와야 해요.
 *
 * - UPAGE + READ_BYTES부터 ZERO_BYTES 바이트는 0으로 채워야 해요.
 *
 * 이 함수가 초기화한 페이지는 WRITABLE이 true면 유저 프로세스가
 * 쓸 수 있어야 하고, 아니면 읽기 전용이어야 해요.
 *
 * 성공하면 true를, 메모리 할당 에러나 디스크 읽기 에러가
 * 나면 false를 돌려줘요. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산해요.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고,
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채워요. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 메모리 페이지 하나를 받아 와요. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* 이 페이지를 불러와요. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* 그 페이지를 프로세스의 주소 공간에 추가해요. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* 다음으로 넘어가요. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* 0으로 채운 페이지를 USER_STACK에 매핑해서 최소한의 스택을 만들어요 */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* 유저 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로 가는
 * 매핑을 페이지 테이블에 추가해요.
 * WRITABLE이 true면 유저 프로세스가 그 페이지를 고칠 수 있고,
 * 아니면 읽기 전용이에요.
 * UPAGE는 이미 매핑되어 있으면 안 돼요.
 * KPAGE는 아마 palloc_get_page()로 유저 풀에서
 * 받아 온 페이지여야 할 거예요.
 * 성공하면 true를, UPAGE가 이미 매핑되어 있거나
 * 메모리 할당에 실패하면 false를 돌려줘요. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* 그 가상 주소에 이미 페이지가 있지 않은지 확인한 다음,
	 * 우리 페이지를 거기에 매핑해요. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* 여기부터의 코드는 프로젝트 3 이후에 쓰여요.
 * 프로젝트 2에서만 쓸 함수를 구현하고 싶다면
 * 위쪽 블록에 구현해요. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: 파일에서 세그먼트를 불러와요 */
	/* TODO: 이 함수는 주소 VA에서 첫 페이지 폴트가 났을 때 불려요. */
	/* TODO: 이 함수를 부를 때 VA를 쓸 수 있어요. */
}

/* FILE의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에
 * 불러와요. 모두 합쳐 READ_BYTES + ZERO_BYTES 바이트의 가상
 * 메모리를 아래처럼 초기화해요.
 *
 * - UPAGE부터 READ_BYTES 바이트는 FILE의 오프셋 OFS부터
 * 읽어 와야 해요.
 *
 * - UPAGE + READ_BYTES부터 ZERO_BYTES 바이트는 0으로 채워야 해요.
 *
 * 이 함수가 초기화한 페이지는 WRITABLE이 true면 유저 프로세스가
 * 쓸 수 있어야 하고, 아니면 읽기 전용이어야 해요.
 *
 * 성공하면 true를, 메모리 할당 에러나 디스크 읽기 에러가
 * 나면 false를 돌려줘요. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산해요.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고,
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채워요. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: lazy_load_segment에 정보를 넘길 수 있게 aux를 준비해요. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* 다음으로 넘어가요. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK에 스택 페이지(PAGE) 하나를 만들어요. 성공하면 true를 돌려줘요. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: stack_bottom에 스택을 매핑하고 그 페이지를 바로 claim 해요.
	 * TODO: 성공하면 그에 맞게 rsp를 설정해요.
	 * TODO: 그 페이지가 스택이라는 표시를 해 둬야 해요. */
	/* TODO: 여기에 코드를 써요 */

	return success;
}
#endif /* VM */
