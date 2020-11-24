/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    Definition of the list of standard libraries
*/

#pragma once

#include "strings.h"

typedef enum
{
    l_threads,
    l_math,
    l_sockets,
    l_unknown
} stdlib_t;

stdlib_t parse_stdlib_name(const wide_string_t *name);
