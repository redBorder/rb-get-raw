#include "enrichment.h"
#include <librd/rd.h>
#include <librd/rdfile.h>

static int enrich = 0;
static int times = 1;
static time_t timestamp = 0;
static yajl_val node;
char host_name[128] = "";

struct keyval_t {
	int is_first_key;
	int type;
	int enriched;
	const char * key;
	char * val;
};

struct target_t {
	const char * name;
	struct keyval_t * key_vals;
	size_t len;
};

struct propperty_t {
	const char * name;
	struct target_t * targets;
	size_t len;
};

struct propperties_t {
	struct propperty_t * propperties;
	size_t len;
};

struct keyval_t_list {
	struct keyval_t * key_val;
	struct keyval_t_list * next;
};

static FILE * output_file = NULL;
static struct propperties_t props = {NULL, 0};

int load_output_file (char * output_filename) {

	if (output_filename != NULL) {
		if (! (output_file = fopen (output_filename, "w"))) {
			printf ("Can't open output file\n");
			return 1;
		}
	} else {
		output_file = stdout;
	}

	return 0;
}

int load_file (char * enrich_filename) {
	char errbuf[1024];
	FILE * file = NULL;
	char * fileData = NULL;
	int fileData_len = 0;


	if (enrich_filename != NULL) {
		if ( ! (file = fopen (enrich_filename, "r"))) {
			printf ("Can't open enrichment file\n");
			exit (1);
		}
		enrich = 1;

		/* null plug buffers */
		memset (errbuf, 0, sizeof (char));

		fileData = rd_file_read (enrich_filename, &fileData_len);

		/* we have the whole config file in memory.  let's parse it ... */
		node = yajl_tree_parse ((const char *) fileData, errbuf, sizeof (errbuf));

		/* parse error handling */
		if (node == NULL) {
			fprintf (stderr, "parse_error: ");
			if (strlen (errbuf)) fprintf (stderr, " %s", errbuf);
			else fprintf (stderr, "unknown error");
			fprintf (stderr, "\n");
			return 1;
		}

		size_t i;
		size_t j;
		size_t k;

		// Go to the root
		const char * root_path[] = { (const char *) 0 };
		yajl_val root = yajl_tree_get (node, root_path, yajl_t_object);
		props.propperties = (struct propperty_t *) calloc (YAJL_GET_OBJECT (root)->len,
		                    sizeof (struct propperty_t));
		props.len = YAJL_GET_OBJECT (root)->len;

		// Iterate through propperties
		for (i = 0; i < YAJL_GET_OBJECT (root)->len; i++) {
			props.propperties[i].name = YAJL_GET_OBJECT (root)->keys[i];
			// printf ("%s\n", props.propperties[i].name);

			// Go to the propperty
			const char * propperty_path[] = { YAJL_GET_OBJECT (root)->keys[i],
			                                  (const char *) 0
			                                };
			yajl_val propperty = yajl_tree_get (root, propperty_path, yajl_t_object);
			props.propperties[i].len = YAJL_GET_OBJECT (propperty)->len;
			props.propperties[i].targets = (struct target_t *) calloc (YAJL_GET_OBJECT (
			                                   propperty)->len, sizeof (struct target_t));

			// Iterate through targets
			for (j = 0; j < YAJL_GET_OBJECT (propperty)->len; j++) {
				props.propperties[i].targets[j].name = YAJL_GET_OBJECT (
				        propperty)->keys[j];

				// printf ("\t%s\n", props.propperties[i].targets[j].name);

				// Go to the target
				const char * target_path[] = { YAJL_GET_OBJECT (root)->keys[i],
				                               YAJL_GET_OBJECT (propperty)->keys[j],
				                               (const char *) 0
				                             };
				yajl_val target = yajl_tree_get (root, target_path, yajl_t_object);
				props.propperties[i].targets[j].len = YAJL_GET_OBJECT (target)->len;

				props.propperties[i].targets[j].key_vals = (struct keyval_t *) calloc (
				            YAJL_GET_OBJECT (target)->len, sizeof (struct keyval_t));

				for (k = 0; k < YAJL_GET_OBJECT (target)->len; k++) {
					props.propperties[i].targets[j].key_vals[k].key = YAJL_GET_OBJECT (
					            target)->keys[k];
					props.propperties[i].targets[j].key_vals[k].val = YAJL_GET_STRING (
					            YAJL_GET_OBJECT (
					                target)->values[k]);
					// printf ("\t\t%s: %s\n", props.propperties[i].targets[j].key_vals[k].key,
					// props.propperties[i].targets[j].key_vals[k].val);
				}
			}
		}
	}

	if (fileData != NULL) {
		free (fileData);
	}

	if (file != NULL) {
		fclose (file);
	}

	return 0;
}

