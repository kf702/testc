#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include "convert2b.h"

int main() {

  struct Input_info info;
  memset(&info, 0, sizeof(struct Input_info));
  sprintf(info.ip2Bfile, "%s", "HX1-Or_GRAS_MOMAG-IP_SCI_P_20211113081637_20211113144007_00573_A.2B");
  sprintf(info.op2Bfile, "%s", "HX1-Or_GRAS_MOMAG-OP_SCI_P_20211113081637_20211113144007_00573_A.2B");
  sprintf(info.config_parafile, "%s", "config_00573_32.dat");
  sprintf(info.outputpath, "%s", "output_dir");

  int ret = MoMAG_magdata_l2c_32(&info);
  printf ("MoMAG_magdata_l2c_32 ret = %d\n", ret);

  return 0;
}
