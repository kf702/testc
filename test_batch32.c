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

void test_shift(char** shift_files, int shift_files_cnt, int hz);
int MoMAG_magdata_l2c_32_test(struct Input_info *input);

char* input_op_dir = "input_op_dir";
char* config_dir = "config_dir";
char* output_dir = "output_dir";

int test(char* opfile) {
  struct Input_info info;
  memset(&info, 0, sizeof(struct Input_info));
  sprintf(info.ip2Bfile, "%s", opfile);
  sprintf(info.op2Bfile, "%s", opfile);
  sprintf(info.config_parafile, "%s", "----");
  sprintf(info.outputpath, "%s", output_dir);
  int ret = MoMAG_magdata_l2c_32_test(&info);
  if (ret != 0) {
    printf ("----------------------- MoMAG_magdata_l2c_32 ret = %d\n", ret);
  }
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    printf ("Usage: test_batch32 <input_op_dir> <config_dir> <output_dir>\n");
    return 0;
  }
  input_op_dir = argv[1];
  config_dir = argv[2];
  output_dir = argv[3];

  char* shift_files[1000];
  int shift_files_cnt = 0 ;
  struct dirent * ptr;
  DIR * dir2 = opendir(config_dir);
  if (!dir2) {
    printf("config_dir: %s not found\n", config_dir);
    return -1;
  }
  while((ptr = readdir(dir2)) != NULL) {
    if (ptr->d_name[0] != '.') {
      shift_files[shift_files_cnt] = malloc(200);
      sprintf(shift_files[shift_files_cnt], "%s/%s", config_dir, ptr->d_name);
      shift_files_cnt ++;
    }
  }
  printf ("total config files: %d\n", shift_files_cnt);
  test_shift(shift_files, shift_files_cnt, 32);


  char opfilepath[512] = {0};
  dir2 = opendir(input_op_dir);
  if (!dir2) {
    printf("input_op_dir: %s not found\n", input_op_dir);
    return -1;
  }
  while((ptr = readdir(dir2)) != NULL) {
    if (ptr->d_name[0] != '.') {
      sprintf(opfilepath, "%s/%s", input_op_dir, ptr->d_name);
      printf("\n---- do file: %s\n", opfilepath);
      test(opfilepath);
    }
  }

  return 0;
}
