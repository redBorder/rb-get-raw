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
#include <getopt.h>
#include <time.h>

#include "enrichment.h"

#define STR_TYPE 1
#define NUM_TYPE 2
#define NULL_TYPE 3

static int s_streamReformat = 0;
static uint on_event = 0;
static char * last_element = NULL;
static char * output_filename = NULL;
static char * start_time = NULL;
static char * end_time = NULL;

static yajl_handle hand;
static yajl_gen g;

int file_flag = 0;
char * source = NULL;

////////////////////////////////////////////////////////////////////////////////

int get_time (const char * p_time, struct tm *my_tm) {
	struct tm *aux = NULL;
	int rc = sscanf (p_time, "%d-%d-%dT%d:%d:%d",
	                 &my_tm->tm_year, &my_tm->tm_mon, &my_tm->tm_mday,
	                 &my_tm->tm_hour, &my_tm->tm_min, &my_tm->tm_sec);

	if ( rc == 1) {
		aux = gmtime ((const time_t *) &my_tm->tm_year);
		memcpy (my_tm, aux, sizeof (*aux));
		return 0;
	}

	if (rc != 6 ) {
		return 1;
	}

	my_tm->tm_year -= 1900;

	return 0;
}

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

static size_t WriteMemoryCallback (void *contents, size_t size, size_t nmemb,
                                   void *userp) {
	size_t realsize = size * nmemb;
	yajl_parse (hand, (const unsigned char *) contents, realsize);
	return realsize;
}

void rb_get_raw_getopts (int argc, char* argv[]) {

	char *cvalue = NULL;
	int index;
	int c;

	opterr = 0;
	while ((c = getopt (argc, argv, "o:d:s:e:")) != -1)
		switch (c) {
		case 'o':
			file_flag = 1;
			output_filename = optarg;
			break;
		case 'd':
			source = optarg;
			break;
		case 's':
			start_time = optarg;
			break;
		case 'e':
			end_time = optarg;
			break;
		case '?':
			if (optopt == 'c')
				fprintf (stderr, "Option -%c requires an argument.\n", optopt);
			else if (isprint (optopt))
				fprintf (stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf (stderr,
				         "Unknown option character `\\x%x'.\n",
				         optopt);
			return;
		default:
			abort ();
		}
	for (index = optind; index < argc; index++)
		printf ("Non-option argument %s\n", argv[index]);
}

void rb_get_raw_print_usage() {
	printf (
	    "Usage: rb_get_raw.rb -d data_source -s start_timestamp [-e end_timestamp] [-f enrichment_file_path]\n"
	    "Get enrichment data from Druid in json format.\n"
	    "Options:\n"
	    "\t -d\t\t select data source target to fetch data. Available data sources: rb_flow, rb_event, rb_monitor.\n"
	    "\t -s\t\t start time in unix or ISO format.\n"
	    "\t -e [OPTIONAL]\t end time in unix or ISO format. Default value is the current timestamp.\n"
	    "\t -f [OPTIONAL]\t absolute path of file used to enrich data.\n"
	    "\t -i [OPTIONAL]\t time interval in minutes, default 1 minute.\n"
	    "\t -g [OPTIONAL]\t granularity in minutes, default 1 minute.\n"
	    "\t -o [OPTIONAL]\t output file instead of stdout\n"
	    "\t\t\t The format of the content file is:\n"
	    "\n"
	    "-------------------------------------\n"
	    "Enrichment File Format:\n"
	    "-------------------------------------\n"
	    "sensor_name:\n"
	    "  'ASR':\n"
	    "    sensor_uuid: '7553351737517864910'\n"
	    "  'ISG':\n"
	    "    sensor_uuid: '1771993440671112867'\n"
	    "wireless_station:\n"
	    "  '22:22:22:22:22:22':\n"
	    "    floor_uuid: '1207968231979495976'\n"
	    "    building_uuid: '4137496452293841175'\n"
	    "    dot11_status: 'CONNECTED'\n"
	    "\n"
	);
}

////////////////////////////////////////////////////////////////////////////////
int main (int argc, char * argv[]) {

	dns_init (&dns_defctx, 1);
	int retval = 0;

	rb_get_raw_getopts (argc, argv);

	if (file_flag == 1) {
		load_output_file (output_filename);
	} else {
		load_output_file (NULL);
	}

	load_file ();

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
	                  "http://10.0.150.23:8080/druid/v2/?pretty=true");

	struct curl_slist * headers = NULL;
	headers = curl_slist_append (headers, "Accept: application/json");
	headers = curl_slist_append (headers,
	                             "Content-Type: application/json");
	headers = curl_slist_append (headers, "charsets: utf-8");
	curl_easy_setopt (curl_handle, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt (curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

	curl_easy_setopt (curl_handle, CURLOPT_POSTFIELDS,
	                  "{\"dataSource\":\"rb_flow\",\"granularity\":{\"type\":\"duration\",\"duration\":300000},\"intervals\":[\"2015-08-24T9:00:00+00:00/2015-08-25T12:00:00+00:00\"],\"queryType\":\"groupBy\",\"dimensions\":[\"application_id_name\",\"biflow_direction\",\"conversation\",\"direction\",\"engine_id_name\",\"http_user_agent_os\",\"http_host\",\"http_social_media\",\"http_social_user\",\"http_referer_l1\",\"l4_proto\",\"ip_protocol_version\",\"sensor_name\",\"sensor_uuid\",\"scatterplot\",\"src\",\"src_country_code\",\"src_net_name\",\"src_port\",\"src_as_name\",\"client_id\",\"client_mac\",\"client_mac_vendor\",\"dot11_status\",\"src_vlan\",\"src_map\",\"srv_port\",\"dst\",\"dst_country_code\",\"dst_net_name\",\"dst_port\",\"dst_as_name\",\"dst_vlan\",\"dst_map\",\"input_snmp\",\"output_snmp\",\"input_vrf\",\"output_vrf\",\"tos\",\"client_latlong\",\"coordinates_map\",\"deployment\",\"deployment_uuid\",\"namespace\",\"namespace_uuid\",\"campus\",\"campus_uuid\",\"building\",\"building_uuid\",\"floor\",\"floor_uuid\",\"zone\",\"zone_uuid\",\"wireless_uuid\",\"client_rssi\",\"client_rssi_num\",\"client_snr\",\"client_snr_num\",\"wireless_station\",\"hnblocation\",\"hnbgeolocation\",\"rat\",\"darklist_score_name\",\"darklist_category\",\"darklist_protocol\",\"darklist_direction\",\"darklist_score\",\"market\",\"market_uuid\",\"organization\",\"organization_uuid\",\"dot11_protocol\",\"type\",\"duration\"],\"aggregations\":[{\"type\":\"longSum\",\"name\":\"events\",\"fieldName\":\"events\"},{\"type\":\"longSum\",\"name\":\"pkts\",\"fieldName\":\"sum_pkts\"},{\"type\":\"longSum\",\"name\":\"bytes\",\"fieldName\":\"sum_bytes\"}]}");

	res = curl_easy_perform (curl_handle);

	if (res != CURLE_OK) {
		fprintf (stderr, "curl_easy_perform() failed: %s\n",
		         curl_easy_strerror (res));
	}

	curl_easy_cleanup (curl_handle);
	curl_global_cleanup();
	yajl_gen_free (g);
	yajl_free (hand);

	if (file_flag == 1) {
		close_file();
	}

	return retval;
}
