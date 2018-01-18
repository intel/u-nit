/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "inittab.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "lexer.h"
#include "log.h"

enum inittab_parse_result { RESULT_OK, RESULT_ERROR, RESULT_DONE };

static bool safe_strtoi32_t(const char *s, int32_t *ret)
{
	char *end_ptr = NULL;
	long int l;
	bool result = true;

	errno = 0;
	l = strtol(s, &end_ptr, 10);

	if (((errno != 0) || (*end_ptr != '\0')) ||
	    ((l > INT32_MAX) || (l < INT32_MIN))) {
		result = false;
	}

	if (result) {
		*ret = (int32_t)l;
	}

	return result;
}

static void add_entry_to_list(struct inittab_entry **list,
			      struct inittab_entry *entry)
{
	struct inittab_entry *current;

	assert(list != NULL);
	assert(entry != NULL);

	/* Add item to list ensuring entry->order is respected,
	 * and keeping list stable - or, if a new item with same order
	 * is added, it is kept after all previous items */
	if ((*list == NULL) || ((*list)->order > entry->order)) {
		entry->next = *list;
		*list = entry;
	} else {
		current = *list;
		while ((current->next != NULL) &&
		       (current->next->order <= entry->order)) {
			current = current->next;
		}
		entry->next = current->next;
		current->next = entry;
	}
}

static void debug_inittab_entry_list(struct inittab_entry *list)
{
	if (list == NULL) {
		log_message("\tNULL\n");
	} else {
		struct inittab_entry *current = list;

		while (current != NULL) {
			log_message(
			    "\t[Entry] order: %d, core_id: %d, type: %d, "
			    "controlling-terminal: '%s', process: '%s'\n",
			    current->order, current->core_id, current->type,
			    current->ctty_path, current->process_name);
			current = current->next;
		}
	}
}

static void debug_inittab_entries(struct inittab *inittab_entries)
{
	log_message("STARTUP LIST:\n");
	debug_inittab_entry_list(inittab_entries->startup_list);

	log_message("SHUTDOWN LIST:\n");
	debug_inittab_entry_list(inittab_entries->shutdown_list);

	log_message("SAFE MODE:\n");
	debug_inittab_entry_list(inittab_entries->safe_mode_entry);
}

void free_inittab_entry_list(struct inittab_entry *list)
{
	struct inittab_entry *tmp;

	while (list != NULL) {
		tmp = list;
		list = list->next;
		free(tmp);
	}
}

static bool place_entry(struct inittab_entry *entry,
			struct inittab *inittab_entries)
{
	bool result = true;

	assert(entry != NULL);

	switch (entry->type) {
	case ONE_SHOT:
	case SAFE_ONE_SHOT:
	case SERVICE:
	case SAFE_SERVICE: {
		add_entry_to_list(&inittab_entries->startup_list, entry);
		break;
	}
	case SHUTDOWN:
	case SAFE_SHUTDOWN: {
		add_entry_to_list(&inittab_entries->shutdown_list, entry);
		break;
	}
	case SAFE_MODE: {
		if (inittab_entries->safe_mode_entry != NULL) {
			log_message("Safe process already defined before "
				    "'%.20s'(...)\n",
				    entry->process_name);
			result = false;
			break;
		}
		add_entry_to_list(&inittab_entries->safe_mode_entry, entry);
		break;
	}
	default: {
		/* Should never happen */
		assert(false);
		break;
	}
	}

	return result;
}

