#ifndef STORAGE_FILE_H
#define STORAGE_FILE_H

#include "../include/uthash.h"

/* string */
typedef struct str {
	int len;
	char *s;
} str_t;

/* storage unit */
typedef struct record {
	str_t data;        // JSON data
	char *aor;         // address of record
	UT_hash_handle hh; // htable field
} record_t;

record_t* find_record(char *aor);

#endif
