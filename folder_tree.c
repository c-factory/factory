/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    Implementation of the hierarchical folder tree structure
*/

#include "folder_tree.h"
#include "files/path.h"
#include "files/folders.h"
#include <dirent.h>

folder_tree_t *create_folder_tree()
{
    return (folder_tree_t*)create_tree_map((void*)compare_strings);
}

folder_tree_t *create_folder_subtree(folder_tree_t *tree, string_t *subfolder_name)
{
    folder_tree_entry_t *entry = get_entry_from_folder_tree(tree, subfolder_name);
    if (entry)
        return entry->subfolders;
    folder_tree_t *subtree = create_folder_tree();
    add_pair_to_tree_map(&tree->base, duplicate_string(*subfolder_name), subtree);
    return subtree;
}

void destroy_folder_tree(folder_tree_t *tree)
{
    destroy_tree_map_and_content(&tree->base, free, (void*)destroy_folder_tree);
}

folder_tree_entry_t * get_entry_from_folder_tree(folder_tree_t *tree, string_t *folder_name)
{
    return (folder_tree_entry_t*)get_pair_from_tree_map(&tree->base, folder_name);
}

static void add_subfolder_to_subtree(folder_tree_t *tree, strings_list_t *list, size_t index)
{
    if (index == list->size)
        return;

    string_t *subfolder = list->items[index];
    folder_tree_entry_t *entry = get_entry_from_folder_tree(tree, subfolder);
    if (entry)
    {
        add_subfolder_to_subtree(entry->subfolders, list, index + 1);
    }
    else
    {
        folder_tree_t *subtree = create_folder_tree();
        add_pair_to_tree_map(&tree->base, duplicate_string(*subfolder), subtree);
        add_subfolder_to_subtree(subtree, list, index + 1);
    }
}

void add_folder_to_tree(folder_tree_t *tree, string_t *path)
{
    strings_list_t *list = split_string(*path, path_separator);
    add_subfolder_to_subtree(tree, list, 0);
    destroy_strings_list(list);
}

bool make_folders(string_t root, folder_tree_t *tree)
{
    if (!folder_exists(root.data))
    {
        if (0 != mkdir(root.data))
            return false;
    }
    map_iterator_t *iter = create_iterator_from_tree_map(&tree->base);
    bool no_error = true;
    while(has_next_pair(iter) && no_error)
    {
        folder_tree_entry_t *entry = (folder_tree_entry_t*)next_pair(iter);
        string_t *subfolder_name = create_formatted_string("%S%c%S", root, path_separator, *entry->name);
        no_error = make_folders(*subfolder_name, entry->subfolders);
        free(subfolder_name);
    }
    destroy_map_iterator(iter);
    return no_error;
}
