#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include "convert2b.h"

#define SUB14 1

struct one_data {
  uint64_t time;
  double bx;
  double by;
  double bz;
  char *flag;
  int arrage;
};

// 2B 文件长度不固定，使用 数组 + 链表 存储
struct more_data {
  int raw_sample;
  int now_sample;
  int buf_len;
  int data_len;
  struct one_data buf[512];
  struct more_data * next;
  struct more_data * prev;
};

struct shift_one_data {
  uint64_t time;
  double bx;
  double by;
  double bz;
  char *flag;
};
struct shift_32_data {
  uint64_t sec;
  int data_len;
  struct shift_one_data buf[33];
};

int proccess_hz = 1;

// 记录每次处理的起始时间和结束时间，秒
uint64_t filter_start_s = 0;
uint64_t filter_end_s = 0;

struct sort_t {
  uint64_t time;
  uint64_t end_time;
  char filename[512];
};

// 排序过的 shift 文件数组
struct sort_t * shift_sort_arr = NULL;
int shift_sort_arr_cnt = 0;
struct one_data * shift_data = NULL;
int shift_data_len = 0;

char out_prefix1[512];
char out_prefix32[512];

struct more_data * new_data() {
  struct more_data * aa = malloc(sizeof(struct more_data));
  memset(aa, 0, sizeof(struct more_data));
  aa->buf_len = 256;
  return aa;
}

void free_all(struct more_data * first) {
  struct more_data * next = first;
  struct more_data * xx ;
  while(next != NULL) {
    xx = next;
    next = next->next;
    free(xx);
  }
}

void print_shift_data() {
  if (!shift_data) return ;
  int size = filter_end_s - filter_start_s + 1;
  int i = 0;
  for(i = 0; i < size; i ++) {
    struct one_data * one = shift_data + i;
    printf ("%lu %.3f %.3f %.3f\n", one->time, one->bx, one->by, one->bz);
  }
}

// shift 文件 时间格式： 2021-11-13T23:38:35.798
// 转换成 秒， 0 = fail
uint64_t string_to_s(char* time) {
  int year, month, day, hour, min, second, mirco;
  int cnt = sscanf(time, "%d-%d-%dT%d:%d:%d.%d", &year, &month, &day, &hour, &min, &second, &mirco);
  if (cnt != 7) return 0;

  struct tm *tmp = (struct tm*)malloc(sizeof(struct tm));
  memset(tmp, 0, sizeof(struct tm));
  tmp->tm_year = year - 1900;
  tmp->tm_mon = month - 1;
  tmp->tm_mday = day;
  tmp->tm_hour = hour;
  tmp->tm_min = min;
  tmp->tm_sec = second;

  time_t s = mktime(tmp);
  free(tmp);

  if (s == -1) {
    printf ("string_to_s: %s\n", strerror(errno));
    return 0;
  }

  uint64_t ms = (uint64_t)s;
  return ms;
}

// 2B 文件 时间格式： 2022-01-31T15:21:57.091750Z
// 转换成微秒， 0 = fail
uint64_t string_to_ms(char* time) {
  int year, month, day, hour, min, second, mirco;
  int cnt = sscanf(time, "%d-%d-%dT%d:%d:%d.%dZ", &year, &month, &day, &hour, &min, &second, &mirco);
  if (cnt != 7) return 0;

  struct tm *tmp = (struct tm*)malloc(sizeof(struct tm));
  memset(tmp, 0, sizeof(struct tm));
  tmp->tm_year = year - 1900;
  tmp->tm_mon = month - 1;
  tmp->tm_mday = day;
  tmp->tm_hour = hour;
  tmp->tm_min = min;
  tmp->tm_sec = second;

  time_t s = mktime(tmp);
  free(tmp);

  if (s == -1) {
    printf ("string_to_ms: %s\n", strerror(errno));
    return 0;
  }

  //printf ("%s %ld %d\n", time, s, mirco);
  uint64_t ms = (uint64_t)s;
  ms = ms * 1000000 + mirco;
  return ms;
}

void ms_to_string(uint64_t ms, char* buf) {
  int mirco = ms % 1000000;
  time_t t = ms / 1000000;
  //strftime(buf, 128, "%FT%T", localtime(&t));
  strftime(buf, 128, "%Y-%m-%dT%H:%M:%S", localtime(&t));
  sprintf(buf + strlen(buf), ".%06dZ", mirco);
}

void ms_to_date(uint64_t ms, char* buf) {
  time_t t = ms / 1000000;
  strftime(buf, 128, "%Y-%m-%d", localtime(&t));
}

int ms_to_day(uint64_t ms) {
  time_t t = ms / 1000000;
  struct tm * tm = localtime(&t);
  return tm->tm_mday;
}

