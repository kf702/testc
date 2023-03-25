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
  sprintf(info.ip2Bfile, "%s", "input_2B_files/HX1-Or_GRAS_MOMAG-IP_SCI_P_20211201001331_20211201071807_00633_A.2B");
  sprintf(info.op2Bfile, "%s", "input_2B_files/HX1-Or_GRAS_MOMAG-OP_SCI_P_20211231091044_20211231161524_00736_A.2B");
  sprintf(info.config_parafile, "%s", "config_data_dir/HX1-Or_MOMAG_BIO_20211231091044_20211231161524_D001_V3_SHIFT.dat");
  sprintf(info.outputpath, "%s", "output_dir");

  int ret = MoMAG_magdata_l2c_32(&info);
  printf ("MoMAG_magdata_l2c ret = %d\n", ret);

  return 0;
}
