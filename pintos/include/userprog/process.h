#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* 자식 하나의 종료 기록이에요 (#113).
   부모가 malloc해서 자기 child_record_list에 넣고, wait나 부모 exit에서 free해요.
   자식은 끝날 때 status를 쓰고 child_address를 NULL로 바꾸는 것만 해요. */
struct record
{
    tid_t tid;                      // 자식의 tid. wait(tid)로 찾을 때 써요
    int status;                     // 자식의 종료 코드
    struct thread *child_address;   // 자식 struct thread 주소. NULL이면 자식이 이미 끝난 거예요
    struct semaphore process_sema;  // 부모는 wait에서 down, 자식은 exit에서 up
    struct list_elem child_elem;    // 부모의 child_record_list에 매달리는 원소
};

#endif /* userprog/process.h */
