#include "enrichment.h"

static FILE * output = NULL;
static yajl_val node;
static int enrich = 1;

struct keyval_t {
	int is_first_key;
	int type;
	int enriched;
	char * key;
	size_t key_len;
	char * val;
	size_t val_len;
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

static struct propperties_t props = {NULL, 0};

int load_file (FILE * _output) {

	size_t rd;
	char errbuf[1024];
	FILE * file = NULL;
	unsigned char fileData[65536];

	output = _output;

	if ( ! (file = fopen ("enrichment.json", "r"))) {
		printf ("No se puede abrir el fichero de enriquecimiento\n");
		exit (1);
	}

	if (! (output = fopen ("output", "w"))) {
		printf ("No se puede abrir fichero de salida\n");
		exit (1);
	} else {
		printf ("Abierto fichero de salida\n");
	}

	/* null plug buffers */
	fileData[0] = errbuf[0] = 0;

	/* read the entire config file */
	rd = fread ((void *) fileData, 1, sizeof (fileData) - 1, file);

	/* file read error handling */
	if (rd == 0 && !feof (stdin)) {
		fprintf (stderr, "error encountered on file read\n");
		return 1;
	} else if (rd >= sizeof (fileData) - 1) {
		fprintf (stderr, "config file too big\n");
		return 1;
	}

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

	int i;
	int j;
	int k;

	// Go to the root
	const char * root_path[] = { (const char *) 0 };
	yajl_val root = yajl_tree_get (node, root_path, yajl_t_object);
	props.propperties = (struct propperty_t *) calloc (YAJL_GET_OBJECT (root)->len,
	                    sizeof (struct propperty_t));
	props.len = YAJL_GET_OBJECT (root)->len;

	// Iterate through propperties
	for (i = 0; i < YAJL_GET_OBJECT (root)->len; i++) {
		props.propperties[i].name = YAJL_GET_OBJECT (root)->keys[i];
		printf ("%s\n", props.propperties[i].name);

		// Go to the propperty
		const char * propperty_path[] = { YAJL_GET_OBJECT (root)->keys[i],
		                                  (const char *) 0
		                                };
		yajl_val propperty = yajl_tree_get (root, propperty_path, yajl_t_object);
		props.propperties[i].targets = (struct target_t *) calloc (YAJL_GET_OBJECT (
		                                   propperty)->len, sizeof (struct target_t));

		// Iterate through targets
		for (j = 0; j < YAJL_GET_OBJECT (propperty)->len; j++) {
			props.propperties[i].targets[j].name = YAJL_GET_OBJECT (propperty)->keys[j];
			printf ("\t%s\n", props.propperties[i].targets[j].name);

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
				props.propperties[i].targets[j].key_vals[k].key = (char *) YAJL_GET_OBJECT (
				            target)->keys[k];
				props.propperties[i].targets[j].key_vals[k].val = YAJL_GET_STRING (
				            YAJL_GET_OBJECT (
				                target)->values[k]);
				printf ("\t\t%s: %s\n", props.propperties[i].targets[j].key_vals[k].key,
				        props.propperties[i].targets[j].key_vals[k].val);
			}
		}
	}
	return 0;
}

static struct keyval_t_list * enrichment = NULL;

static void add_enrich (char * keyVal,
                        size_t keyLen,
                        char * valueVal,
                        size_t valueLen) {

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
	enrichment_aux->key_val->key_len = keyLen;
	enrichment_aux->key_val->val = valueVal;
	enrichment_aux->key_val->val_len = valueLen;
	enrichment_aux->key_val->enriched = 0;
}

char * src_addrr = NULL;
size_t src_addrr_len = 0;
char * dst_addrr = NULL;
size_t dst_addrr_len = 0;
int direction = 0;

// static struct keyval_t_list * enrichment = NULL;
static struct keyval_t_list * current_event = NULL;