// 0 = ok, 1 = fail, 2 = diff sample, 3 = trash
int parse_line(char* line, struct more_data * data) {
  char mline[1000] = {0};
  snprintf(mline, sizeof(mline), "%s", line);
  char *token, *sptr, *str;
  int i = 0;
  struct one_data * one = data->buf + data->data_len;
  one->flag = "XX";
  for (str = mline; ; str = NULL) {
    token = strtok_r(str, " ", &sptr);
    if (token == NULL) break;
    i ++;
    //printf("%d %s\n", i, token);
    if (i == 1) {
      one->time = string_to_ms(token);
      if (one->time == 0) return 1;
      if (filter_start_s == 0) filter_start_s = one->time / 1000000;
      filter_end_s = one->time / 1000000;
    } else if (i == 3) {
      int spl = atoi(token);
      if (spl != 1 && spl != 32 && spl != 128) return 1;
      if (data->data_len == 0) {
        data->raw_sample = spl;
        data->now_sample = spl;
      } else {
        if (data->raw_sample != spl) {
	    //printf("%d, len=%d\n", data->raw_sample, data->data_len);
	    return 2;
	}
      }
    } else if (i == 7) {
      one->bx = atof(token);
    } else if (i == 8) {
      one->by = atof(token);
    } else if (i == 9) {
      one->bz = atof(token);
    } else if (i == 18) {
      if (0 != strncmp(token,"0x00", 4)) {
	  char ttt[32] = {0};
	  snprintf(ttt, 20, "%s", line);
          printf("%s not end of 0x00\n", ttt);
          return 3;
      }
    }
  }
  if (i < 18) {
      printf("line lack 18 column\n");
      return 1;
  }
  return 0;
}

struct more_data * load_one_file(char* filename, struct more_data * current) {
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) return current;
  char * line = NULL;
  size_t len = 0;
  int nread;

  while ((nread = getline(&line, &len, fp)) != -1) {
    if (line[0] == '#') continue;
    if (len < 50) continue;
    //printf("%s\n", line);
    int ok = parse_line(line, current);
    if (ok == 0) {
      current->data_len ++;
      if (current->data_len == current->buf_len) {
        struct more_data * new_one = new_data();
	      new_one->prev = current;
        current->next = new_one;
        current = new_one;
      }
    } else if (ok == 2) {
        struct more_data * new_one = new_data();
	      new_one->prev = current;
        current->next = new_one;
        current = new_one;
        int ok2 = parse_line(line, current);
        if (ok2 == 0) current->data_len ++;
    }
  }

  free(line);
  fclose(fp);

  return current;
}

struct more_data * load_files(char **files, int cnt) {
  struct more_data * first = new_data();
  struct more_data * current = first;

  int i = 0;
  for(i = 0 ; i < cnt ; i ++) {
    char* filename = files[i];
    current = load_one_file(filename, current);
  }

  return first;
}

// -1 = error, 0 = ok, 1 = over
int parse_shift_line_32(char* line, int last_got) {
  char mline[1000] = {0};
  snprintf(mline, sizeof(mline), "%s", line);
  char *token, *sptr, *str;
  int i = 0;
  struct shift_32_data * one32 = NULL;
  struct shift_one_data * one = NULL;

  for (str = mline; ; str = NULL) {
    token = strtok_r(str, " ", &sptr);
    if (token == NULL) break;
    i ++;
    if (i == 1) {
      uint64_t ms = string_to_ms(token);
      uint64_t sec = ms / 1000000;
      if (sec == 0) {
        printf ("parse_shift_line time error: %s\n", token);
        return -1;
      }
      if (sec < filter_start_s) return 0;
      if (sec > filter_end_s) return 1;

      int index = sec - filter_start_s;
      one32 = shift_data + index;
      if (one32->sec > 0 && last_got > 100) return 0;
      one32->sec = sec;

      if (one32->data_len > 32) {
        printf ("parse_shift_line time error: %s\n", token);
        return 0;
      }

      one = one32.buf[one32->data_len];
      one->time = ms;
      one->flag = "00";
      
      one32->data_len ++;
    } else if (i == 3) {
      if (0 == strncmp(token,"NaN", 3)) {
        //printf ("parse_shift_line got NaN\n");
        //return -1;
        one->bx = 0;
      } else {
        one->bx = atof(token);
      }
    } else if (i == 4) {
      if (0 == strncmp(token,"NaN", 3)) {
        //printf ("parse_shift_line got NaN\n");
        //return -1;
        one->by = 0;
      } else {
        one->by = atof(token);
      }
    } else if (i == 5) {
      if (0 == strncmp(token,"NaN", 3)) {
        //printf ("parse_shift_line got NaN\n");
        //return -1;
        one->bz = 0;
      } else {
        one->bz = atof(token);
      }
    } else if (i == 6) {
      if (0 == strncmp(token,"00", 2)) one->flag = "00";
      else if (0 == strncmp(token,"30", 2)) one->flag = "00";
      else if (0 == strncmp(token,"05", 2)) one->flag = "05";
      else if (0 == strncmp(token,"08", 2)) one->flag = "08"; 
      else if (0 == strncmp(token,"15", 2)) one->flag = "15"; 
      else if (0 == strncmp(token,"11", 2)) one->flag = "11"; 
      else if (strlen(token) == 2) { printf("parse config data, error flag \"%s\"\n", token); return -1; }
      else if (0 == strncmp(token,"0", 1)) one->flag = "00";
      else if (0 == strncmp(token,"2", 1)) one->flag = "02";
      else if (0 == strncmp(token,"5", 1)) one->flag = "05";
      else if (0 == strncmp(token,"8", 1)) one->flag = "08";
      else { printf("parse config data, error flag %s\n", token); return -1; }
    }
  }
  if (i < 6) return -1;
  return 0;
}


