/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    Implementation of the source files list
*/

#include "source_list.h"

static int compare_source_descriptors(source_descriptor_t *first, source_descriptor_t *second)
{
    return compare_strings(first->c_file, second->c_file);
}

static void destroy_source_descriptor(source_descriptor_t *source)
{
    free(source->c_file);
    free(source->obj_file);
    free(source);
}

source_list_t *create_source_list()
{
    return (source_list_t*)create_tree_set((void*)compare_source_descriptors);
}

void add_source_to_list(source_list_t *list, project_descriptor_t *project, string_t *c_file, string_t *obj_file)
{
    source_descriptor_t *source = nnalloc(sizeof(source_descriptor_t));
    source->project = project;
    source->c_file = c_file;
    source->obj_file = obj_file;
    if (false == add_item_to_tree_set(&list->base, source))
        destroy_source_descriptor(source);
}

source_list_iterator_t * create_iterator_from_source_list(source_list_t *list)
{
    return (source_list_iterator_t*)create_iterator_from_tree_set(&list->base);
}

bool has_next_source_descriptor(source_list_iterator_t *iter)
{
    return has_next_item(&iter->base);
}

source_descriptor_t * get_next_source_descriptor(source_list_iterator_t *iter)
{
    return (source_descriptor_t*)next_item(&iter->base);
}

void destroy_source_list_iterator(source_list_iterator_t *iter)
{
    destroy_iterator(&iter->base);
}

void destroy_source_list(source_list_t *list)
{
    destroy_tree_set_and_content(&list->base, (void*)destroy_source_descriptor);
}