static struct keyval_t_list * enrichment = NULL;

void add_enrich (const char * keyVal, char * valueVal) {

	struct keyval_t_list * enrichment_aux = NULL;

	if (enrichment == NULL) {
		enrichment = (struct keyval_t_list *) calloc (1, sizeof (struct keyval_t_list));
		enrichment->next = NULL;
		enrichment_aux = enrichment;
	} else {
		enrichment_aux = enrichment;

		while (enrichment_aux->next != NULL) {
			enrichment_aux = enrichment_aux->next;
		}

		enrichment_aux->next = (struct keyval_t_list *) calloc (1,
		                       sizeof (struct keyval_t_list));
		enrichment_aux = enrichment_aux->next;
		enrichment_aux->next = NULL;
	}

	enrichment_aux->key_val = (struct keyval_t *)calloc (1,
	                          sizeof (struct keyval_t));

	enrichment_aux->key_val->key = keyVal;
	enrichment_aux->key_val->val = valueVal;
	enrichment_aux->key_val->enriched = 0;
}

struct keyval_t_list * current_event = NULL;

char * src_addrr = NULL;
size_t src_addrr_len = 0;
char * dst_addrr = NULL;
size_t dst_addrr_len = 0;
int direction = 0;

void process (char * event, int resolve_names, time_t _timestamp) {

	timestamp = _timestamp;
	char errbuf[BUFSIZ];
	yajl_val event_node = yajl_tree_parse ((const char *) event, errbuf,
	                                       sizeof (errbuf));

	if (event_node == NULL) {
		fprintf (stderr, "parse_error: ");
		if (strlen (errbuf)) fprintf (stderr, " %s", errbuf);
		else fprintf (stderr, "unknown error");
		fprintf (stderr, "\n");
		return;
	}

	const char * root_path[] = { (const char *) 0 };
	yajl_val root = yajl_tree_get (event_node, root_path, yajl_t_object);

	size_t len = YAJL_GET_OBJECT (root)->len;

	size_t p = 0;
	int type = 0;
	int is_first_key = 1;

	// Iterate through propperties
	for (p = 0; p < len; p++) {

		const char * keyVal = YAJL_GET_OBJECT (root)->keys[p];
		char * valueVal = NULL;

		if (!YAJL_IS_NULL (YAJL_GET_OBJECT (root)->values[p])) {

			if (YAJL_IS_NUMBER (YAJL_GET_OBJECT (root)->values[p])) {
				valueVal = YAJL_GET_NUMBER ( YAJL_GET_OBJECT (root)->values[p]);
				type = 2;
			} else {
				valueVal = YAJL_GET_STRING ( YAJL_GET_OBJECT (root)->values[p]);
				type = 1;
			}

			size_t i = 0;
			size_t j = 0;
			size_t k = 0;

			// Check is there is enrichment data for each key
			if (enrich) {
				if (YAJL_IS_STRING (YAJL_GET_OBJECT (root)->values[p])) {
					for (i = 0; i < props.len; i++) {
						if (!strcmp (props.propperties[i].name, keyVal)) {
							for (j = 0; j < props.propperties[i].targets->len ; j++) {
								if (!strcmp (props.propperties[i].targets[j].name, valueVal)) {
									for (k = 0; k < props.propperties[i].targets[j].len; k++) {
										add_enrich (props.propperties[i].targets[j].key_vals[k].key,
										            props.propperties[i].targets[j].key_vals[k].val);
									}
									break;
								}
							}
						}
					}
				}
			}

			if (resolve_names) {
				if (!strcmp (keyVal, "src") || !strcmp (keyVal, "dst")) {
					if (rdns (valueVal, host_name)) {
						if (strlen (host_name) > 0) {
							add_enrich ("target_name", host_name);
						}
					}
				}
			}

			if (!strcmp (keyVal, "events")) {
				if (type == 2) {
					times = atoi (valueVal);
				}
			}

			struct keyval_t_list * current_event_aux = NULL;

			if (current_event == NULL) {
				current_event = (struct keyval_t_list *) calloc (1,
				                sizeof (struct keyval_t_list));
				current_event->next = NULL;
				current_event_aux = current_event;
			} else {

				current_event_aux = current_event;

				while (current_event_aux->next != NULL) {
					current_event_aux = current_event_aux->next;
				}

				current_event_aux->next = (struct keyval_t_list *) calloc (1,
				                          sizeof (struct keyval_t_list));
				current_event_aux = current_event_aux->next;
				current_event_aux->next = NULL;
			}

			current_event_aux->key_val = (struct keyval_t *)calloc (1,
			                             sizeof (struct keyval_t));

			current_event_aux->key_val->key = keyVal;
			current_event_aux->key_val->val = valueVal;
			current_event_aux->key_val->type = type;
			current_event_aux->key_val->is_first_key = is_first_key;
		}
	}

	end_process();
	yajl_tree_free (event_node);
}

