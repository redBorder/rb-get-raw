#include "util.h"

static int dns_done = 0;

void event_putc (struct event_t * event, char c) {
	int resized = 0;

	while (event->length >= event->size) {
		resized = 1;
		event->size += BLOCK_SIZE;
	}
	if (resized) {
		event->str = (char*) realloc (event->str, event->size);
		if (event->str == NULL) exit (1);
	}
	event->str[event->length] = c;
	event->length++;
}

void event_puts (struct event_t * event, char * s, size_t len) {
	int resized = 0;

	while (event->size - event->length <= len) {
		event->size += BLOCK_SIZE;
		resized = 1;
	}
	if (resized) {
		event->str = (char*) realloc (event->str, event->size);
		if (event->str == NULL) exit (1);
	}
	memcpy ((void*)event->str + event->length, s, len);
	event->length += len;
}

void add_key (struct event_t * event, char * new_key, size_t len,
              int first_key) {
	if (first_key) {
		first_key = 0;
	} else {
		event_putc (event, ',');
	}
	event_putc (event, '\"');
	event_puts (event, new_key, len);
	event_putc (event, '\"');
	event_putc (event, ':');
}

void add_number (struct event_t * event, char * new_number,
                 size_t len) {
	event_puts (event, new_number, len);
}

void add_string (struct event_t * event, char * new_string, size_t len) {
	event_putc (event, '\"');
	event_puts (event, new_string, len);
	event_putc (event, '\"');
}

void add_null (struct event_t * event) {
	event_puts (event, "null", strlen ("null"));
}

struct dns_cache_t * dns_cache = NULL;

int IsPrivateAddress (char * ip_addr) {
	int i = 0;
	char s1[4];
	char s2[4];

	if (ip_addr != NULL) {

		if (ip_addr[0] > '0' && ip_addr[0] < '9') {
			while ( ip_addr[i] != '.' ) {
				s1[i] = ip_addr[i];
				i++;
			}

			s1[++i] = '\0';
			uint b1 = atoi (s1);

			while ( ip_addr[i] != '.' ) {
				s2[i] = ip_addr[i];
				i++;
			}

			s2[++i] = '\0';
			uint b2 = atoi (s2);

			// 10.x.y.z
			if (b1 == 10)
				return 1;

			// 172.16.0.0 - 172.31.255.255
			if ((b1 == 172) && (b2 >= 16) && (b2 <= 31))
				return 1;

			// 192.168.0.0 - 192.168.255.255
			if ((b1 == 192) && (b2 == 168))
				return 1;
		}
	}

	return 0;
}

struct dns_info_t {
	char * ip;
	char * name;
	// TODO name len
};

void dns_cb (struct dns_ctx * ctx,
             struct dns_rr_ptr * result, void * opaque) {

	struct dns_info_t * dns_info = (struct dns_info_t *) opaque;
	dns_done = 1;

	if (result != NULL &&  result->dnsptr_ptr != NULL
	        && result->dnsptr_ptr[0] != NULL) {

		dns_info->name = strdup (result->dnsptr_ptr[0]);
		free (result);
		add_cache (dns_info->ip, dns_info->name);
	}
}

int rdns (char * string_val, char * host) {
	struct in_addr ipv4addr;
	int ret = 0;
	char * cached_host = NULL;
	struct dns_info_t * dns_info = (struct dns_info_t *) calloc (1,
	                               sizeof (struct dns_info_t));

	char * ip = (char *)calloc (strlen (string_val), sizeof (char));
	strncpy (ip, string_val, strlen (string_val));

	dns_info->ip = ip;

	if (inet_pton (AF_INET, ip, &ipv4addr)) {

		if (!IsPrivateAddress (ip)) {

			if ((cached_host = get_cache (ip)) != NULL ) {

				if (strlen (cached_host) > 0) {
					strncpy (host, cached_host, strlen (cached_host));
					printf ("CACHE HIT: %s --> %s\n", dns_info->ip, host);
					ret = 1;
				} else {
					ret = 0;
				}
			} else {
				dns_done = 0;
				dns_submit_a4ptr (&dns_defctx, &ipv4addr, dns_cb, dns_info);

				while (!dns_done) {
					time_t now = time (NULL);
					dns_ioevent (&dns_defctx, now);
					dns_timeouts (&dns_defctx, 5, now);
				}
				printf ("RESOLVED: %s --> %s\n", dns_info->ip, dns_info->name);
				ret = 1;
			}
		}
	}

	free (ip);
	return ret;
}

void add_cache (char * ip, char * name) {

	struct dns_cache_t * dns_cache_aux = NULL;

	if (dns_cache == NULL) {
		dns_cache = (struct dns_cache_t *) calloc (1, sizeof (struct dns_cache_t));
		dns_cache->next = NULL;
		dns_cache_aux = dns_cache;
	} else {
		dns_cache_aux = dns_cache;

		while (dns_cache_aux->next != NULL) {
			dns_cache_aux = dns_cache_aux->next;
		}

		dns_cache_aux->next = (struct dns_cache_t *) calloc (1,
		                      sizeof (struct dns_cache_t));
		dns_cache_aux = dns_cache_aux->next;
		dns_cache_aux->next = NULL;

		strncpy (dns_cache_aux->ip, ip, strlen (ip));
		if (name != NULL) {
			strncpy (dns_cache_aux->name, name, strlen (name));
			dns_cache_aux->err = 0;
		} else {
			dns_cache_aux->err = 1;
		}
	}
}

char * get_cache (char * ip) {
	struct dns_cache_t * dns_cache_aux = dns_cache;

	while (dns_cache_aux != NULL) {
		if (dns_cache_aux->err != 1) {
			if (!strncmp (dns_cache_aux->ip, ip, strlen (ip))) {
				return dns_cache_aux->name;
			}
		} else {
			return "";
		}
		dns_cache_aux = dns_cache_aux->next;
	}

	return NULL;
}