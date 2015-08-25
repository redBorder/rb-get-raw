#ifndef UTIL_H
#define UTIL_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <udns.h>
#include <time.h>

#define BLOCK_SIZE 4096

struct event_t {
	char * str;
	size_t size;
	size_t length;
};

struct dns_cache_t {
	char ip[16];
	char name[256];
	int err;
	struct dns_cache_t * next;
};

void event_putc (struct event_t * event, char c);
void event_puts (struct event_t * event, char * s, size_t len);
void add_key (struct event_t * event, char * new_key, size_t len,
              int first_key);
void add_number (struct event_t * event, char * new_number,
                 size_t len);
void add_string (struct event_t * event, char * new_string, size_t len);
void add_null (struct event_t * event);
int rdns (char * string_val, char * host);
void add_cache (char * ip, char * name);
char * get_cache (char * ip);
#endif