#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include <stdarg.h>
#include "threads/init.h"  //poweroff 함수 받아오기 위한 매크로
#include "filesys/file.h"  //filesize + file 관련 함수 받아오기 위한 매크로

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 시스템 콜(System Call).
 *
 * 이전에는 시스템 콜 서비스가 인터럽트 핸들러에 의해 처리되었다
 * (예: Linux의 int 0x80).
 *
 * 그러나 x86-64에서는 제조사가 시스템 콜을 요청하기 위한
 * 효율적인 경로인 `syscall` 명령어를 제공한다.
 *
 * `syscall` 명령어는 모델별 레지스터(Model Specific Register, MSR)에
 * 저장된 값을 읽어 동작한다.
 *
 * 자세한 내용은 매뉴얼을 참고하라.
 */

#define MSR_STAR 0xc0000081         /* Segment selector msr */    
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch(f->R.rax){
		case SYS_WRITE:
			int fd=f->R.rdi;
			void *buffer=f->R.rsi;
			unsigned size=f->R.rdx;

			if(fd==1){
				//버퍼에 저장된 size개의 문자를 콘솔에 출력
				putbuf(buffer,size);
				f->R.rax=size;
				break;
			}
			else{break;}
		
		case SYS_EXIT:
			int status=f->R.rdi;
			thread_current()->exit_status=status;

			//프로세스 종료시키기
			thread_exit ();			
		
		case SYS_HALT:
			power_off(); 
			break;

		//case SYS_FILESIZE:/*open 구현 이후 들어가기*/
			
			

		// case SYS_SEEK:

		//case SYS_TELL:

		// case SYS_CREATE:

		// case SYS_REMOVE:

		case SYS_OPEN: {
			char* file_name=f->R.rdi;
			if(file_name==NULL){
				f->R.rax=-1;
				break;
			}
			
			struct file* file=filesys_open(file_name);
			if(file==NULL){
				f->R.rax=-1;
				break;
			}

			struct fd_table *fdt=thread_current()->fdt;
			
			//새로운 fdt 할당 
			struct file_fd_mapping *new_fd=malloc(sizeof(struct file_fd_mapping));
			if(new_fd==NULL){ //malloc 실패
				f->R.rax=-1;
				break;
			}
			new_fd->index=fdt->next_fd_index;
			new_fd->mapping_file=file;
			list_push_back(&fdt->fd_list,&new_fd->file_fd_elem);

			//인덱스 하나 추가 
			fdt->next_fd_index+=1;
			//반환값
			f->R.rax=new_fd->index;
		}
		


		// case SYS_CLOSE:

		case SYS_READ: {
		
			// int fd=f->R.rdi;
			// void *buffer=f->R.rsi;
			// unsigned size=f->R.rdx;

			// //실제로 읽은 바이트 수 
			// int bytes_read=0;
			// if (fd==0){
			// 	input_getc();
			// }
			// if (fd==2){
				
			// }
		}

		// case SYS_WAIT:

		// case SYS_EXEC:

		// case SYS_FORK:
			
	}

	// printf ("system call!\n");
	// thread_exit ();
}