int eventos = 0;

void end_process() {

	char event_timestamp[BUFSIZ];
	struct event_t processed_event = {NULL, 0, 0};
	struct keyval_t_list * current_event_aux  = current_event;
	struct keyval_t_list * current_event_free  = current_event;
	struct keyval_t_list * enrichment_aux = NULL;
	struct keyval_t_list * enrichment_free = NULL;
	int enriched = 0;
	int i = 0;

	event_putc (&processed_event, '{');
	int is_first_key = 1;

	while (current_event_aux != NULL) {

		if (strcmp (current_event_aux->key_val->key, "events")) {

			enrichment_aux = enrichment;

			while (enrichment_aux != NULL) {
				if (!strncmp (current_event_aux->key_val->key, enrichment_aux->key_val->key,
				              strlen (enrichment_aux->key_val->key))) {
					add_key (&processed_event, current_event_aux->key_val->key,
					         strlen (current_event_aux->key_val->key),
					         is_first_key);
					add_string (&processed_event, enrichment_aux->key_val->val,
					            strlen (enrichment_aux->key_val->val));
					enrichment_aux->key_val->enriched = 1;
					enriched++;
					is_first_key = 0;
					break;
				}

				enrichment_aux = enrichment_aux->next;
			}

			if (!enriched) {
				switch (current_event_aux->key_val->type) {
				case 1:
					add_key (&processed_event, current_event_aux->key_val->key,
					         strlen (current_event_aux->key_val->key),
					         is_first_key);
					add_string (&processed_event, current_event_aux->key_val->val,
					            strlen (current_event_aux->key_val->val));
					is_first_key = 0;

					break;
				case 2:
					add_key (&processed_event, current_event_aux->key_val->key,
					         strlen (current_event_aux->key_val->key),
					         is_first_key);
					add_number (&processed_event, current_event_aux->key_val->val,
					            strlen (current_event_aux->key_val->val));
					is_first_key = 0;

					break;
				default:
					break;
				}
			}
		}

		enriched = 0;
		current_event_free = current_event_aux;
		current_event_aux = current_event_aux->next;
		free (current_event_free->key_val);
		free (current_event_free);
	};

	enrichment_aux = enrichment;

	while (enrichment_aux != NULL) {

		if (enrichment_aux->key_val->enriched == 0) {
			add_key (&processed_event, enrichment_aux->key_val->key,
			         strlen (enrichment_aux->key_val->key), 0);
			add_string (&processed_event, enrichment_aux->key_val->val,
			            strlen (enrichment_aux->key_val->val));
		}

		enrichment_aux = enrichment_aux->next;
	}

	sprintf (event_timestamp, "%zu", timestamp - 59960732400);
	add_enrich ("timestamp", event_timestamp);

	add_key (&processed_event, "timestamp", strlen ("timestamp"), 0);
	add_number (&processed_event, event_timestamp, strlen (event_timestamp));

	while (enrichment != NULL) {
		enrichment_free = enrichment;
		enrichment = enrichment->next;
		free (enrichment_free->key_val);
		free (enrichment_free);
	}

	event_putc (&processed_event, '}');
	event_putc (&processed_event, '\n');
	event_putc (&processed_event, '\0');
	for (i = 0; i < times ; i++) {
		fwrite (processed_event.str, sizeof (char), processed_event.length - 1,
		        output_file);
	}

	current_event = NULL;
	enrichment = NULL;
	free (processed_event.str);
}

void close_file () {
	size_t i = 0;
	size_t j = 0;

	for (i = 0; i < props.len; i++) {

		for (j = 0; j < props.propperties[i].len; j++) {
			free (props.propperties[i].targets[j].key_vals);
		}

		free (props.propperties[i].targets);
	}

	free (props.propperties);
	yajl_tree_free (node);
	fclose (output_file);
}