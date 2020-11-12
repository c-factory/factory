/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    Definition of the hierarchical folder tree structure
*/

#pragma once

#include "tree_map.h"
#include "strings.h"

typedef struct
{
    tree_map_t base;
} folder_tree_t;

typedef struct
{
    string_t *name;
    folder_tree_t *subfolders;
} folder_tree_entry_t;

folder_tree_t *create_folder_tree();
folder_tree_t *create_folder_subtree(folder_tree_t *tree, string_t *subfolder_name);
void destroy_folder_tree(folder_tree_t *tree);
folder_tree_entry_t * get_entry_from_folder_tree(folder_tree_t *tree, string_t *folder_name);
void add_folder_to_tree(folder_tree_t *tree, string_t *path);
bool make_folders(string_t root, folder_tree_t *tree);