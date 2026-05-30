// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_ARGS_H
#define EQUISTREAKAPI_ARGS_H

#include <stdbool.h>

struct config;

struct args {
	bool show_help;
	bool show_version;
	bool silent;
	bool debug;
	bool print_config;
};

int args_parse(int argc, char **argv, struct args *out, struct config *cfg);

void args_print_help(void);
void args_print_version(void);

#endif
