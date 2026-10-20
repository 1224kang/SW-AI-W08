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
#include <string.h>
#include "threads/init.h"  //poweroff 함수 받아오기 위한 매크로
#include "filesys/file.h"  //filesize + file 관련 함수 받아오기 위한 매크로
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/process.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void sys_exit(int status);


extern struct lock sys_lock;

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
			bool found=false;

			if(fd==1){
				//버퍼에 저장된 size개의 문자를 콘솔에 출력
				putbuf(buffer,size);
				f->R.rax=size;
				break;
			}
			else{
				//fd와 매핑된 file 찾기 
				struct list *fd_list=&thread_current()->fdt->fd_list;
				struct list_elem *e=list_begin(fd_list);

				while(e!=list_end(fd_list)){
					struct file_fd_mapping *mapping=list_entry(e,struct file_fd_mapping,file_fd_elem);
					
					if(mapping->index==fd){
						found=true;
						//file_tell, file_length 구현 후 EOF에 도달했는지 판단 예외처리
						off_t bytes_written=file_write(mapping->mapping_file,buffer,size);
						f->R.rax=bytes_written;
						break;
					}

					e=list_next(e);
				}
				if(!found){
					f->R.rax=0;
				}
				break;

			}
		
		case SYS_EXIT:
			int status=f->R.rdi;
			sys_exit(status);	// 돌아오지 않아서, break가 없어도 아래 SYS_HALT로 새지 않아요

		case SYS_HALT:
			power_off(); 
			break;

		case SYS_FILESIZE:{/*open 구현 이후 들어가기*/
			struct fd_table *fdt=thread_current()->fdt;
			struct list_elem *e;
			struct file_fd_mapping *found = NULL; //말그대로 찾았을때 값 반환

			int fd=f->R.rdi; 

			for(e = list_begin(&fdt->fd_list); e != list_end(&fdt->fd_list); e = list_next(e)){  //1. e에 리스트 초기값을 받는다 > 2. e가 리스트
				struct file_fd_mapping *fm = list_entry(e, struct file_fd_mapping, file_fd_elem);	
				if(fm->index == fd){
					found = fm;  //일치하다면 found에 일치한 값 저장
					break;
				}
			}
			if(found == NULL) f->R.rax = -1;  
			else f->R.rax = file_length(found->mapping_file);
			
			break;
		}
		// case SYS_SEEK:

		//case SYS_TELL:

		case SYS_CREATE: {
			char* file_name=f->R.rdi;
			unsigned initial_size=f->R.rsi;
			if(file_name==NULL){
				thread_current()->exit_status=-1;					
				thread_exit();
			}
			lock_acquire(&sys_lock);
			bool success=filesys_create(file_name,initial_size);
			lock_release(&sys_lock);
			f->R.rax=success;
			break;
		}

		case SYS_REMOVE:{
			char *file_name=f->R.rdi;
			if(file_name==NULL){
				thread_current()->exit_status=-1;
				thread_exit();
			}
			lock_acquire(&sys_lock);
			bool success=filesys_remove(file_name);
			lock_release(&sys_lock);
			f->R.rax=success;
			break;
		}

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
			break;
		}

		case SYS_CLOSE: {
			int fd=f->R.rdi;

			if(fd==1 || fd==0){
				break;	
			}
			else{
				//fd와 매핑된 file 찾기 
				struct list *fd_list=&thread_current()->fdt->fd_list;
				struct list_elem *e=list_begin(fd_list);

				while(e!=list_end(fd_list)){
					struct file_fd_mapping *mapping=list_entry(e,struct file_fd_mapping,file_fd_elem);
					
					if(mapping->index==fd){
						file_close(mapping->mapping_file);
						list_remove(e);
						free(mapping);
						break;
					}

					e=list_next(e);
				}

				break;
			}	
		}


		case SYS_READ: {
			int fd=f->R.rdi;
			void *buffer=f->R.rsi;
			unsigned size=f->R.rdx;
			bool found=false;

			struct list *fd_list=&thread_current()->fdt->fd_list;
			if(fd_list==NULL){
				break;
			}
			struct list_elem *e=list_begin(fd_list);

			//실제로 읽은 바이트 수 
			int bytes_read=0;
			if (fd==0){
				for(int i=0;i<size;i++){
					((char *)buffer)[i] = (char)input_getc();
					bytes_read++;
				}
				f->R.rax=bytes_read;
				break;
			}

			if(fd==1){
				f->R.rax=-1;
				break;
			}
			
			if (fd>=2){
				//fd에 맞는 파일 찾기
				while(e!=list_end(fd_list)){
					struct file_fd_mapping *mapping=list_entry(e,struct file_fd_mapping,file_fd_elem);
					
					if(mapping->index==fd){
						found=true;
						off_t read_size=file_read(mapping->mapping_file,buffer,size);
						f->R.rax=read_size;
						break;
					}
					e=list_next(e);
				}
				
				if(!found){
					f->R.rax=-1;
				} 
				break;
			}
			break;
		}

	/* 유저의 wait(pid)가 들어오는 입구예요.
	 * pid는 RDI로 들어와요. PintOS는 프로세스 하나에 스레드 하나라 pid가 곧 tid여서 그대로 넘겨요.
	 * 결과는 return이 아니라 f->R.rax에 써야 해요.
	 * f는 유저로 돌아갈 때 복원될 레지스터 사본이라, 여기 rax가 유저 쪽 wait()의 반환값이 돼요. */
	case SYS_WAIT:
		{
			tid_t tid = f->R.rdi;
			int result = process_wait(tid);
			f->R.rax = result;
			break;
		}

	/* 유저의 exec(cmd_line)가 들어오는 입구예요.
	 * RDI에는 명령줄 문자열의 주소가 들어 있어요. 그 문자열은 유저 주소 공간에 있는데,
	 * process_exec가 맨 먼저 그 주소 공간을 정리해 버려요.
	 * 그래서 주소가 아니라 글자 내용을 커널 페이지로 먼저 복사해서 넘겨요.
	 * process_create_initd의 fn_copy와 같은 방식이고, 그 페이지는 process_exec가 반납해요.
	 * 잘못된 주소를 넘기는 경우(exec-bad-ptr)는 유저 포인터 검사가 들어오면 복사 직전에 막아야 해요. */
	case SYS_EXEC: {
		char *fn_copy;
		fn_copy = palloc_get_page (0);
		strlcpy(fn_copy, (char*)f->R.rdi, PGSIZE);

		/* 성공하면 process_exec는 돌아오지 않아요. 새 프로그램이 이 자리에서 시작해요.
		 * 돌아왔다면 실패한 거고, 원래 주소 공간은 이미 없어서 유저로 돌아갈 수 없어요.
		 * 그래서 그 자리에서 -1로 끝내요. */
		int result = process_exec(fn_copy);
		if (result == -1) sys_exit(-1);
		break;
	}

		// case SYS_FORK:
			
	}

	// printf ("system call!\n");
	// thread_exit ();
}

/* 지금 프로세스를 종료 코드 STATUS로 끝내요. 돌아오지 않아요.
 * SYS_EXIT 칸이 하던 일을 그대로 옮긴 함수예요. exec 실패처럼 커널이 -1로 끝내야 할 때도 같이 써요.
 * exit_called를 켜 두면 process_exit가 STATUS를 그대로 종료 코드로 써요. */
void sys_exit(int status)
{
	thread_current()->exit_status=status;
	thread_current()->exit_called=true;	// 정상 종료 표시. process_exit에서 이게 false면 예외로 죽은 거라 -1로 봐요

	//프로세스 종료시키기
	thread_exit ();
}
