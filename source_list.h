/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    Definition of the source files list
*/

#pragma once

#include "tree_set.h"
#include "strings.h"

typedef struct project_descriptor_t project_descriptor_t;

typedef struct
{
    project_descriptor_t *project;
    string_t *c_file;
    string_t *obj_file;
} source_descriptor_t;

typedef struct
{
    tree_set_t base;
} source_list_t;

typedef struct
{
    iterator_t base;
} source_list_iterator_t;

source_list_t *create_source_list();
void add_source_to_list(source_list_t *list, project_descriptor_t *project, string_t *c_file, string_t *obj_file);
source_list_iterator_t * create_iterator_from_source_list(source_list_t *list);
bool has_next_source_descriptor(source_list_iterator_t *iter);
source_descriptor_t * get_next_source_descriptor(source_list_iterator_t *iter);
void destroy_source_list_iterator(source_list_iterator_t *iter);
void destroy_source_list(source_list_t *list);
