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

typedef enum {
	rb_flow			= 1,
	rb_event		= 2,
	rb_social		= 3,
	rb_monitor	= 4
} SERVICE;

#define STR_TYPE 1
#define NUM_TYPE 2
#define NULL_TYPE 3
#define PATH "/druid/v2/?pretty=true"

static int s_streamReformat = 0;
static uint on_event = 0;
static uint on_timestamp = 0;
static char * output_filename = NULL;
static time_t end_time_s = 0;
static time_t start_time_s = 0;
static int interval = 1;
static yajl_handle hand;
static int granularity = 1;
static int resolve_names = 0;
static char * enrich_filename = NULL;
static char * host = NULL;
static char * url = NULL;
static char * timestamp = NULL;
static time_t timestamp_t = 0;

static SERVICE service = 0;

int file_flag = 0;
char * source = NULL;

////////////////////////////////////////////////////////////////////////////////

static int get_time (const char * p_time, time_t * my_tm) {
	struct tm aux = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	int rc = sscanf (p_time, "%d-%d-%dT%d:%d:%d",
	                 &aux.tm_year, &aux.tm_mon, &aux.tm_mday,
	                 &aux.tm_hour, &aux.tm_min, &aux.tm_sec);

	*(&aux.tm_isdst) = -1;

	if ( rc == 1) {
		*my_tm = aux.tm_year;
		return 0;
	}

	if (rc != 6 ) {
		return 1;
	}

	time_t t = time(NULL);
  	struct tm lt = {0};

  	localtime_r(&t, &lt);

	*(&aux.tm_year) -= 1900;
	*(&aux.tm_mon) -= 1;
	*my_tm = mktime (&aux);
	*my_tm += lt.tm_gmtoff;

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
	GEN_AND_RETURN (yajl_gen_number (g, s, l));
}
static int reformat_string (void * ctx, const unsigned char * stringVal,
                            size_t stringLen) {
	yajl_gen g = (yajl_gen) ctx;
	if (on_timestamp) {
		timestamp = calloc (stringLen + 1, sizeof (char));
		strncpy (timestamp, (char *) stringVal, stringLen);
		timestamp[stringLen] = '\0';
		on_timestamp = 0;
	}
	GEN_AND_RETURN (yajl_gen_string (g, stringVal, stringLen));
}
static int reformat_map_key (void * ctx, const unsigned char * stringVal,
                             size_t stringLen) {
	yajl_gen g = (yajl_gen) ctx;

	if (!on_event) {
		if (!strncmp ((const char *)stringVal, "event", strlen ("event"))) {
			on_event = 1;
		}

		if (!strncmp ((const char *)stringVal, "timestamp", strlen ("timestamp"))) {
			on_timestamp = 1;
		}
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
		get_time (timestamp, &timestamp_t);
		process ((char *)event + 1, resolve_names, timestamp_t);
		free (timestamp);
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

static size_t WriteMemoryCallback (void *contents, size_t size, size_t nmemb) {
	size_t realsize = size * nmemb;
	yajl_parse (hand, (const unsigned char *) contents, realsize);
	return realsize;
}

static void rb_get_raw_print_usage() {
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
	    "\t -n [OPTIONAL]\t resolve host names\n"
	    "\t -h [OPTIONAL]\t DRUID host. If not provided, get from zookeper (TODO)\n"
	    "\n"
	    "-------------------------------------\n"
	    "Enrichment JSON File Format:\n"
	    "-------------------------------------\n"
	    "{\n"
	    "  \"sensor_name\": {\n"
	    "    \"ASR\": {\n"
	    "      \"sensor_uuid\": \"7553351737517864910\"\n"
	    "    },\n"
	    "    \"ISG\": {\n"
	    "      \"sensor_uuid\": \"1771993440671112867\"\n"
	    "    }\n"
	    "  },\n"
	    "  \"wireless_station\": {\n"
	    "    \"22:22:22:22:22:22\": {\n"
	    "      \"floor_uuid\": \"1207968231979495976\",\n"
	    "      \"building_uuid\": \"413749645229384117\",\n"
	    "      \"dot11_status\": \"CONNECTED\"\n"
	    "    }\n"
	    "  }\n"
	    "}\n"
	);
}

static void rb_get_raw_getopts (int argc, char* argv[]) {

	int c;

	end_time_s = time (NULL);

	opterr = 0;
	while ((c = getopt (argc, argv, "no:d:s:e:i:g:f:h:?")) != -1)
		switch (c) {
		case 'o':
			file_flag = 1;
			output_filename = optarg;
			break;
		case 'f':
			enrich_filename = optarg;
			break;
		case 'd':
			source = optarg;
			break;
		case 's':
			get_time (optarg, &start_time_s);
			break;
		case 'e':
			get_time (optarg, &end_time_s);
			break;
		case 'i':
			interval = atoi (optarg);
			break;
		case 'g':
			granularity = atoi (optarg);
			break;
		case 'n':
			resolve_names = 1;
			break;
		case 'h':
			host = optarg;
			break;
		case '?':
			return;
		default:
			abort ();
		}

	if (start_time_s == 0 ) {
		rb_get_raw_print_usage();
		printf ("\nInvalid start time\n");
		exit (1);
	}

	if (source != NULL) {
		if (!strcmp (source, "rb_flow")) service = rb_flow;
		if (!strcmp (source, "rb_monitor")) service = rb_monitor;
		if (!strcmp (source, "rb_event")) service = rb_event;
		if (!strcmp (source, "rb_social")) service = rb_social;
	}

	if (service == 0) {
		rb_get_raw_print_usage();
		printf ("\nInvalid source\n");
		exit (1);
	}

	if (granularity < interval) {
		granularity = interval;
	}

	if (host == NULL) {
		rb_get_raw_print_usage();
		printf ("\nHost not provided\n");
		exit (1);
	} else {
		url = calloc (strlen (host) + strlen (PATH) + 1, sizeof (char));
		strcat (url, host);
		strcat (url, PATH);
	}
}

static int gen_query0 (char *dst, size_t dst_sz, char * start_interval_str,
                       char * end_interval_str) {

	char * dimensions = NULL;
	char * aggregations = NULL;

	switch (service) {
	case rb_flow:
		dimensions =
		    "[\"application_id_name\",\"biflow_direction\", \"conversation\", \"direction\", \"engine_id_name\", \"http_user_agent_os\", \"http_host\", \"http_social_media\", \"http_social_user\", \"http_referer_l1\", \"l4_proto\", \"ip_protocol_version\", \"sensor_name\", \"sensor_uuid\", \"scatterplot\", \"src\", \"src_country_code\", \"src_net_name\", \"src_port\", \"src_as_name\", \"client_id\", \"client_mac\", \"client_mac_vendor\", \"dot11_status\", \"src_vlan\", \"src_map\", \"srv_port\", \"dst\", \"dst_country_code\", \"dst_net_name\", \"dst_port\", \"dst_as_name\", \"dst_vlan\", \"dst_map\", \"input_snmp\", \"output_snmp\", \"input_vrf\", \"output_vrf\", \"tos\", \"client_latlong\", \"coordinates_map\", \"deployment\", \"deployment_uuid\", \"namespace\", \"namespace_uuid\", \"campus\", \"campus_uuid\", \"building\", \"building_uuid\", \"floor\", \"floor_uuid\", \"zone\", \"zone_uuid\", \"wireless_uuid\", \"client_rssi\", \"client_rssi_num\", \"client_snr\", \"client_snr_num\", \"wireless_station\", \"hnblocation\", \"hnbgeolocation\", \"rat\", \"darklist_score_name\", \"darklist_category\", \"darklist_protocol\", \"darklist_direction\", \"darklist_score\", \"market\", \"market_uuid\", \"organization\", \"organization_uuid\", \"dot11_protocol\", \"type\", \"duration\"]";
		aggregations = "[{"
		               "\"type\": \"longSum\","
		               "\"name\": \"events\","
		               "\"fieldName\": \"events\""
		               "}, {"
		               "\"type\": \"longSum\","
		               "\"name\": \"pkts\","
		               "\"fieldName\": \"sum_pkts\""
		               "}, {"
		               "\"type\": \"longSum\","
		               "\"name\": \"bytes\","
		               "\"fieldName\": \"sum_bytes\""
		               "}]";
		break;
	case rb_event:
		dimensions =
		    "[\"action\", \"classification\", \"conversation\", \"domain_name\", \"ethlength_range\", \"group_name\", \"group_id\", \"group_uuid\", \"sig_generator\", \"icmptype\", \"iplen_range\", \"l4_proto\", \"rev\", \"sensor_name\", \"sensor_uuid\", \"deployment\", \"deployment_uuid\", \"namespace\", \"namespace_uuid\", \"priority\", \"msg\", \"sig_id\", \"scatterplot\", \"ethsrc\", \"ethsrc_vendor\", \"src\", \"src_country_code\", \"src_net_name\", \"src_port\", \"src_as_name\", \"src_map\", \"ethdst\", \"ethdst_vendor\", \"dst\", \"dst_country_code\", \"dst_net_name\", \"dst_port\", \"dst_as_name\", \"dst_map\", \"tos\", \"ttl\", \"vlan\", \"darklist_score_name\", \"darklist_category\", \"darklist_protocol\", \"darklist_direction\", \"darklist_score\", \"market\", \"market_uuid\", \"organization\", \"organization_uuid\", \"client_latlong\", \"floor\", \"floor_uuid\", \"zone\", \"building\", \"building_uuid\", \"campus\", \"campus_uuid\", \"wireless_station\", \"sha256\", \"file_size\", \"file_uri\", \"file_hostname\"]";
		aggregations = "[{"
		               "\"type\": \"longSum\","
		               "\"name\": \"events\","
		               "\"fieldName\": \"events\""
		               "}]";
		break;
	case rb_monitor:
		dimensions =
		    "[\"sensor_name\", \"monitor\", \"type\", \"unit\", \"group_name\"]";
		aggregations = "[{"
		               "\"type\": \"longSum\","
		               "\"name\": \"monitors\","
		               "\"fieldName\": \"events\""
		               "}, {"
		               "\"type\": \"doubleSum\","
		               "\"name\": \"sum_value\","
		               "\"fieldName\": \"sum_value\""
		               "}]";
		break;
	case rb_social:
		dimensions =
		    "[\"client_id\", \"client_latlong\", \"user_screen_name\", \"user_name\", \"user_id\", \"type\", \"hashtags\", \"mentions\", \"msg\", \"sentiment\", \"msg_send_from\", \"user_from\", \"user_profile_img_https\", \"src_country_code\", \"influence\", \"picture_url\", \"language\", \"category\", \"sensor_name\", \"sensor_uuid\", \"floor\", \"floor_uuid\", \"building\", \"building_uuid\", \"campus\", \"campus_uuid\", \"market\", \"market_uuid\", \"organization\", \"organization_uuid\", \"service_provider\", \"service_provider_uuid\", \"deployment\", \"deployment_uuid\", \"namespace\", \"namespace_uuid\"]";
		aggregations = "[{"
		               "\"type\": \"count\","
		               "\"name\": \"events\""
		               "}, {"
		               "\"type\": \"longSum\","
		               "\"name\": \"sum_followers\","
		               "\"fieldName\": \"followers\""
		               "}]";
		break;
	default:
		dimensions = "";
		aggregations = "";
		break;
	}

	return snprintf (dst, dst_sz, "{"
	                 "\"dataSource\": \"%s\","
	                 "\"granularity\": {"
	                 "\"type\": \"duration\","
	                 "\"duration\": %d"
	                 "},"
	                 "\"intervals\": [\"%s+00:00/%s+00:00\"],"
	                 "\"queryType\": \"groupBy\","
	                 "\"dimensions\": %s,"
	                 "\"aggregations\": %s"
	                 "}",
	                 source,
	                 granularity,
	                 start_interval_str,
	                 end_interval_str,
	                 dimensions,
	                 aggregations);

}

static char *gen_query (char * start_interval_str,
                        char * end_interval_str) {
	const int dst_sz = gen_query0 (NULL, 0, start_interval_str, end_interval_str);
	// TODO error treatment

	char * ret = calloc (dst_sz + 1.0, 1);
	// TODO error treatment
	gen_query0 (ret, dst_sz + 1.0, start_interval_str, end_interval_str);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////
int main (int argc, char * argv[]) {

	yajl_gen g;
	dns_init (&dns_defctx, 1);
	int retval = 0;

	rb_get_raw_getopts (argc, argv);

	if (file_flag == 1) {
		load_output_file (output_filename);
	} else {
		load_output_file (NULL);
	}

	load_file (enrich_filename);

	time_t interval_s = interval * 60;
	time_t end_interval_s = 0;
	time_t start_interval_s = 0;
	start_interval_s = start_time_s;
	char * query = NULL;

	CURL *curl_handle;
	CURLcode res;

	curl_global_init (CURL_GLOBAL_ALL);

	while (end_time_s > end_interval_s) {

		g = yajl_gen_alloc (NULL);
		yajl_gen_config (g, yajl_gen_beautify, 0);
		yajl_gen_config (g, yajl_gen_validate_utf8, 1);

		hand = yajl_alloc (&callbacks, NULL, (void *) g);
		yajl_config (hand, yajl_allow_comments, 1);

		if (start_interval_s + interval_s > end_time_s) {
			end_interval_s = end_time_s;
		} else {
			end_interval_s = start_interval_s + interval_s;
		}

		char start_interval_str[BUFSIZ], end_interval_str[BUFSIZ];
		strftime (start_interval_str, sizeof (start_interval_str), "%FT%R:00",
		          gmtime (&start_interval_s));
		strftime (end_interval_str, sizeof (end_interval_str), "%FT%R:00",
		          gmtime (&end_interval_s));

		query = gen_query (start_interval_str, end_interval_str);
		if (file_flag) {
			printf ("Getting data from druid [ %s/%s ]\n", start_interval_str,
			        end_interval_str);
		}
		curl_handle = curl_easy_init();

		curl_easy_setopt (curl_handle, CURLOPT_URL, url);

		struct curl_slist * headers = NULL;
		headers = curl_slist_append (headers, "Accept: application/json");
		headers = curl_slist_append (headers,
		                             "Content-Type: application/json");
		headers = curl_slist_append (headers, "charsets: utf-8");
		curl_easy_setopt (curl_handle, CURLOPT_HTTPHEADER, headers);

		curl_easy_setopt (curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

		curl_easy_setopt (curl_handle, CURLOPT_POSTFIELDS, query);
		res = curl_easy_perform (curl_handle);

		if (res != CURLE_OK) {
			fprintf (stderr, "curl_easy_perform() failed: %s\n",
			         curl_easy_strerror (res));
		}

		curl_slist_free_all (headers);
		curl_easy_cleanup (curl_handle);
		start_interval_s += interval_s;
		free (query);
		yajl_gen_free (g);
		yajl_free (hand);
	}

	curl_global_cleanup();
	dns_close (&dns_defctx);

	if (file_flag == 1) {
		close_file();
	}

	return retval;
}
