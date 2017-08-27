#include <stdio.h>
#include <stdlib.h>
#include "../include/jsmn.h"
#include "../include/storage.h"
#include "../include/tcp_server.h"

/* storage root */
int record_count;
record_t *records = NULL;

/* add a record the htable storage */
int add_record(record_t *record) {
	HASH_ADD_KEYPTR(hh, records, record->aor, strlen(record->aor), record);
	record_count++;
	return 1;
}

/* find a record in the htable storage */
record_t* find_record(char *aor) {
	record_t *record;
	HASH_FIND_STR(records, aor, record);
	return record;
}

/* find a json value by matching the key */
static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
			strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

/* process on input data line, data file must be formated to have one json record per line */
int process_line(char *line, int len) {
	if (!len)
		return 0;
	++len;
	record_t *record = (record_t*) malloc(sizeof(record_t));
	record->data.len = len;
	record->data.s = (char*) malloc(sizeof(char)*len);
	memcpy(record->data.s, line, len);
	record->data.s[len-1] = '\0';
	record->data.s[len-2] = '\n';

	int i;
	int r;
	jsmn_parser p;
	jsmntok_t t[128]; /* We expect no more than 128 tokens */
	jsmn_init(&p);
	r = jsmn_parse(&p, record->data.s, record->data.len, t, sizeof(t)/sizeof(t[0]));
	if (r < 0) {
		printf("[error] failed to parse JSON: %d\n", r);
		return 0;
	}
	/* Assume the top-level element is an object */
	if (r < 1 || t[0].type != JSMN_OBJECT) {
		printf("[error] object expected\n");
		return 0;
	}
	/* Loop over all keys of the root object */
	for (i = 1; i < r; i++) {
		if (jsoneq(record->data.s, &t[i], "addressOfRecord") == 0)
			break;
	}
	record->aor = strndup(record->data.s + t[i+1].start, t[i+1].end-t[i+1].start);
	add_record(record);
	printf("aor[%d]added[%s]\n", record_count, record->aor);
	return 1;
}

int load_data_from_file(const char *fn) {
	FILE * fp;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	fp = fopen(fn, "r");
	if (fp == NULL) {
		printf("[ERROR] canot not open json data file [%s]\n", fn);
		return 0;
	}
	while ((read = getline(&line, &len, fp)) != -1) {
		if (!process_line(line, read))
			return 0;
	}
	fclose(fp);
	if (line)
		free(line);
	return 1;
}

void main(void) {
	if (!load_data_from_file("data/registrations.json"))
		return;
	char *aor = "0155839131f84c669c000100620009";
	int ret = serve();
	if (ret != 0) {
		printf("[error] tcp server error: %d", ret);
	}
}
