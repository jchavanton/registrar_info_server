#include <stdio.h>
#include <stdlib.h>
#include "../include/uthash.h"
#include "../include/jsmn.h"

typedef struct str {
	int len;
	char *s;
} str_t;

typedef struct record {
	str_t data;
	char* aor;
	UT_hash_handle hh;
} record_t;

record_t *records = NULL;

int add_record(record_t *record) {
	HASH_ADD_KEYPTR(hh, records, record->aor, strlen(record->aor), record);
	return 1;
}

record_t* find_record(char *aor) {
	record_t *record;
	HASH_FIND_STR(records, aor, record);
	if (record) {
		printf("aor[%s] found:%s\n", aor, record->data.s);
	} else {
		printf("aor[%s] not found!\n", aor);
	}
}

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
			strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

int parse_line(char *line, int len) {
	if (!len)
		return 0;
	printf("parsing[%d] %s", len, line);
	record_t *record = (record_t*) malloc(sizeof(record_t));
	record->data.len = len;
	record->data.s = (char*) malloc(sizeof(char)*len);
	memcpy(record->data.s, line, len);
	record->data.s[len-1] = '\0';

	int i;
	int r;
	jsmn_parser p;
	jsmntok_t t[128]; /* We expect no more than 128 tokens */
	jsmn_init(&p);
	r = jsmn_parse(&p, record->data.s, record->data.len, t, sizeof(t)/sizeof(t[0]));
	if (r < 0) {
		printf("Failed to parse JSON: %d\n", r);
		return 1;
	}
	/* Assume the top-level element is an object */
	if (r < 1 || t[0].type != JSMN_OBJECT) {
		printf("Object expected\n");
		return 0;
	}
	/* Loop over all keys of the root object */
	for (i = 1; i < r; i++) {
		if (jsoneq(record->data.s, &t[i], "addressOfRecord") == 0) {
			//printf("- AOR: %.*s\n", t[i+1].end-t[i+1].start, record->data.s + t[i+1].start);
			break;
		}
	}
	record->aor = strndup(record->data.s + t[i+1].start, t[i+1].end-t[i+1].start);
	printf("\naor[%s]\n", record->aor);
	add_record(record);
}

int load_data(void) {
	FILE * fp;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	fp = fopen("data/registrations.json", "r");
	if (fp == NULL) {
		printf("[ERROR] canot not open json data file\n");
		return 0;
	}
	while ((read = getline(&line, &len, fp)) != -1) {
		parse_line(line, read);
		//printf("Retrieved line of length %zu :\n", read);
		//printf("%s", line);
	}

	fclose(fp);
	if (line)
		free(line);
	return 1;
}

void main(void) {
	if (!load_data())
		return;
	char *aor = "0155839131f84c669c000100620009";
	find_record(aor);
}
