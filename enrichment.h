#ifndef ENRICHMENT_H
#define ENRICHMENT_H
#include <yajl/yajl_tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "util.h"

#endif

int load_file (char * enrich_filename);
int load_output_file (char * output_filename);
void enrich_free();
void process (char * event, int resolve_names, time_t timestamp,
              int _expand_events);
void end_process();
void close_file();
void add_enrich (const char * keyVal,
                 char * valueVal);
