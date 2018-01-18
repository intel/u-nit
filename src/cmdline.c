/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "cmdline.h"
#include "lexer.h"
#include "log.h"
#include "macros.h"

static bool add_env(const char *env, struct cmdline_contents *contents)
{
	int i;
	bool result = false;

	for (i = 0; i < ARRAY_SIZE(contents->env) - 1; i++) {
		if (contents->env[i] == NULL) {
			contents->env[i] = env;
			result = true;
			break;
		}
	}

	return result;
}

static bool add_arg(const char *arg, struct cmdline_contents *contents)
{
	int i;
	bool result = false;

	for (i = 0; i < ARRAY_SIZE(contents->args) - 1; i++) {
		if (contents->args[i] == NULL) {
			contents->args[i] = arg;
			result = true;
			break;
		}
	}

	return result;
}

bool parse_cmdline(const char *cmdline, struct cmdline_contents *contents)
{
	struct lexer_data lexer;
	enum token_result tr;
	char *token, *dup_cmdline = NULL;

	assert(cmdline);
	assert(contents);

	dup_cmdline = strdup(cmdline);
	if (!dup_cmdline) {
		log_message("Could not parse command line '%s': %m\n", cmdline);
		goto end;
	}

	/* cmdline is expected to be of the form:
	 * [<environ>...]<path-to-exec>[<arg>...]
	 * where:
	 * <environ>: <key>=<value>
	 * <key>: Unquoted string
	 * All remaining definitions can be quoted strings (or partially
	 * quoted, as in alfa="beta gama"). Note that quotes can be
	 * of form '<string>' or "<string>", and that <string> may have
	 * the non-starting quote, as in 'delta "epsilon"' */

	init_lexer(&lexer, dup_cmdline, strlen(dup_cmdline) + 1);

	/* While quoted token has '=', we assume then as environment variables.
	 * Sorry, no executable with '=' will be started. Is this a problem? */
	while (true) {
		tr = next_token(&lexer, &token, ' ', true, true);
		if (tr == TOKEN_OK) {
			char *equals_pos = strchr(token, '=');
			if (equals_pos != NULL) {
				if (!add_env(token, contents)) {
					log_message("Too many environment "
						    "variables for '%s'!\n",
						    cmdline);
					goto end;
				}

				log_message("Got env: [%s]\n", token);
			} else {
				break; /* Current token doesn't appear to be an
				env var, so it should be program path*/
			}
		} else {
			log_message("Invalid command line '%s'\n", cmdline);
			goto end;
		}
	}

	/* Program name should be first arg */
	log_message("Got program: [%s]\n", token);
	if (!add_arg(token, contents)) {
		log_message("Too many arguments for '%s'!\n", cmdline);
		goto end;
	}

	/* Everything from now on should be arguments */
	while (true) {
		tr = next_token(&lexer, &token, ' ', true, true);
		if (tr == TOKEN_OK) {
			log_message("Got arg: [%s]\n", token);
			if (!add_arg(token, contents)) {
				log_message("Too many arguments for '%s'!\n",
					    cmdline);
				goto end;
			}
		} else if (tr == TOKEN_END) {
			break; /* Finished args */
		} else {
			log_message("Invalid arguments on command line '%s'\n",
				    cmdline);
			goto end;
		}
	}

	contents->_freeable = dup_cmdline;
	return true;

end:
	free(dup_cmdline);
	return false;
}

void free_cmdline_contents(struct cmdline_contents *contents)
{
	assert(contents);

	free(contents->_freeable);
	contents->_freeable = NULL;
}
