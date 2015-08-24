/**
** Copyright (C) 2015 Eneo Tecnologia S.L.
** Author: Diego Fernández <bigomby@gmail.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as
** published by the Free Software Foundation, either version 3 of the
** License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <udns.h>

#include "enrichment.h"

#define STR_TYPE 1
#define NUM_TYPE 2
#define NULL_TYPE 3

static int s_streamReformat = 0;
static uint on_event = 0;
static FILE * output = NULL;
static char * last_element = NULL;

static yajl_handle hand;
static yajl_gen g;

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
	GEN_AND_RETURN (yajl_gen_null (g));
}
static int reformat_boolean (void * ctx, int boolean) {
	yajl_gen g = (yajl_gen) ctx;
	GEN_AND_RETURN (yajl_gen_bool (g, boolean));
}

static int reformat_number (void * ctx, const char * s, size_t l) {
	yajl_gen g = (yajl_gen) ctx;

	last_element = (char *)s + l;

	GEN_AND_RETURN (yajl_gen_number (g, s, l));
}
static int reformat_string (void * ctx, const unsigned char * stringVal,
                            size_t stringLen) {
	yajl_gen g = (yajl_gen) ctx;

	last_element = (char *)stringVal + stringLen;

	GEN_AND_RETURN (yajl_gen_string (g, stringVal, stringLen));
}
static int reformat_map_key (void * ctx, const unsigned char * stringVal,
                             size_t stringLen) {
	yajl_gen g = (yajl_gen) ctx;

	if (!on_event
	        && !strncmp ((const char *)stringVal, "event", strlen ("event"))) {

		on_event = 1;
	}

	GEN_AND_RETURN (yajl_gen_string (g, stringVal, stringLen));
}
static int reformat_start_map (void * ctx) {
	yajl_gen g = (yajl_gen) ctx;

	if (on_event) {
		yajl_gen_clear (g);
	}

	GEN_AND_RETURN (yajl_gen_map_open (g));
}

static int reformat_end_map (void * ctx) {
	yajl_gen g = (yajl_gen) ctx;

	char *event = NULL;
	size_t aux_size = 0;

	yajl_gen_status rc = yajl_gen_map_close (g);

	if (on_event) {
		yajl_gen_get_buf (g, (const unsigned char **)&event, &aux_size);
		on_event = 0;
		// printf ("%s\n\n", event + 1);
		process ((char *)event + 1);
	}

	GEN_AND_RETURN (rc);
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

static size_t
WriteMemoryCallback (void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;

	//memcpy (fileData, contents + fileData_pos, realsize);

	//printf ("%d\n",  printbuf_length (&p));
	yajl_parse (hand, (const unsigned char *) contents, realsize);

	return realsize;
}

////////////////////////////////////////////////////////////////////////////////
int main () {

	dns_init (&dns_defctx, 1);
	int retval = 0;

	if (! (output = fopen ("output", "w"))) {
		printf ("No se puede abrir fichero de salida\n");
		exit (1);
	} else {
		printf ("Abierto fichero de salida\n");
		load_file (output);

		CURL *curl_handle;
		CURLcode res;
		g = yajl_gen_alloc (NULL);
		yajl_gen_config (g, yajl_gen_beautify, 0);
		yajl_gen_config (g, yajl_gen_validate_utf8, 1);

		hand = yajl_alloc (&callbacks, NULL, (void *) g);
		yajl_config (hand, yajl_allow_comments, 1);

		curl_global_init (CURL_GLOBAL_ALL);
		curl_handle = curl_easy_init();

		curl_easy_setopt (curl_handle, CURLOPT_URL,
		                  "http://rbb1xdbmdfoi:8080/druid/v2/?pretty=true");

		struct curl_slist * headers = NULL;
		headers = curl_slist_append (headers, "Accept: application/json");
		headers = curl_slist_append (headers,
		                             "Content-Type: application/json");
		headers = curl_slist_append (headers, "charsets: utf-8");
		curl_easy_setopt (curl_handle, CURLOPT_HTTPHEADER, headers);

		curl_easy_setopt (curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

		curl_easy_setopt (curl_handle, CURLOPT_POSTFIELDS,
		                  "{\"dataSource\":\"rb_flow\",\"granularity\":{\"type\":\"duration\",\"duration\":300000},\"intervals\":[\"2015-08-24T9:00:00+00:00/2015-08-24T9:10:00+00:00\"],\"queryType\":\"groupBy\",\"dimensions\":[\"application_id_name\",\"biflow_direction\",\"conversation\",\"direction\",\"engine_id_name\",\"http_user_agent_os\",\"http_host\",\"http_social_media\",\"http_social_user\",\"http_referer_l1\",\"l4_proto\",\"ip_protocol_version\",\"sensor_name\",\"sensor_uuid\",\"scatterplot\",\"src\",\"src_country_code\",\"src_net_name\",\"src_port\",\"src_as_name\",\"client_id\",\"client_mac\",\"client_mac_vendor\",\"dot11_status\",\"src_vlan\",\"src_map\",\"srv_port\",\"dst\",\"dst_country_code\",\"dst_net_name\",\"dst_port\",\"dst_as_name\",\"dst_vlan\",\"dst_map\",\"input_snmp\",\"output_snmp\",\"input_vrf\",\"output_vrf\",\"tos\",\"client_latlong\",\"coordinates_map\",\"deployment\",\"deployment_uuid\",\"namespace\",\"namespace_uuid\",\"campus\",\"campus_uuid\",\"building\",\"building_uuid\",\"floor\",\"floor_uuid\",\"zone\",\"zone_uuid\",\"wireless_uuid\",\"client_rssi\",\"client_rssi_num\",\"client_snr\",\"client_snr_num\",\"wireless_station\",\"hnblocation\",\"hnbgeolocation\",\"rat\",\"darklist_score_name\",\"darklist_category\",\"darklist_protocol\",\"darklist_direction\",\"darklist_score\",\"market\",\"market_uuid\",\"organization\",\"organization_uuid\",\"dot11_protocol\",\"type\",\"duration\"],\"aggregations\":[{\"type\":\"longSum\",\"name\":\"events\",\"fieldName\":\"events\"},{\"type\":\"longSum\",\"name\":\"pkts\",\"fieldName\":\"sum_pkts\"},{\"type\":\"longSum\",\"name\":\"bytes\",\"fieldName\":\"sum_bytes\"}]}");

		res = curl_easy_perform (curl_handle);

		if (res != CURLE_OK) {
			fprintf (stderr, "curl_easy_perform() failed: %s\n",
			         curl_easy_strerror (res));
		}

		curl_easy_cleanup (curl_handle);
		curl_global_cleanup();
		yajl_gen_free (g);
		yajl_free (hand);
		fclose (output);
	}

	return retval;
}