// -1 = error, 0 = ok, 1 = over
int parse_shift_line(char* line, int last_got) {
  char mline[1000] = {0};
  snprintf(mline, sizeof(mline), "%s", line);
  char *token, *sptr, *str;
  int i = 0;
  struct one_data * one = NULL;

  for (str = mline; ; str = NULL) {
    token = strtok_r(str, " ", &sptr);
    if (token == NULL) break;
    i ++;
    //printf("%d %s\n", i, token);
    if (i == 1) {
      uint64_t sec = string_to_s(token);
      if (sec == 0) {
        printf ("parse_shift_line time error: %s\n", token);
        return -1;
      }
      if (sec < filter_start_s) return 0;
      if (sec > filter_end_s) return 1;

      int index = sec - filter_start_s;
      one = shift_data + index;
      if (one->time > 0 && last_got > 100) return 0;
      one->time = sec;
      one->flag = "00";

    } else if (i == 6) {
      if (0 == strncmp(token,"NaN", 3)) {
        //printf ("parse_shift_line got NaN\n");
        //return -1;
        one->bx = 0;
      } else {
        one->bx = atof(token);
      }
    } else if (i == 7) {
      if (0 == strncmp(token,"NaN", 3)) {
        //printf ("parse_shift_line got NaN\n");
        //return -1;
        one->by = 0;
      } else {
        one->by = atof(token);
      }
    } else if (i == 8) {
      if (0 == strncmp(token,"NaN", 3)) {
        //printf ("parse_shift_line got NaN\n");
        //return -1;
        one->bz = 0;
      } else {
        one->bz = atof(token);
      }
    } else if (i == 9) {
      if (0 == strncmp(token,"00", 2)) one->flag = "00";
      else if (0 == strncmp(token,"30", 2)) one->flag = "00";
      else if (0 == strncmp(token,"05", 2)) one->flag = "05";
      else if (0 == strncmp(token,"08", 2)) one->flag = "08"; 
      else if (0 == strncmp(token,"15", 2)) one->flag = "15"; 
      else if (0 == strncmp(token,"11", 2)) one->flag = "11"; 
      else if (strlen(token) == 2) { printf("parse config data, error flag \"%s\"\n", token); return -1; }
      else if (0 == strncmp(token,"0", 1)) one->flag = "00";
      else if (0 == strncmp(token,"2", 1)) one->flag = "02";
      else if (0 == strncmp(token,"5", 1)) one->flag = "05";
      else if (0 == strncmp(token,"8", 1)) one->flag = "08";
      else { printf("parse config data, error flag %s\n", token); return -1; }
    }
  }
  if (i < 9) return -1;
  return 0;
}

int load_one_shift_file(char* filename, int last_got) {
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) return 0;
  char * line = NULL;
  size_t len = 0;
  int got = 0;
  int nread;
  while ((nread = getline(&line, &len, fp)) != -1) {
    if (line[0] == '#') continue;
    int rr = parse_shift_line(line, last_got);
    if (rr == 1) break;
    if (rr == 0) got++;
  }
  free(line);
  fclose(fp);
  return got;
}

int load_shift_data() {
  if (shift_sort_arr == NULL) return 0;
  int size = filter_end_s - filter_start_s + 1;

  int start_i = -1;
  int end_i = -1;
  int i = 0;
  for(i = 0 ; i < shift_sort_arr_cnt ; i ++) {
    struct sort_t * one = shift_sort_arr + i;
    //printf ("%s: %lu ~ %lu\n", one->filename, one->time, one->end_time);
    if (one->time <= filter_start_s && one->end_time >= filter_start_s) {
      start_i = i;
      //printf ("load_shift_data from %s ", one->filename);
    }
    if (one->time <= filter_end_s && one->end_time >= filter_end_s) {
      end_i = i;
      //printf ("~ %s\n", one->filename);
    }
  }

  /*
  if (start_i == -1 && end_i == -1) {
    printf ("convert2b.so: config data not found\n");
    return 3;
  }
  */
  if (start_i == -1) {
    printf ("convert2b.so: config data lack seconds\n");
    start_i = 0;
  }
  if (end_i == -1) {
    printf ("convert2b.so: config data lack seconds\n");
    end_i = shift_sort_arr_cnt - 1;
  }

  if (proccess_hz == 1) {
    shift_data = malloc(sizeof(struct one_data) * size);
    if (shift_data == NULL) return 4;
    memset(shift_data, 0, sizeof(struct one_data) * size);
  }
  if (proccess_hz == 32) {
    shift_data = malloc(sizeof(struct shift_32_data) * size);
    if (shift_data == NULL) return 4;
    memset(shift_data, 0, sizeof(struct shift_32_data) * size);
  } 
  
  int last_got = 0;
  for(i = start_i ; i <= end_i ; i ++) {
    struct sort_t * one = shift_sort_arr + i;
    last_got = load_one_shift_file(one->filename, last_got);
  }
  shift_data_len = size;
  return 0;
}

// 0 = 全部处理完成 , 1 = 未完成
int filter(struct more_data * first) {
  double coeff[] = {0, -5710215, 0, 28236580, 0, -85425417, 0, 331398102, 536743724, 331398102, 0, -85425417, 0, 28236580, 0, -5710215, 0};
  int i = 0;
  for(i = 0; i < 17; i ++) coeff[i] = coeff[i] / 1073741823;

  double fx[17];
  double fy[17];
  double fz[17];
  int found = 0;
  int last_spl = 0;

  struct more_data * next = first;
  while(next != NULL) {

    int new_index = 0;
    struct more_data * current = next;

    if (last_spl != current->raw_sample) {
      int i = 0;
      for(i = 0; i < 17; i ++) {
        fx[i] = current->buf[0].bx;
        fy[i] = current->buf[0].by;
        fz[i] = current->buf[0].bz;
      }
    }

    last_spl = current->raw_sample;

    if (current->now_sample == 1) {
      next = next->next;
      continue;
    }

    found = 1;
    int filter_index = 0;
    for(filter_index = 0 ; filter_index < current->data_len; filter_index ++) {
      if (filter_index % 2 == 0) continue;

      fx[0] = current->buf[filter_index].bx;
      fy[0] = current->buf[filter_index].by;
      fz[0] = current->buf[filter_index].bz;

      int mi = 0;
      for(mi = 0; mi < 16; mi++) fx[16-mi] = fx[15-mi];
      for(mi = 0; mi < 16; mi++) fy[16-mi] = fy[15-mi];
      for(mi = 0; mi < 16; mi++) fz[16-mi] = fz[15-mi];

      double outx = 0;
      double outy = 0;
      double outz = 0;
      for(mi = 0; mi < 17; mi++) outx = outx + fx[mi] * coeff[mi];
      for(mi = 0; mi < 17; mi++) outy = outy + fy[mi] * coeff[mi];
      for(mi = 0; mi < 17; mi++) outz = outz + fz[mi] * coeff[mi];

      current->buf[new_index].time = current->buf[filter_index].time;
      current->buf[new_index].bx = outx;
      current->buf[new_index].by = outy;
      current->buf[new_index].bz = outz;
      new_index ++;
    }
    current->data_len = new_index;
    current->now_sample = current->now_sample / 2;

    next = next->next;
  }

  return found;
}

