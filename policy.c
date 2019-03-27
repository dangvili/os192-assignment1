#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(2, "Usage: policy policy_iden...\n");
    exit(-1);
  }

  policy(atoi(argv[1]));

  exit(0);
} 