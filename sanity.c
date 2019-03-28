#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"


//detach constants:
#define WPERIOD 700
#define CHILD 0
#define WRONG_PID 99
#define SUCCESS 0
#define FAILURE -1

//exit&wait constants:
#define WPERIOD_E 150


void exit_and_wait_test(){
    int e_status[] = {0,0,0,0,0};
    for(int i=0; i<5; ++i){
        if(fork() == 0){
            sleep(WPERIOD_E);
            exit(i);
        }

        wait(&e_status[i]);

        if(e_status[i] != i){
            printf(2,"TEST FAILED");
            exit(-1);
        }
        printf(1,"%d child exit status is: %d\n",i, e_status[i]);
    }

    printf(1,"WAIT&EXIT_TEST - PASSED!!!!!!!!!!!\n");
}


void detach_test(){
    int pids[] ={0,0,0,0,0}; 
    int detach_res[] = {0,0,0,0,0};

    pids[0] = fork(); 
    if(pids[0] == CHILD){
        sleep(WPERIOD);
        exit(WPERIOD);
    }

    pids[1] = fork(); 
    if(pids[1] == CHILD){
        sleep(WPERIOD);
        exit(WPERIOD);
    }

    pids[2] = fork(); 
    if(pids[2] == CHILD){
        sleep(WPERIOD);
        exit(WPERIOD);
    }

    pids[3] = fork(); 
    if(pids[3] == CHILD){
        sleep(WPERIOD);
        exit(WPERIOD);
    }

    pids[4] = fork(); 
    if(pids[4] == CHILD){
        sleep(WPERIOD);
        exit(WPERIOD);
    }

    detach_res[0] = detach(pids[0]);
    detach_res[1] = detach(pids[1]);
    detach_res[2] = detach(pids[2]);
    detach_res[3] = detach(pids[3]);
    detach_res[4] = detach(pids[4]);

    int detach_fail = detach(WRONG_PID);

    if(detach_res[0] != SUCCESS || detach_res[1] != SUCCESS || detach_res[2] != SUCCESS 
        || detach_res[3] != SUCCESS || detach_res[4] != SUCCESS || detach_fail != FAILURE)
        printf(2, "detach has faild - existing child test");

    else
        printf(1,"DETACH_TEST - PASSED!!!!!!!!!!!\n");
    
    sleep(WPERIOD * 2);

    exit(0);

}


int main (int argc, char *argv[]){
    exit_and_wait_test();
    detach_test();
    return 0;
}


// int main(int argc, char *argv[]){
//     int pidChild1;
//     int pidChild2;
//     int pidChild3;
//     int pidChild4;
//     int pidChild5;
//     int pidChild6;
//     int detachResult;
//     int secToWait = 500;
    
//     if((pidChild1 = fork()) == 0){
//         sleep(secToWait);
//         exit(secToWait);
//     }

//     if((pidChild2 = fork()) == 0){
//         sleep(secToWait);
//         exit(secToWait);
//     }

//     if((pidChild3 = fork()) == 0){
//         sleep(secToWait);
//         exit(secToWait);
//     }

//     if((pidChild4 = fork()) == 0){
//         sleep(secToWait);
//         exit(secToWait);
//     }

//     if((pidChild5 = fork()) == 0){
//         sleep(secToWait);
//         exit(secToWait);
//     }

//     if((pidChild6 = fork()) == 0){
//         sleep(secToWait);
//         exit(secToWait);
//     }
    
//     detachResult = detach(77);
//     printf(1,"detach result for not my child is: %d\n",detachResult);

//     detachResult = detach(pidChild1);
//     printf(1,"detach result for child 1 is: %d\n",detachResult);

//     detachResult = detach(pidChild2);
//     printf(1,"detach result for child 2 is: %d\n",detachResult);

//     detachResult = detach(pidChild3);
//     printf(1,"detach result for child 3 is: %d\n",detachResult);

//     detachResult = detach(pidChild4);
//     printf(1,"detach result for child 4 is: %d\n",detachResult);

//     detachResult = detach(pidChild5);
//     printf(1,"detach result for child 5 is: %d\n",detachResult);

//     detachResult = detach(pidChild6);
//     printf(1,"detach result for child 6 is: %d\n",detachResult);

//     detachResult = detach(105);
//     printf(1,"detach result for not my child is: %d\n",detachResult);

//     detachResult = detach(pidChild1);
//     printf(1,"second detach result for child 1 is: %d\n",detachResult);

//     detachResult = detach(pidChild2);
//     printf(1,"second detach result for child 2 is: %d\n",detachResult);

//     detachResult = detach(pidChild3);
//     printf(1,"second detach result for child 3 is: %d\n",detachResult);

//     detachResult = detach(pidChild4);
//     printf(1,"second detach result for child 4 is: %d\n",detachResult);

//     detachResult = detach(pidChild5);
//     printf(1,"second detach result for child 5 is: %d\n",detachResult);

//     detachResult = detach(pidChild6);
//     printf(1,"second detach result for child 6 is: %d\n",detachResult);

//     detachResult = detach(44);
//     printf(1,"detach result for not my child is: %d\n",detachResult);
    
//     sleep(secToWait * 2);

//     if(wait(null) != -1){
//         printf(1,"Succeded in waiting for a child, possible detach error\n");
//     }
//     else{
//         printf(1,"no child left to wait for\n");
//     }

//     exit(0);
// }