// 读取shift文件第一行的时间
uint64_t read_first_time_shift(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    printf ("convert2b.so: read_first_time_shift open \"%s\"\n", filename);
    return 0;
  }

  char * line = NULL;
  size_t len = 0;
  int nread;

  while ((nread = getline(&line, &len, fp)) != -1) {
    if (line[0] != '#') break;
  }

  fclose(fp);

  if (nread < 23) return 0;

  char time_line[30] = {0};
  snprintf (time_line, 28, "%s", line);
  free(line);

  //printf ("read_first_time_shift: %s\n", time_line);
  if (proccess_hz == 32) return string_to_ms(time_line);
  return string_to_s(time_line);
}

// 读取shift文件最后一行的时间
uint64_t read_last_time_shift(char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) return 0;
  const int max_len = 150;
  const int max_len32 = 75;
  char buf[256] = {0};
  int len = 0;
  if (proccess_hz == 1) {
    fseek(fp, -1 * max_len, SEEK_END);
    len = fread(buf, 1, max_len, fp);
  }
  if (proccess_hz == 32) {
    fseek(fp, -1 * max_len32, SEEK_END);
    len = fread(buf, 1, max_len32, fp);
  }
  fclose(fp);
  buf[len] = '\0';
  if (buf[len-1] == '\n') buf[len-1] = '\0';
  char *last_newline = strrchr(buf, '\n');
  char *last_line = last_newline+1;
  if (last_newline == NULL) last_line = buf;
  //printf ("read_last_time_shift: %s\n", last_line);
  if (proccess_hz == 32) return string_to_ms(last_line);
  return string_to_s(last_line);
}

// 读取2B文件第一行的时间
uint64_t read_first_time_2B(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    printf ("convert2b.so: read_first_time_2B open %s error\n", filename);
    return 0;
  }

  char * line = NULL;
  size_t len = 0;
  int nread;

  while ((nread = getline(&line, &len, fp)) != -1) {
    if (line[0] != '#') break;
  }

  fclose(fp);

  if (nread < 27) return 0;

  char time_line[30] = {0};
  snprintf (time_line, 28, "%s", line);
  free(line);

  //printf("read_first_time_2B %s\n", time_line);
  uint64_t t = string_to_ms(time_line);
  return t;
}

// 读取output文件最后一行的时间
uint64_t read_last_time_output(char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) return 0;
  const int max_len = 70;
  char buf[max_len + 1];
  fseek(fp, -1 * max_len, SEEK_END);
  int len = fread(buf, 1, max_len, fp);
  fclose(fp);
  //printf ("read len=%d\n", len);
  buf[len] = '\0';
  if (buf[len-1] == '\n') buf[len-1] = '\0';
  //printf ("%s\n", buf);
  char *last_newline = strrchr(buf, '\n');
  char *last_line = last_newline+1;
  if (last_newline == NULL) last_line = buf;
  //printf ("read_last_time_output: %s\n", last_line);
  return string_to_ms(last_line);
}

// HX1-Or_GRAS_MOMAG-IP_SCI_P_20211201071807_20211201142247_00634_A.2B
void gen_out_prefix(char* line) {
  memset(out_prefix1, 0, sizeof(out_prefix1));
  memset(out_prefix32, 0, sizeof(out_prefix32));
  char mline[512] = {0};
  snprintf(mline, sizeof(mline), "%s", line);
  char *token, *sptr, *str;
  int i = 0;
  for (str = mline; ; str = NULL) {
    token = strtok_r(str, "_", &sptr);
    if (token == NULL) break;
    if (0 == strncmp(token,"20", 2)) {
        i++;
        continue;
    }
    if (i == 2) {
        sprintf(out_prefix1, "%s", token);
        break;
    }
  }

  if (strlen(out_prefix1) == 0) {
    printf("convert2b.so: gen output filename error\n");
    sprintf(out_prefix32, "%s", "00000_2Ctemp-32Hz.txt");
    sprintf(out_prefix1, "%s", "00000_2Ctemp-1Hz.txt");
  } else {
    sprintf(out_prefix32, "%s", out_prefix1);
    sprintf(out_prefix32 + strlen(out_prefix32), "_%s", "2Ctemp-32Hz.txt");
    sprintf(out_prefix1 + strlen(out_prefix1), "_%s", "2Ctemp-1Hz.txt");
  }
}

