#pragma once

//constants for scheduling algorithms
#define SP_rrs    1
#define SP_ps     2
#define SP_eps    3

// constants for priority
#define NP_PRIORITY 5

//misc. constants
#define LLONG_MAX 9223372036854775807
#define TQ_THRESHOLD 100
#define RRS_ACC_VAL 0

//performace field identifiers:
#define CTIME 1
#define TTIME 2
#define STIME 3
#define RETIME 4
#define RUTIME 5

//perf struct - 3.5
struct perf {
  int ctime;
  int ttime;
  int stime;
  int retime;
  int rutime;
};


// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                       // Size of process memory (bytes)
  pde_t* pgdir;                  // Page table
  char *kstack;                  // Bottom of kernel stack for this process
  enum procstate volatile state; // Process state
  int pid;                       // Process ID
  struct proc *parent;           // Parent process
  struct trapframe *tf;          // Trap frame for current syscall
  struct context *context;       // swtch() here to run process
  void *chan;                    // If non-zero, sleeping on chan
  int killed;                    // If non-zero, have been killed
  struct file *ofile[NOFILE];    // Open files
  struct inode *cwd;             // Current directory  
  char name[16];                 // Process name (debugging)

  int status;                    //the process' status

  long long accumulator;         // priority's accumulator
  int priority;                  // process's priority
  long long last_tq;             // a number indicating the last time the process has run

  long long ctime;               // process creation time
  long long ttime;               // process termination time
  long long stime;                // the total time the process spent in the SLEEPING state
  long long retime;              // the total time the process spent in the READY state
  long long rutime;              // the total time the process spent in the RUNNING state
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