void process (char * keyVal,
              size_t keyLen,
              char * valueVal,
              size_t valueLen,
              int is_first_key,
              int type) {

	int i = 0;
	int j = 0;
	int k = 0;

	// Check is there is enrichment data for each key
	if (valueVal != NULL) {
		for (i = 0; i < props.len; i++) {
			if (!strncmp (props.propperties[i].name, keyVal, keyLen)) {
				for (j = 0; j < props.propperties[i].targets->len ; j++) {
					if (!strncmp (props.propperties[i].targets[j].name, valueVal, valueLen)) {
						for (k = 0; k < props.propperties[i].targets[j].len; k++) {
							add_enrich (props.propperties[i].targets[j].key_vals[k].key,
							            strlen (props.propperties[i].targets[j].key_vals[k].key),
							            props.propperties[i].targets[j].key_vals[k].val,
							            strlen (props.propperties[i].targets[j].key_vals[k].val));
						}
						break;
					}
				}
			}
		}

		if (!strncmp ("direction", keyVal, keyLen)) {
			if (!strncmp ("ingress", valueVal, valueLen)) {
				direction = 1;
			} else {
				direction = 0;
			}
		}

		if (!strncmp ("src", keyVal, keyLen)) {
			src_addrr = valueVal;
			src_addrr_len = valueLen;
		}

		if (!strncmp ("dst", keyVal, keyLen)) {
			dst_addrr = valueVal;
			dst_addrr_len = valueLen;
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
	current_event_aux->key_val->key_len = keyLen;
	current_event_aux->key_val->val = valueVal;
	current_event_aux->key_val->val_len = valueLen;
	current_event_aux->key_val->type = type;
	current_event_aux->key_val->is_first_key = is_first_key;
}

int eventos = 0;

void end_process() {

	char * host_name = (char *) calloc (128, sizeof (char));
	struct event_t processed_event = {NULL, 0, 0};
	struct keyval_t_list * current_event_aux  = current_event;
	struct keyval_t_list * current_event_free  = current_event;
	struct keyval_t_list * enrichment_aux = NULL;
	struct keyval_t_list * enrichment_free = NULL;
	int enriched = 0;

	if (direction) {
		if (rdns (dst_addrr, dst_addrr_len, host_name)) {
			add_enrich ("dst_net_name",
			            strlen ("dst_net_name"),
			            host_name,
			            strlen (host_name));
		}
	} else {
		if (rdns (src_addrr, src_addrr_len, host_name)) {
			add_enrich ("src_net_name",
			            strlen ("src_net_name"),
			            host_name,
			            strlen (host_name));
		}
	}

	while (current_event_aux != NULL) {
		add_key (&processed_event, current_event_aux->key_val->key,
		         current_event_aux->key_val->key_len,
		         current_event_aux->key_val->is_first_key);

		enrichment_aux = enrichment;

		while (enrichment_aux != NULL) {
			if (!strncmp (current_event_aux->key_val->key, enrichment_aux->key_val->key,
			              enrichment_aux->key_val->key_len) && enrich) {
				add_string (&processed_event, enrichment_aux->key_val->val,
				            enrichment_aux->key_val->val_len);
				enrichment_aux->key_val->enriched = 1;
				enriched++;
				break;
			}

			enrichment_aux = enrichment_aux->next;
		}

		if (!enriched) {
			switch (current_event_aux->key_val->type) {
			case 1:
				add_string (&processed_event, current_event_aux->key_val->val,
				            current_event_aux->key_val->val_len);
				break;
			case 2:
				add_number (&processed_event, current_event_aux->key_val->val,
				            current_event_aux->key_val->val_len);
				break;
			case 3:
				add_null (&processed_event);
				break;
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

		if (enrichment_aux->key_val->enriched == 0 && enrich) {
			add_key (&processed_event, enrichment_aux->key_val->key,
			         enrichment_aux->key_val->key_len, 0);
			add_string (&processed_event, enrichment_aux->key_val->val,
			            enrichment_aux->key_val->val_len);
		}

		enrichment_aux = enrichment_aux->next;
	}

	while (enrichment != NULL) {
		enrichment_free = enrichment;
		enrichment = enrichment->next;
		free (enrichment_free->key_val);
		free (enrichment_free);
	}

	event_putc (&processed_event, '\n');
	event_putc (&processed_event, '\0');
	// printf ("%s\n", processed_event.str);
	fwrite (processed_event.str, sizeof (char), processed_event.length - 1,
	        output);

	current_event = NULL;
	enrichment = NULL;
	free (processed_event.str);
}

void close_output() {
	fclose (output);
}