double round_wrap(double d) {
    return round(d*1000)/1000.0;
    /*
    if (d == 0) return 0;
    if (d > 0) return ((int)(d * 1000 + 0.5)) / 1000.0;
    else {
        double c = ((int)(d * 1000 - 0.5)) / 1000.0;
	if (c == 0) return 0;
	return c;
    }
    */
}

void write_line(FILE* fp, struct one_data * one) {
  // 和 shift data 相同秒的值 相减
  struct one_data * shift_one = NULL;
  if (shift_data) {
    uint64_t sec = one->time / 1000000;
    int index = sec - filter_start_s;
    if (index >= 0 && index < shift_data_len) {
      shift_one = shift_data + index;
      if (shift_one->time == sec) {
        one->bx -= shift_one->bx;
        one->by -= shift_one->by;
        one->bz -= shift_one->bz;
      }
    }
  }
  char buf[128] = {0};
  ms_to_string(one->time, buf);
  if (shift_one != NULL && shift_one->flag != NULL) {
    if (0 == strncmp(shift_one->flag, "02", 2)) fprintf(fp, "%s NaN NaN NaN 02\n", buf);
    else if (0 == strncmp(shift_one->flag, "05", 2)) fprintf(fp, "%s NaN NaN NaN 05\n", buf);
    else if (0 == strncmp(shift_one->flag, "15", 2)) fprintf(fp, "%s NaN NaN NaN 15\n", buf);
    else fprintf(fp, "%s %.3f %.3f %.3f %s\n", buf, round_wrap(one->bx), round_wrap(one->by), round_wrap(one->bz), shift_one->flag);
  } else {
    fprintf(fp, "%s %.3f %.3f %.3f 06\n", buf, round_wrap(one->bx), round_wrap(one->by), round_wrap(one->bz));
  }
}

void write_line_noby(FILE* fp, struct one_data * one) {
  char buf[128] = {0};
#ifdef SUB14
  one->time = one->time - 14 * 1000000;
#endif
  ms_to_string(one->time, buf);
  if (0 == strncmp(one->flag, "11", 2)) return ;
  if (proccess_hz == 1) {
    if (0 == strncmp(one->flag, "02", 2)) fprintf(fp, "%s NaN NaN NaN 02\n", buf);
    else if (0 == strncmp(one->flag, "05", 2)) fprintf(fp, "%s NaN NaN NaN 05\n", buf);
    else if (0 == strncmp(one->flag, "15", 2)) fprintf(fp, "%s NaN NaN NaN 15\n", buf);
    else if (0 == strncmp(one->flag, "XX", 2)) fprintf(fp, "%s %.3f %.3f %.3f 06\n", buf, round_wrap(one->bx), round_wrap(one->by), round_wrap(one->bz));
    else fprintf(fp, "%s %.3f %.3f %.3f %s\n", buf, round_wrap(one->bx), round_wrap(one->by), round_wrap(one->bz), one->flag);
  }
  if (proccess_hz == 32) {
    if (0 == strncmp(one->flag, "02", 2)) fprintf(fp, "%s 32 NaN NaN NaN 02\n", buf);
    else if (0 == strncmp(one->flag, "05", 2)) fprintf(fp, "%s 32 NaN NaN NaN 05\n", buf);
    else if (0 == strncmp(one->flag, "15", 2)) fprintf(fp, "%s 32 NaN NaN NaN 15\n", buf);
    else if (0 == strncmp(one->flag, "XX", 2)) fprintf(fp, "%s 32 %.3f %.3f %.3f 06\n", buf, round_wrap(one->bx), round_wrap(one->by), round_wrap(one->bz));
    else fprintf(fp, "%s 32 %.3f %.3f %.3f %s\n", buf, round_wrap(one->bx), round_wrap(one->by), round_wrap(one->bz), one->flag);
  }
}

void write_line_nan(FILE* fp, uint64_t ms) {
  char buf[128] = {0};
#ifdef SUB14
  ms = ms - 14 * 1000000;
#endif
  ms_to_string(ms, buf);
  if (proccess_hz == 1) fprintf(fp, "%s NaN NaN NaN 15\n", buf);
  if (proccess_hz == 32) fprintf(fp, "%s 32 NaN NaN NaN 15\n", buf);
}

void print_32_area(uint64_t t1, uint64_t t2) {
  char buf1[128] = {0};
  char buf2[128] = {0};
  ms_to_string(t1, buf1);
  ms_to_string(t2, buf2);
  printf (" area %s ~ %s\n", buf1, buf2);
}

void print_delta(uint64_t t1, uint64_t t2, int type) {
  char buf1[128] = {0};
  char buf2[128] = {0};
  ms_to_string(t1, buf1);
  ms_to_string(t2, buf2);
  if (type == 1) printf (" drop area: %s ~ %s\n", buf1, buf2);
  else printf (" insert NaN: %s ~ %s\n", buf1, buf2);
}

