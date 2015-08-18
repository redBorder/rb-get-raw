/*
 * Copyright (c) 2007-2014, Lloyd Hilaiel <me@lloyd.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct event {
	char * str;
	size_t size;
	size_t length;
};

static struct event current_event = {NULL, 0, 0};
static int s_streamReformat = 0;
static uint on_event = 0;
static uint first_key = 1;
static FILE * output = NULL;

static void new_event () {
	on_event = 1;
	first_key = 1;
}

static void end_event() {
	fprintf (output, "%s\n", current_event);
	//printf ("%s\n", current_event);
	//memset (current_event, '\0', sizeof (current_event));
	current_event[0] = '\0';
	on_event = 0;
}

static void add_key (char * new_key, size_t len) {
	if (first_key) {
		first_key = 0;
	} else {
		strncat (current_event, ",", strlen (","));
	}
	strncat (current_event, "\"", strlen ("\""));
	strncat (current_event, new_key, len);
	strncat (current_event, "\":", strlen ("\":"));
}

static void add_number (char * new_number, size_t len) {
	strncat (current_event, new_number, len);
}

static void add_string (char * new_string, size_t len) {
	strncat (current_event, "\"", strlen ("\""));
	strncat (current_event, new_string, len);
	strncat (current_event, "\"", strlen ("\""));
}

static void add_null () {
	strncat (current_event, "null", strlen ("null"));
}

////////////////////////////////////////////////////////////////////////////////

#define GEN_AND_RETURN(func)                                          \
  {                                                                   \
    yajl_gen_status __stat = func;                                    \
    if (__stat == yajl_gen_generation_complete && s_streamReformat) { \
      yajl_gen_reset(g, "\n");                                        \
      __stat = func;                                                  \
    }                                                                 \
    return __stat == yajl_gen_status_ok; }
static int reformat_null (void * ctx) {
	yajl_gen g = (yajl_gen) ctx;

	if (on_event) {
		add_null();
	}

	GEN_AND_RETURN (yajl_gen_null (g));
}
static int reformat_boolean (void * ctx, int boolean) {
	yajl_gen g = (yajl_gen) ctx;
	GEN_AND_RETURN (yajl_gen_bool (g, boolean));
}
static int reformat_number (void * ctx, const char * s, size_t l) {
	yajl_gen g = (yajl_gen) ctx;

	if (on_event) {
		add_number ((char *) s, l);
	}

	GEN_AND_RETURN (yajl_gen_number (g, s, l));
}
static int reformat_string (void * ctx, const unsigned char * stringVal,
                            size_t stringLen) {
	yajl_gen g = (yajl_gen) ctx;

	if (on_event) {
		add_string ((char *) stringVal, stringLen);
	}

	GEN_AND_RETURN (yajl_gen_string (g, stringVal, stringLen));
}
static int reformat_map_key (void * ctx, const unsigned char * stringVal,
                             size_t stringLen) {
	yajl_gen g = (yajl_gen) ctx;
	if (on_event) {
		add_key ((char *)stringVal, stringLen);
	} else if (!strncmp ((const char *)stringVal, "event", strlen ("event"))) {
		new_event();
	}

	GEN_AND_RETURN (yajl_gen_string (g, stringVal, stringLen));
}
static int reformat_start_map (void * ctx) {
	yajl_gen g = (yajl_gen) ctx;
	GEN_AND_RETURN (yajl_gen_map_open (g));
}
static int reformat_end_map (void * ctx) {
	yajl_gen g = (yajl_gen) ctx;

	// README This function assumes that "event" object doesn't contains nested
	// objects
	if (on_event) {
		end_event();
	}

	GEN_AND_RETURN (yajl_gen_map_close (g));
}
static int reformat_start_array (void * ctx) {
	yajl_gen g = (yajl_gen) ctx;
	GEN_AND_RETURN (yajl_gen_array_open (g));
}
static int reformat_end_array (void * ctx) {
	yajl_gen g = (yajl_gen) ctx;
	GEN_AND_RETURN (yajl_gen_array_close (g));
}
static yajl_callbacks callbacks = {
	reformat_null,
	reformat_boolean,
	NULL,
	NULL,
	reformat_number,
	reformat_string,
	reformat_start_map,
	reformat_map_key,
	reformat_end_map,
	reformat_start_array,
	reformat_end_array
};

////////////////////////////////////////////////////////////////////////////////

int main () {
	yajl_handle hand;
	static unsigned char fileData[65536];
	/* generator config */
	yajl_gen g;
	yajl_status stat;
	size_t rd;
	int retval = 0;
	g = yajl_gen_alloc (NULL);
	yajl_gen_config (g, yajl_gen_beautify, 1);
	yajl_gen_config (g, yajl_gen_validate_utf8, 1);
	FILE * input = NULL;

	/* ok.  open file.  let's read and parse */
	if (! (input = fopen ("input.json", "r"))) {
		printf ("No se puede abrir fichero de entrada\n");
		exit (1);
	} else {
		printf ("Abierto fichero de entrada\n");
	}

	if (! (output = fopen ("output", "w"))) {
		printf ("No se puede abrir fichero de salida\n");
		exit (1);
	} else {
		printf ("Abierto fichero de salida\n");
	}

	hand = yajl_alloc (&callbacks, NULL, (void *) g);

	// /* and let's allow comments by default */
	yajl_config (hand, yajl_allow_comments, 1);

	for (;;) {
		rd = fread ((void *) fileData, 1, sizeof (fileData) - 1, input);
		if (rd == 0) {
			if (!feof (input)) {
				fprintf (stderr, "error on file read.\n");
				retval = 1;
			}
			break;
		}
		fileData[rd] = 0;
		stat = yajl_parse (hand, fileData, rd);
		if (stat != yajl_status_ok) break;
		{
			const unsigned char * buf;
			size_t len;
			yajl_gen_get_buf (g, &buf, &len);
			yajl_gen_clear (g);
		}
	}
	stat = yajl_complete_parse (hand);
	if (stat != yajl_status_ok) {
		unsigned char * str = yajl_get_error (hand, 1, fileData, rd);
		fprintf (stderr, "%s", (const char *) str);
		yajl_free_error (hand, str);
		retval = 1;
	}
	yajl_gen_free (g);
	yajl_free (hand);
	fclose (output);
	fclose (input);
	return retval;
}