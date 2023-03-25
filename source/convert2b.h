#ifndef __CONVERT2B_H__
#define __CONVERT2B_H__

struct Input_info {
  char ip2Bfile[512];
  char op2Bfile[512];
  char config_parafile[512];
  char outputpath[512];
};

int MoMAG_magdata_l2c(struct Input_info *input);

#endif