void arrage_time(struct more_data * first) {
  //struct more_data * tail = first;
  //while(tail->next != NULL) tail = tail->next;
  
  int exit_flag = 0;
  uint64_t start_time = 0;
  uint64_t end_time = 0;
  uint64_t last_t = 0;
  int data_count = 0;
  struct more_data * block_last = NULL;
  int block_last_i = 0;
  struct more_data * next = first;
  int i = 0;
  while(exit_flag != 1) {
    start_time = 0;
    end_time = 0;
    last_t = 0;
    data_count = 0;
    block_last = NULL;
    block_last_i = 0;
    next = first;
    while(next != NULL) {
      for(i = 0; i < next->data_len; i ++) {
        struct one_data * one = next->buf + i;
        if (one->arrage == 1) continue;
        data_count ++;
        if (start_time == 0) {
	  last_t = one->time;
          start_time = one->time;
	  //if (next->raw_sample == 32) start_time = start_time - 500000;
          continue;
        }
        int64_t delta = one->time - last_t;
        if (delta > 3*1000000) {
	  /*
          char buf1[128] = {0};
          char buf2[128] = {0};
          ms_to_string(one->time, buf1);
          ms_to_string(last_t, buf2);
          printf("> 3s, %s ~ %s, gap=%lu\n", buf1, buf2);
	  */
          end_time = last_t;
	  data_count --;
          block_last = next;
          block_last_i = i;
	  if (i == 0) {
            block_last = next->prev;
            block_last_i = next->prev->data_len - 1;
	  }
          break;
        }
        last_t = one->time;
      }
      if (block_last != NULL) break;
      if (next->next == NULL) {
        block_last = next;
        block_last_i = next->data_len - 1;
        end_time = last_t;
        exit_flag = 1;
      }
      next = next->next;
    }
    // arrage
    if (data_count == 1) data_count = 2;
    uint64_t gap = (end_time - start_time) / (data_count - 1);
    /*
    char buf1[128] = {0};
    char buf2[128] = {0};
    ms_to_string(start_time, buf1);
    ms_to_string(end_time, buf2);
    printf("arrage, data count=%d, %s ~ %s, gap=%lu\n", data_count, buf1, buf2, gap);
    */
    if ((1000000 - gap) < 20) gap = 1000000;
    int arrage_count = 0 ;
    while(arrage_count <= data_count) {
      for(i = block_last_i; i >= 0; i --) {
        struct one_data * one = block_last->buf + i;
	one->arrage = 1;
        if (arrage_count > 0) {
          one->time = end_time - gap * arrage_count;
	  /*
	  if (arrage_count > (data_count - 10)) {
            char buf3[128] = {0};
            ms_to_string(one->time, buf3);
            printf ("%s\n", buf3);
	  }
	  */
	}
        arrage_count ++;
	if (arrage_count > data_count) break;
      }
      if (arrage_count > data_count) break;
      block_last = block_last->prev;
      if (block_last) block_last_i = block_last->data_len - 1;
      else break;
    }
  }
}

void by_config(struct more_data * first) {
  if (shift_data == NULL) return;
  struct more_data * next = first;
  struct one_data * shift_one = NULL;
  int i = 0;
  while(next != NULL) {
    for(i = 0; i < next->data_len; i ++) {
      struct one_data * one = next->buf + i;
      if (0 != strncmp(one->flag, "XX", 2)) continue;
      uint64_t sec = one->time / 1000000;
      int index = sec - filter_start_s;
      if (index >= 0 && index < shift_data_len) {
        shift_one = shift_data + index;
        if (shift_one->time == sec) {
          one->bx -= shift_one->bx;
          one->by -= shift_one->by;
          one->bz -= shift_one->bz;
	        one->flag = shift_one->flag;
        }
      }
    }
    next = next->next;
  }
}

struct shift_one_data * find_shift_one(struct shift_32_data * shift_one32, ms) {
  int i = 0;
  for(i = 0; i < shift_one32->data_len; i++) {
    if (shift_one32->buf[i].time == ms) return (shift_one32->buf[i]);
  }
  return null;
}

void by_config_32(struct more_data * first) {
  if (shift_data == NULL) return;
  struct more_data * next = first;
  struct shift_32_data * shift_one32 = NULL;
  struct shift_one_data * shift_one = NULL;
  int i = 0;
  while(next != NULL) {
    if (next->raw_sample != 32) {
      next = next->next;
      continue;
    }
    for(i = 0; i < next->data_len; i ++) {
      struct one_data * one = next->buf + i;
      if (0 != strncmp(one->flag, "XX", 2)) continue;
      uint64_t sec = one->time / 1000000;
      int index = sec - filter_start_s;
      if (index >= 0 && index < shift_data_len) {
        shift_one32 = shift_data + index;
        if (shift_one32->sec == sec) {
          shift_one = find_shift_one(shift_one32, one->time);
          if (shift_one) {
            one->bx -= shift_one->bx;
            one->by -= shift_one->by;
            one->bz -= shift_one->bz;
            one->flag = shift_one->flag;
          }
        }
      }
    }
    next = next->next;
  }
}