static enum inittab_parse_result
inittab_parse_entry(FILE *fp, struct inittab_entry *entry)
{
	char buf[BUFFER_LEN] = {};
	struct lexer_data lexer = {};
	enum inittab_parse_result result = RESULT_OK;
	enum next_line_result next;
	enum token_result tr;

	char *order_str = NULL, *core_id_str = NULL, *type_str = NULL,
	     *process_str = NULL, *ctty_path_str = NULL;

	if ((fp == NULL) || (feof(fp) != 0)) {
		result = RESULT_ERROR;
		goto end;
	}

	next = inittab_next_line(fp, buf);

	if (next == NEXT_LINE_TOO_BIG) {
		log_message("Line too big: '%.20s(...)'\n", buf);
		result = RESULT_ERROR;
		goto end;
	} else if (next == NEXT_LINE_ERROR) {
		log_message("Couldn't read inittab file\n");
		result = RESULT_ERROR;
		goto end;
	} else if (next == NEXT_LINE_EOF) {
		result = RESULT_DONE;
		goto end;
	} else {
		/* Everything is fine */
	}

	/* Set lexer up */
	init_lexer(&lexer, buf, sizeof(buf));

	/* Get <order> */
	tr = next_token(&lexer, &order_str, ':', false, false);
	if (tr == TOKEN_BLANK) {
		entry->order = -1;
	} else if (tr == TOKEN_END) {
		log_message("Invalid 'order' field on inittab entry\n");
		result = RESULT_ERROR;
		goto end;
	} else {
		if (!safe_strtoi32_t(order_str, &entry->order)) {
			log_message("Invalid 'order' field on inittab entry\n");
			result = RESULT_ERROR;
			goto end;
		}
		if (entry->order < 0) {
			log_message("Invalid order 'field' on inittab entry\n");
			result = RESULT_ERROR;
			goto end;
		}
	}

	/* Get <core_id> */
	tr = next_token(&lexer, &core_id_str, ':', false, false);
	if (tr == TOKEN_BLANK) {
		entry->core_id = -1;
	} else if (tr == TOKEN_END) {
		log_message("Invalid 'core_id' field on inittab entry\n");
		result = RESULT_ERROR;
		goto end;
	} else {
		if (!safe_strtoi32_t(core_id_str, &entry->core_id)) {
			log_message(
			    "Invalid 'core_id' field on inittab entry\n");
			result = RESULT_ERROR;
			goto end;
		}
		if (entry->core_id < 0) {
			log_message(
			    "Invalid 'core_id' field on inittab entry\n");
			result = RESULT_ERROR;
			goto end;
		}
	}

	/*Get <type> */
	tr = next_token(&lexer, &type_str, ':', false, false);
	if (tr != TOKEN_OK) {
		log_message("Expected 'type' field on inittab entry\n");
		result = RESULT_ERROR;
		goto end;
	} else {
		if (strcmp(type_str, "<one-shot>") == 0) {
			entry->type = ONE_SHOT;
		} else if (strcmp(type_str, "<safe-one-shot>") == 0) {
			entry->type = SAFE_ONE_SHOT;
		} else if (strcmp(type_str, "<service>") == 0) {
			entry->type = SERVICE;
		} else if (strcmp(type_str, "<safe-service>") == 0) {
			entry->type = SAFE_SERVICE;
		} else if (strcmp(type_str, "<shutdown>") == 0) {
			entry->type = SHUTDOWN;
		} else if (strcmp(type_str, "<safe-shutdown>") == 0) {
			entry->type = SAFE_SHUTDOWN;
		} else if (strcmp(type_str, "<safe-mode>") == 0) {
			entry->type = SAFE_MODE;
		} else {
			log_message(
			    "Invalid 'type' field on inittab entry: %s\n",
			    type_str);
			result = RESULT_ERROR;
			goto end;
		}
	}

	/* Now that we know entry type, check if it has a valid order */
	if ((entry->order == -1) && (entry->type != SAFE_MODE)) {
		log_message("Expected 'order' field on entry with type "
			    "different of '<safe-mode>'\n");
		result = RESULT_ERROR;
		goto end;
	}

	/* Get <controlling-terminal> */
	tr = next_token(&lexer, &ctty_path_str, ':', false, false);
	if ((tr == TOKEN_OK) &&
	    strlen(ctty_path_str) < sizeof(entry->ctty_path)) {
		(void)strcpy(entry->ctty_path, ctty_path_str);
	} else if (tr == TOKEN_BLANK) {
		entry->ctty_path[0] = '\0';
	} else {
		log_message(
		    "Invalid 'controlling-terminal' field on inittab entry\n");
		result = RESULT_ERROR;
		goto end;
	}

	/*Get <process> */
	tr = next_token(&lexer, &process_str, '\0', false, false);
	if (tr != TOKEN_OK) {
		log_message("Expected 'process' field on inittab entry\n");
		result = RESULT_ERROR;
		goto end;
	} else if (strlen(process_str) < sizeof(entry->process_name)) {
		(void)strcpy(entry->process_name, process_str);
	} else {
		log_message("Invalid 'process' field on inittab entry\n");
		result = RESULT_ERROR;
		goto end;
	}

end:
	return result;
}

bool read_inittab(const char *filename, struct inittab *inittab_entries)
{
	FILE *fp = NULL;
	struct inittab_entry *entry;
	enum inittab_parse_result r;
	bool error = false, result = true;

	assert(filename != NULL);
	assert(inittab_entries != NULL);

	errno = 0;
	fp = fopen(filename, "re");
	if (fp == NULL) {
		log_message("Couldn't open inittab file: %m\n");
		result = false;
		goto end;
	}

	log_message("Reading inittab entries...\n");
	while (true) {
		bool exit_loop = false;

		entry = calloc(1, sizeof(struct inittab_entry));
		if (entry == NULL) {
			log_message("Could not allocate memory to parse "
				    "inittab entry\n");
			error = true;
			break;
		}

		r = inittab_parse_entry(fp, entry);
		if (r == RESULT_OK) {
			log_message("[Entry] order: %d, core_id: %d, type: %d, "
				    "controlling-terminal: '%s', process: "
				    "'%s'\n",
				    entry->order, entry->core_id, entry->type,
				    entry->ctty_path, entry->process_name);

			if (!place_entry(entry, inittab_entries)) {
				free(entry);
				error = true;
				exit_loop = true;
			}
		} else if (r == RESULT_ERROR) {
			error = true;
			free(entry);
			/* TODO currently, `inittab_parse_entry` itself prints
			 * error. Maybe it'd better if it returned (via a
			 * pointer arg) information about the error, so caller
			 * print it */
		} else {
			exit_loop = true;
			free(entry);
		}

		if (exit_loop) {
			break;
		}
	}

	if (inittab_entries->safe_mode_entry == NULL) {
		log_message("No <safe-mode> entry on inittab. Can't go on!\n");
		error = true;
		/* TODO is this the right approach? */
	}

	if (error) {
		log_message("Error(s) during inittab parsing. Exiting!\n");
		result = false;

		free_inittab_entry_list(inittab_entries->startup_list);
		free_inittab_entry_list(inittab_entries->shutdown_list);
		free_inittab_entry_list(inittab_entries->safe_mode_entry);
	} else {
		debug_inittab_entries(inittab_entries);
	}

	errno = 0;
	if (fclose(fp) != 0) {
		log_message("Error closing inittab file: %m\n");
	}

end:
	return result;
}
