#ifndef ENRICHMENT_H
#define ENRICHMENT_H
#include <yajl/yajl_tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#endif

int load_file ();
void enrich_free();
void process (char * keyVal,
              size_t keyLen,
              char * valueVal,
              size_t valueLen,
              int is_fist_key,
              int type);
void end_process();
void close_output();