int save_file(struct more_data * first, char* output_dir, char* fix_filename) {
  struct more_data * next = first;
  uint64_t last_t = next->buf[0].time - 1000000;
  int last_day = -1;
  char daystr[128] = {0};
  char filename[500] = {0};
  FILE *fp = NULL;

  if (fix_filename) {
    sprintf(filename, "%s/%s", output_dir, fix_filename);
    fp = fopen(filename, "w");
    if (fp == NULL) {
      printf ("convert2b.so: open file: %s error\n", filename);
      return 1;
    }
    printf ("convert2b.so: output file %s\n", filename);
  } else {
    ms_to_date(next->buf[0].time, daystr);
    sprintf(filename, "%s/%s-1hz-out.txt", output_dir, daystr);
    uint64_t file_last_t = read_last_time_output(filename);
    if (file_last_t > 0) {
      last_day = ms_to_day(file_last_t);
      last_t = file_last_t;
      fp = fopen(filename, "a+");
      if (fp == NULL) {
        printf ("convert2b.so: open file: %s error\n", filename);
        return 1;
      }
      char timestr[128] = {0};
      ms_to_string(file_last_t, timestr);
      printf ("convert2b.so: %s exsit, append at %s\n", filename, timestr);
    }
  }

  int count = 0 ;
  uint64_t delta_flag_1 = 0;
  int i = 0;
  while(next != NULL) {
    for(i = 0; i < next->data_len; i ++) {
      struct one_data * one = next->buf + i;
      uint64_t now_t = one->time;

      if (fix_filename == NULL) {
        int day = ms_to_day(now_t);
        if (day != last_day) {
          if (fp) fclose(fp);
          ms_to_date(now_t, daystr);
          sprintf(filename, "%s/%s-1hz-out.txt", output_dir, daystr);
          fp = fopen(filename, "w");
          if (fp == NULL) {
            printf ("convert2b.so: open file: %s error\n", filename);
            return 1;
          }
          printf ("convert2b.so: output file %s\n", filename);
          last_day = day;
        }
      }

      int64_t delta = now_t - last_t;
      if (delta < 300000) {
        if (delta_flag_1 == 0) delta_flag_1 = last_t;
        continue;  // 小于0.3秒，丢弃
      }
      if (delta_flag_1 > 0) {
        print_delta(delta_flag_1, last_t, 1);
        delta_flag_1 = 0;
      }

      if (delta > 10 * 1000000) {
        char eebuf[128] = {0};
        ms_to_string(now_t, eebuf);
        printf ("convert2b.so: skip error line time = %s\n", eebuf);
      }

      if (delta > 1600000 && delta < 10 * 1000000) { // 大于1秒，插入 NaN
        delta = delta - 200000;
	int di = 0;
        for(di = 1000000; di < delta; di += 1000000) {
          write_line_nan(fp, last_t + di);
        }
        print_delta(last_t, now_t, 2);
      }

      write_line_noby(fp, one);
      //write_line(fp, one);
      last_t = now_t;
      count ++;
    }
    next = next->next;
  }

#ifdef SUB14
  for(i = 1 ; i <= 14; i++) {
    write_line_nan(fp, last_t + i * 1000000);
  }
#endif

  if (fp) fclose(fp);
  return 0;
}

int save_file_32(struct more_data * first, char* output_dir, char* fix_filename) {
  struct more_data * next = first;
  uint64_t last_t = next->buf[0].time - 1000000;
  int last_day = -1;
  char daystr[128] = {0};
  char filename[500] = {0};
  FILE *fp = NULL;

  if (fix_filename) {
    sprintf(filename, "%s/%s", output_dir, fix_filename);
    fp = fopen(filename, "w");
    if (fp == NULL) {
      printf ("convert2b.so: open file: %s error\n", filename);
      return 1;
    }
    printf ("convert2b.so: output file %s\n", filename);
  } else {
    ms_to_date(next->buf[0].time, daystr);
    sprintf(filename, "%s/%s-32hz-out.txt", output_dir, daystr);
    uint64_t file_last_t = read_last_time_output(filename);
    if (file_last_t > 0) {
      last_day = ms_to_day(file_last_t);
      last_t = file_last_t;
      fp = fopen(filename, "a+");
      if (fp == NULL) {
        printf ("convert2b.so: open file: %s error\n", filename);
        return 1;
      }
      char timestr[128] = {0};
      ms_to_string(file_last_t, timestr);
      printf ("convert2b.so: %s exsit, append at %s\n", filename, timestr);
    }
  }

  uint64_t start_t = 0;

  while(next != NULL) {
    if (next->raw_sample != 32) {
      next = next->next;
      continue;
    }
    int i = 0;
    for(i = 0; i < next->data_len; i ++) {
      struct one_data * one = next->buf + i;
      uint64_t now_t = one->time;

      if (fix_filename == NULL) {
        int day = ms_to_day(now_t);
        if (day != last_day) {
          if (fp) fclose(fp);
          ms_to_date(now_t, daystr);
          sprintf(filename, "%s/%s-32hz-out.txt", output_dir, daystr);
          fp = fopen(filename, "w");
          if (fp == NULL) {
            printf ("convert2b.so: open file: %s error\n", filename);
            return 1;
          }
          printf ("convert2b.so: output file %s\n", filename);
          last_day = day;
        }
      }

      if (start_t == 0) start_t = now_t;
      int64_t delta = now_t - last_t;
      if (delta > 10 * 1000000) {
	      print_32_area(start_t, last_t);
	      start_t = now_t;
      }

      write_line_noby(fp, one);
      //write_line(fp, one);
      last_t = now_t;
    }
    next = next->next;
  }

  if (start_t > 0) print_32_area(start_t, last_t);
  if (fp) fclose(fp);
  return 0;
}


int convert_one_32(char** files, int files_cnt, char* output_dir, char* fix_filename) {
  filter_start_s = 0;
  filter_end_s = 0;

  struct more_data * all = load_files(files, files_cnt);

  int size = filter_end_s - filter_start_s + 1;
  printf ("convert2b.so: %lu ~ %lu, size: %d second\n", filter_start_s,filter_end_s, size);

  int serr = load_shift_data();
  if (serr != 0) return serr;

  by_config_32(all);
  int sr = save_file_32(all, output_dir, fix_filename);
  if (sr != 0) printf("convert2b.so: output file error\n");

  free_all(all);

  if (shift_data) {
    free(shift_data);
    shift_data = NULL;
    shift_data_len = 0;
  }

  return 0;
}


