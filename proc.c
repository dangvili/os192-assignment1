#include "schedulinginterface.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"


extern PriorityQueue pq;
extern RoundRobinQueue rrq;
extern RunningProcessesHolder rpholder;

static void (*sched_policy_arr[])(struct cpu*) = {
[SP_rrs]  sp_round_robin, 
[SP_ps]   sp_priority,
[SP_eps]  sp_ext_priority,

}; 

static int current_sched_strat = 1;      // (Added By Ido & Dan) holds the current scheduling strategy

static long long tq_timestamp = 1;       // accumulates the number of timestamp since the OS initiated

//Schedule policies Strategy Array:

long long getAccumulator(struct proc *p) {
	return p->accumulator;
}

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  enqueue_by_state(p);

  p->priority = NP_PRIORITY;

  if(current_sched_strat == SP_ps)
    p->accumulator = get_min_acc(); 

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->ctime = ticks; 

  np->state = RUNNABLE;

  update_pref_field(-ticks, RETIME, np);
  
  enqueue_by_state(np);

  np->priority = NP_PRIORITY;

  if(current_sched_strat == SP_ps)
    np->accumulator = get_min_acc();  

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(int status)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  curproc->status = status;

  // Jump into the scheduler, never to return.
  update_pref_field(-ticks, RUTIME, curproc);
  curproc->state = ZOMBIE;
  curproc->ttime = ticks;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(int* status)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        if(status != null)
          *status = p->status;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    sched_policy_arr[current_sched_strat](c);
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  struct proc *p = myproc();
  //myproc()->state = RUNNABLE;
  update_pref_field(ticks, RUTIME, p);
  p->state = RUNNABLE;
  update_pref_field(-ticks, RETIME, p);
  enqueue_by_state(p);
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;

  update_pref_field(-ticks, RUTIME, p);

  p->state = SLEEPING;

  update_pref_field(-ticks, STIME, p);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      update_pref_field(ticks, STIME, p);
      p->state = RUNNABLE;
      update_pref_field(-ticks, RETIME, p);
      if(current_sched_strat == SP_ps)
        p->accumulator = get_min_acc(); 
      enqueue_by_state(p);

    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        update_pref_field(ticks, STIME, p);
        p->state = RUNNABLE;
        update_pref_field(-ticks, RETIME, p);
        enqueue_by_state(p);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
/*
  if(parent has child with @pid){
    detach the child from parent
    connect child to the init process
    return 0
  }
  else
    return -1
  
  */ 

int
detach(int pid)
{
  struct proc *p;
  struct proc *curr_proc = myproc();
  
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curr_proc && p->pid == pid){
      p->parent = initproc; 
      release(&ptable.lock); 
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

void 
priority(int priority)
{
  if((priority>0 && priority<=10 ) || (priority == 0 && current_sched_strat == SP_eps))
    myproc()->priority = priority;
}


void 
policy (int policy_iden)
{

  if(/*policy_iden != current_sched_strat && */policy_iden<=3 && policy_iden>0){//new_policy!=curr_policy && 0<new_policy<=3
    acquire(&ptable.lock);
    if(policy_iden == SP_eps && current_sched_strat == SP_rrs)//new_policy = 3 && curr_policy = 1
      rrq.switchToPriorityQueuePolicy();
    

    else if(policy_iden == SP_rrs){//new_policy = 1
      set_all_accumulators(RRS_ACC_VAL);
      pq.switchToRoundRobinPolicy(); 
    }

    else /*if(policy_iden == SP_ps)*/{//new_policy = 2
      set_filtered_priorities(0,1);
      if(current_sched_strat == SP_rrs)
        rrq.switchToPriorityQueuePolicy();
    }

    current_sched_strat = policy_iden;
    release(&ptable.lock); 
  }

  else
    panic("the desired policy is out of bounds");

}

int
wait_stat(int* status, struct perf* performace)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;

        performace->ctime = p->ctime; 
        performace->ttime = p->ttime;
        performace->stime = p->stime;
        performace->retime = p->retime;
        performace->rutime = p->rutime;

        p->state = UNUSED;

        if(status != null)
          *status = p->status;

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

void set_all_accumulators(int value){
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    p->accumulator = value;
}

void set_filtered_priorities(int filter, int value){
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->priority == filter)
      p->priority = value;
}

//enqueue according to state:
void enqueue_by_state(struct proc* p){
  if(current_sched_strat == SP_rrs)
    rrq.enqueue(p); 
  else if(current_sched_strat == SP_ps || current_sched_strat == SP_eps)
    pq.put(p); 
  else
    panic("incorrect scheduling strategy state\n"); 
}

void swtch_to_proc(struct proc* p, struct cpu* c){
  // Switch to chosen process.  It is the process's job
  // to release ptable.lock and then reacquire it
  // before jumping back to us.
  c->proc = p;
  switchuvm(p);

  update_pref_field(ticks, RETIME, p);

  p->state = RUNNING;

  update_pref_field(-ticks, RUTIME, p);

  p->last_tq = tq_timestamp; 
  ++tq_timestamp; 

  rpholder.add(p); 

  swtch(&(c->scheduler), p->context);
  switchkvm();

  // Process is done running for now.
  // It should have changed its p->state before coming back.
  
  //update_pref_field(ticks, RUTIME, p);
  c->proc = 0;


  rpholder.remove(p);
}

//Round Robin Sheduling Algorithm:
void sp_round_robin (struct cpu* c){
  if(!rrq.isEmpty()){
    struct proc *p = rrq.dequeue(); 
    swtch_to_proc(p, c); 
  }
}

//Priority Scheduling Algorithm:

void sp_priority (struct cpu* c){
  if(!pq.isEmpty()){
    
    struct proc *p = pq.extractMin(); 
    swtch_to_proc(p, c); 

    if(p->state == RUNNABLE){
      p->accumulator += p->priority;  
      //pq.put(p);
    }

  }

}

long long get_min_acc(){
  long long runnable_acc = LLONG_MAX; 
  boolean success_pq = pq.getMinAccumulator(&runnable_acc); 

  long long running_acc = LLONG_MAX; 
  boolean success_rp = rpholder.getMinAccumulator(&running_acc);

  if(success_pq || success_rp)
    return min(runnable_acc, running_acc);  

  return 0; 
}

long long min (long long a, long long b){
  return a > b ? b : a; 
}

//Extended Priority Scheduling Algorithm:

void sp_ext_priority (struct cpu* c){

  if(!pq.isEmpty()){
    
    struct proc *p = proc_to_run(); 

    swtch_to_proc(p, c); 

    if(p->state == RUNNABLE){
      p->accumulator += p->priority;  
      //pq.put(p);
    }
  }

}

struct proc* proc_to_run(void){
  if(tq_timestamp%TQ_THRESHOLD == 0)
    return proc_with_min_timestamp(); 

  return pq.extractMin(); 
  
}

struct proc* proc_with_min_timestamp(void) {

  struct proc *p;
  struct proc *result = null;
  long long min_timestamp = LLONG_MAX;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE && min_timestamp > p->last_tq){
      min_timestamp = p->last_tq;
      result = p; 
    }

  }
  pq.extractProc(result);
  return result;
}


void update_pref_field(int curr_ticks, int f_iden, struct proc* p){
  switch (f_iden){
    case CTIME:
    case TTIME:
        panic("not supposed to be updated");
        break;

    case STIME:
        p->stime += curr_ticks; 
        break;

    case RETIME:
        p->retime += curr_ticks; 
        break;

    case RUTIME:
        p->rutime += curr_ticks; 
        break;
  
    default:
        panic("field does not exist");
  }

}



