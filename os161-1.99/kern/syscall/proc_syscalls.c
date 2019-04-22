#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include "opt-A2.h"
#include <vfs.h>
#include <kern/fcntl.h>
#include <mips/trapframe.h>
#include <synch.h>
#include <kern/fcntl.h>


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);

#if OPT_A2

  int arrayNum = array_num(&p->child);
  for (int i = 0; i < arrayNum; ++i) 
  	array_remove(&p->child, i);

#endif //OPT_A2

  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

#if OPT_A2 

  p->exitCode = _MKWAIT_EXIT(exitcode);
  p->canExit = true;
  lock_acquire(p->waitLock);
  cv_broadcast(p->waitCV, p->waitLock);
  lock_release(p->waitLock);

#endif //OPT_A2

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
	*retval = curproc->pid;
#else
  *retval = 1;
#endif //OPT_A2
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
#if OPT_A2

  if (pid < 0) return ESRCH;
  struct proc *theChild = NULL;
  for (unsigned int i = 0; i < array_num(&curproc->child); ++i) {
  	theChild = array_get(&curproc->child, i);
  	if (pid == theChild->pid) break;
  }
  if (theChild->parent != curproc) return ECHILD;

  lock_acquire(theChild->waitLock);
  while(!theChild->canExit) 
  	cv_wait(theChild->waitCV, theChild->waitLock);
  lock_release(theChild->waitLock);
  exitstatus = theChild->exitCode;

#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2
	//sys_fork implementation to be written

int 
sys_fork (struct trapframe *tf, 
		pid_t *pid) {
	struct proc *theChild = proc_create_runprogram(curproc->p_name);
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		proc_destroy(theChild);
		return ENOMEM;
	}
	as_copy(curproc_getas(), &(theChild->p_addrspace));
	if (theChild->p_addrspace == NULL) {
		as_destroy(as);
		proc_destroy(theChild);
		return ENOMEM;
	}
	struct trapframe *ntf = kmalloc(sizeof(struct trapframe));
	if (ntf == NULL) {
		as_destroy(as);
		proc_destroy(theChild);
		return ENOMEM;
	}
	memcpy(ntf, tf, sizeof(struct trapframe));
	//forking
	int result = thread_fork(curthread->t_name, theChild, &enter_forked_process, ntf, 0);
	if (result) {
		as_destroy(as);
		proc_destroy(theChild);
		kfree(ntf);
		return result;
	}
	//forked
	theChild->parent = curproc;
	lock_acquire(curproc->waitLock);
	array_add(&curproc->child, theChild, NULL);
	lock_release(curproc->waitLock);
	lock_acquire(theChild->exitLock);
	*pid = theChild->pid;
	return 0;
}

#endif //OPT_A2

#if OPT_A2
int 
sys_execv (const char *program, 
		char **args)
{
	if (program == NULL) return EFAULT;

	struct addrspace *as = curproc_getas();
	struct addrspace *newas;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	size_t progLength = strlen(program) + 1;
	char *newProgram = kmalloc(progLength * sizeof(char *));
	result = copyinstr((userptr_t) program, newProgram, progLength, NULL);

	if (newProgram == NULL) return ENOMEM;
	if (result) return result;

	int argc = 0;
	while (args[argc] != NULL) {
		if (strlen(args[argc]) > 128) return E2BIG;
		argc++;
	}

	char **newArgs = kmalloc((argc + 1) * sizeof(char *));
	for (int i = 0; i < argc; i++) {
		newArgs[i] = kmalloc((strlen(args[i]) + 1) * sizeof(char));
		result = copyinstr((userptr_t) args[i], newArgs[i],strlen(args[i]) + 1, NULL);
		if (result) return result;
	}

	newArgs[argc] = NULL;

	//start copying code from runprogram.c LOL 
	//love copying

	char *progname = kstrdup(program);

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	kfree(progname);

	/* Create a new address space. */
	newas = as_create();
	if (newas ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	curproc_setas(newas);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	result = as_define_stack(newas, &stackptr);
	if (result) {
		curproc_setas(as);
		return result;
	}

	while ((stackptr % 8) != 0) stackptr--;

	vaddr_t arg_ptr[argc + 1];
	for(int i = argc - 1; i >= 0; i--) {
		stackptr -= strlen(newArgs[i]) + 1;
		result = copyoutstr(newArgs[i], (userptr_t) stackptr, strlen(newArgs[i]) + 1, NULL);
		if (result) return result;
		arg_ptr[i] = stackptr;
	}

	while (stackptr % 4 != 0) stackptr--;

	arg_ptr[argc] = 0;

	for (int i = argc; i >= 0; i--) {
		stackptr -= ROUNDUP(sizeof(vaddr_t), 4);
		result = copyout(&arg_ptr[i], (userptr_t) stackptr, sizeof(vaddr_t));
		if (result) return result;
	}

	as_destroy(as);

	/* Warp to user mode. */
	enter_new_process(argc, (userptr_t) stackptr,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}
#endif //OPT_A2