int convert_one(char** files, int files_cnt, char* output_dir, char* fix_filename) {
  filter_start_s = 0;
  filter_end_s = 0;

  struct more_data * all = load_files(files, files_cnt);

  int size = filter_end_s - filter_start_s + 1;
  printf ("convert2b.so: %lu ~ %lu, size: %d second\n", filter_start_s,filter_end_s, size);
  int i = 0;
  for(i = 0 ; i < 7; i ++) {
    int r = filter(all);
    if (r == 0) break;
    //printf("filter %d\n", i);
  }

  int serr = load_shift_data();
  if (serr != 0) return serr;

  by_config(all);
  arrage_time(all);
  int sr = save_file(all, output_dir, fix_filename);
  if (sr != 0) printf("convert2b.so: output file error\n");

  free_all(all);

  if (shift_data) {
    free(shift_data);
    shift_data = NULL;
    shift_data_len = 0;
  }

  return 0;
}

int cmpfunc (const void * a, const void * b) {
  uint64_t t1 = ((struct sort_t *)a)->time;
  uint64_t t2 = ((struct sort_t *)b)->time;
  if (t1 > t2) return 1;
  if (t1 == t2) return 0;
  return -1;
}

void test_shift(char** shift_files, int shift_files_cnt) {
  shift_sort_arr = malloc(sizeof(struct sort_t) * shift_files_cnt);
  memset(shift_sort_arr, 0, sizeof(struct sort_t)* shift_files_cnt);
  int i = 0;
  for(i = 0; i < shift_files_cnt; i ++) {
    snprintf(shift_sort_arr[i].filename, 499, "%s", shift_files[i]);
    shift_sort_arr[i].time = read_first_time_shift(shift_files[i]);
    shift_sort_arr[i].end_time = read_last_time_shift(shift_files[i]);
  }
  qsort(shift_sort_arr, shift_files_cnt, sizeof(struct sort_t), cmpfunc);
  shift_sort_arr_cnt = shift_files_cnt;
}

int MoMAG_magdata_l2c(struct Input_info *input) {
  proccess_hz = 1;

  // 1. test op/ip file
  uint64_t t1 = read_first_time_2B(input->ip2Bfile);
  if (t1 == 0) {
    return 1;
  }
  t1 = read_first_time_2B(input->op2Bfile);
  if (t1 == 0) {
    return 1;
  }

  // 2. test config file
  int shift_files_cnt = 1;
  shift_sort_arr_cnt = 1;
  shift_sort_arr = malloc(sizeof(struct sort_t) * shift_files_cnt);
  memset(shift_sort_arr, 0, sizeof(struct sort_t)* shift_files_cnt);
  snprintf(shift_sort_arr[0].filename, 512, "%s", input->config_parafile);
  shift_sort_arr[0].time = read_first_time_shift(input->config_parafile);
  shift_sort_arr[0].end_time = read_last_time_shift(input->config_parafile);
  if (shift_sort_arr[0].time == 0 || shift_sort_arr[0].end_time == 0) {
    return 2;
  }

  // 3. filter
  char* filename[] = {input->op2Bfile, input->ip2Bfile};
  gen_out_prefix(input->op2Bfile);
  return convert_one(filename, 1, input->outputpath, out_prefix1);
}

int MoMAG_magdata_l2c_32(struct Input_info *input) {
  proccess_hz = 32;

  // 1. test op/ip file
  uint64_t t1 = read_first_time_2B(input->ip2Bfile);
  if (t1 == 0) {
    return 1;
  }
  t1 = read_first_time_2B(input->op2Bfile);
  if (t1 == 0) {
    return 1;
  }

  // 2. test config file
  int shift_files_cnt = 1;
  shift_sort_arr_cnt = 1;
  shift_sort_arr = malloc(sizeof(struct sort_t) * shift_files_cnt);
  memset(shift_sort_arr, 0, sizeof(struct sort_t)* shift_files_cnt);
  snprintf(shift_sort_arr[0].filename, 512, "%s", input->config_parafile);
  shift_sort_arr[0].time = read_first_time_shift(input->config_parafile);
  shift_sort_arr[0].end_time = read_last_time_shift(input->config_parafile);
  if (shift_sort_arr[0].time == 0 || shift_sort_arr[0].end_time == 0) {
    return 2;
  }

  // 3. filter
  char* filename[] = {input->op2Bfile, input->ip2Bfile};
  gen_out_prefix(input->op2Bfile);
  return convert_one_32(filename, 1, input->outputpath, out_prefix32);
}

int MoMAG_magdata_l2c_test(struct Input_info *input) {
  proccess_hz = 1;

  // 1. test op/ip file
  uint64_t t1 = read_first_time_2B(input->ip2Bfile);
  if (t1 == 0) {
    printf("ip file error\n");
    return 1;
  }
  t1 = read_first_time_2B(input->op2Bfile);
  if (t1 == 0) {
    printf("op file error\n");
    return 1;
  }

  // 3. filter
  char* filename[] = {input->op2Bfile, input->ip2Bfile};
  gen_out_prefix(basename(input->op2Bfile));
  return convert_one(filename, 1, input->outputpath, out_prefix1);
}

int MoMAG_magdata_l2c_32_test(struct Input_info *input) {
  proccess_hz = 32;

  // 1. test op/ip file
  uint64_t t1 = read_first_time_2B(input->ip2Bfile);
  if (t1 == 0) {
    printf("ip file error\n");
    return 1;
  }
  t1 = read_first_time_2B(input->op2Bfile);
  if (t1 == 0) {
    printf("op file error\n");
    return 1;
  }

  // 3. filter
  char* filename[] = {input->op2Bfile, input->ip2Bfile};
  gen_out_prefix(basename(input->op2Bfile));
  return convert_one_32(filename, 1, input->outputpath, out_prefix32);
}
