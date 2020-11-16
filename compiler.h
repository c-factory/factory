/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    The structure that defines compiler
*/

#pragma once

#include "strings.h"
#include "vector.h"

typedef struct
{
    string_t * (*create_include_files_list)(vector_t *list);
    string_t * (*create_cmd_line_compile)(string_t *c_file, string_t *h_files, string_t *obj_file);
    string_t * (*create_cmd_line_link)(string_t *target_folder, vector_t *object_file_list, string_t *exe_file);
} compiler_t;

const compiler_t * get_appropriate_compiler(string_t target);
