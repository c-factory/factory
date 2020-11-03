#include "strings/strings.h"
#include "json/json.h"
#include "files/path.h"
#include "files/files.h"
#include "files/folders.h"
#include "tree_set.h"
#include "allocator.h"
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

typedef enum
{
    project_type_application,
    project_type_library
} project_type_t;

typedef struct
{
    wide_string_t  *name;
    string_t       *fixed_name;
    wide_string_t  *description;
    wide_string_t  *author;
    project_type_t  type;
    full_path_t   **sources;
    size_t          sources_count;
} project_descriptor_t;

typedef struct
{
    project_descriptor_t *project;
    string_t *c_file;
    string_t *obj_folder;
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

json_element_t * read_json_from_file(const char *file_name, bool silent_mode);
project_descriptor_t * parse_project_descriptor(json_element_t *root, const char *file_name);
source_list_t *create_source_list();
void add_source_to_list(source_list_t *list, project_descriptor_t *project, string_t *c_file,
                        string_t *obj_folder, string_t *obj_file);
source_list_iterator_t * create_iterator_from_source_list(source_list_t *list);
bool has_next_source_descriptor(source_list_iterator_t *iter);
source_descriptor_t * get_next_source_descriptor(source_list_iterator_t *iter);
void destroy_source_list_iterator(source_list_iterator_t *iter);
void destroy_source_list(source_list_t *list);
void destroy_project_descriptor(project_descriptor_t *project);
bool build_source_list(project_descriptor_t *project, string_t path_prefix, source_list_t *list);
bool create_folder_structure(string_t target, source_list_t *source_list);
void make(string_t target, source_list_t *source_list, string_t *exe_name);

int main(void)
{
    json_element_t *root = read_json_from_file("factory.json", false);
    project_descriptor_t * project = parse_project_descriptor(root, "factory.json");
    destroy_json_element(&root->base);
    source_list_t *source_list = create_source_list();
    build_source_list(project, __S(""), source_list);
    string_t target = __S("debug");
    create_folder_structure(target, source_list);
    make(target, source_list, project->fixed_name);
    destroy_project_descriptor(project);
    destroy_source_list(source_list);
    return 0;
}

json_element_t * read_json_from_file(const char *file_name, bool silent_mode)
{
    string_t *raw_data = read_file_to_string(file_name);
    if (!raw_data)
    {
        if (!silent_mode)
            fprintf(stderr,
                "Can't open file '%s'\n", file_name);
        return NULL;
    }
    wide_string_t *encoded_data = decode_utf8_string(*raw_data);
    free(raw_data);
    if (!encoded_data)
    {
        if (!silent_mode)
            fprintf(stderr,
                "The file '%s' is not encoded by UTF-8\n", file_name);
        return NULL;
    }
    json_error_t json_error;
    json_element_t *result = parse_json_ext(encoded_data, &json_error);
    free(encoded_data);
    if (!result && !silent_mode)
    {
        wide_string_t *error_wstr = json_error_to_string(&json_error);
        string_t *error_str = encode_utf8_string(*error_wstr);
        free(error_wstr);
        fprintf(stderr,
            "The file '%s' can't be parsed, %s\n", 
            file_name, error_str->data);
        free(error_str);
    }
    return result;
}

project_descriptor_t * parse_project_descriptor(json_element_t *root, const char *file_name)
{
    if (root->base.type != json_object)
    {
        fprintf(stderr,
            "'%s', invalid format, expected a JSON object the contains a project descriptor\n", file_name);
        return NULL;
    }

    project_descriptor_t *project = nnalloc(sizeof(project_descriptor_t));
    memset(project, 0, sizeof(project_descriptor_t));

    json_pair_t *elem_name = get_pair_from_json_object(root->data.object, L"name");
    if (!elem_name || elem_name->value->base.type != json_string)
    {
        fprintf(stderr,
            "'%s', the project descriptor does not contain a name\n", file_name);
        goto error;
    }
    project->name = duplicate_wide_string(*elem_name->value->data.string_value);
    project->fixed_name = wide_string_to_string(*project->name, '_', NULL);
    for (size_t i = 0; i < project->fixed_name->length; i++)
    {
        char c = project->fixed_name->data[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            project->fixed_name->data[i] = '_';
    }

    json_pair_t *elem_description = get_pair_from_json_object(root->data.object, L"description");
    if (elem_description && elem_description->value->base.type == json_string)
        project->description = duplicate_wide_string(*elem_description->value->data.string_value);

    json_pair_t *elem_author = get_pair_from_json_object(root->data.object, L"author");
    if (elem_author && elem_author->value->base.type == json_string)
        project->author = duplicate_wide_string(*elem_author->value->data.string_value);

    json_pair_t *elem_type = get_pair_from_json_object(root->data.object, L"type");
    if (elem_type)
    {
        if (elem_type->value->base.type != json_string)
        {
            wide_string_t *elem_wstr = json_element_to_simple_string(&elem_type->value->base);
            string_t *elem_str = encode_utf8_string(*elem_wstr);
            fprintf(stderr,
                "'%s', the project descriptor contains unsupported project type: '%s'\n",
                file_name, elem_str->data);
                free(elem_wstr);
                free(elem_str);
            goto error;
        }
        if (are_wide_strings_equal(*elem_type->value->data.string_value, __W(L"application")))
            project->type = project_type_application;
        else if (are_wide_strings_equal(*elem_type->value->data.string_value, __W(L"library")))
            project->type = project_type_library;
        else
        {
            string_t *type = encode_utf8_string(*elem_type->value->data.string_value);
            fprintf(stderr,
                "'%s', the project descriptor contains unsupported project type: '%s'\n",
                file_name, type->data);
            free(type);
            goto error;
        }
    }
    else
    {
        project->type = project_type_application;
    }

    json_pair_t *elem_sources = get_pair_from_json_object(root->data.object, L"sources");
    if (elem_sources)
    {
        bool bad_file_name = false;
        if (elem_sources->value->base.type == json_string)
        {
            project->sources = nnalloc(sizeof(string_t*) * 1);
            string_t * full_path = wide_string_to_string(*elem_sources->value->data.string_value, '?', &bad_file_name);
            project->sources[0] = split_path(*full_path);
            free(full_path);
            project->sources_count = 1;
        }
        else if (elem_sources->value->base.type == json_array)
        {
            size_t count = elem_sources->value->data.array->count;
            project->sources = nnalloc(sizeof(wide_string_t*) * count);
            for (size_t i = 0; i < count; i++)
            {
                json_element_t *elem_source = get_element_from_json_array(elem_sources->value->data.array, i);
                if  (elem_source && elem_source->base.type == json_string)
                {
                    string_t * full_path = wide_string_to_string(*elem_source->data.string_value, '?', &bad_file_name);
                    project->sources[project->sources_count++] = split_path(*full_path);
                    free(full_path);
                }
                if (bad_file_name)
                    break;
            }
        }
        if (bad_file_name)
        {
            fprintf(stderr,
                "'%s', the source file list contains a bad filename\n", file_name);
            goto error;
        }
    }
    if (!project->sources_count)
    {
        fprintf(stderr,
            "'%s', the project descriptor does not contain a list of source files\n", file_name);
        goto error;
    }
    
    return project;

error:
    destroy_project_descriptor(project);
    return NULL;
}

void destroy_project_descriptor(project_descriptor_t *project)
{
    free(project->name);
    free(project->fixed_name);
    free(project->description);
    free(project->author);
    for (size_t i = 0; i < project->sources_count; i++)
        destroy_full_path(project->sources[i]);
    free(project->sources);
    free(project);
}

static int compare_source_descriptors(source_descriptor_t *first, source_descriptor_t *second)
{
    return compare_strings(first->c_file, second->c_file);
}

static void destroy_source_descriptor(source_descriptor_t *source)
{
    free(source->c_file);
    free(source->obj_folder);
    free(source->obj_file);
    free(source);
}

source_list_t *create_source_list()
{
    return (source_list_t*)create_tree_set((void*)compare_source_descriptors);
}

void add_source_to_list(source_list_t *list, project_descriptor_t *project, string_t *c_file,
                        string_t *obj_folder, string_t *obj_file)
{
    source_descriptor_t *source = nnalloc(sizeof(source_descriptor_t));
    source->project = project;
    source->c_file = c_file;
    source->obj_folder = obj_folder;
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

static string_t * create_c_file_name(string_t path_prefix, string_t *path, string_t *file_name)
{
    string_builder_t *c_name = NULL;
    if (path_prefix.length > 0 && !are_strings_equal(path_prefix, __S(".")))
        c_name = append_formatted_string(NULL, "%S%c", path_prefix, path_separator);
    if (path->length > 0 && !are_strings_equal(*path, __S(".")))
        c_name = append_formatted_string(c_name, "%S%c", *path, path_separator);
    c_name = append_string(c_name, *file_name);
    return (string_t*)c_name;
}

static string_t * create_obj_file_name(string_t *path, string_t *short_c_name)
{
    const string_t obj_extension = __S("o");
    string_builder_t *obj_name = NULL;
    file_name_t *fn = split_file_name(*short_c_name);
    if (path->length > 0 && !are_strings_equal(*path, __S(".")))
        obj_name = append_formatted_string(NULL, "%S%c", *path, path_separator);
    obj_name = append_formatted_string(obj_name, "%S.%S",
        (fn->extension->length == 0 || are_strings_equal(*fn->extension, __S("c"))) ? *fn->name : *short_c_name,
        obj_extension);
    destroy_file_name(fn);
    return (string_t*)obj_name;
}

bool build_source_list(project_descriptor_t *project, string_t path_prefix, source_list_t *list)
{
    for (size_t i = 0; i < project->sources_count; i++)
    {
        full_path_t *fp = project->sources[i];
        if (index_of_char_in_string(*fp->file_name, '*') == fp->file_name->length)
        {
            string_t *c_file = create_c_file_name(path_prefix, fp->path, fp->file_name); 
            string_t *obj_file = create_obj_file_name(fp->path, fp->file_name);
            add_source_to_list(list, project, c_file, duplicate_string(*fp->path), obj_file);
            if (!file_exists(c_file->data))
            {
                fprintf(stderr, "File '%s' not found\n", c_file->data);
                return false;
            }
        }
        else
        {
            file_name_template_t *tmpl = create_file_name_template(*fp->file_name);
            DIR *dir = opendir(fp->path->data);
            struct dirent *dent;
            if(dir != NULL)
            {
                while((dent = readdir(dir)) != NULL)
                {
                    string_t file_name = _S(dent->d_name);
                    if (file_name_matches_template(file_name, tmpl))
                    {
                        string_t *c_file = create_c_file_name(path_prefix, fp->path, &file_name);
                        string_t *obj_file = create_obj_file_name(fp->path, &file_name);
                        add_source_to_list(list, project, c_file, duplicate_string(*fp->path), obj_file);
                    }
                }
                closedir(dir);
            }
            destroy_file_name_template(tmpl);
        }
    }
    return true;
}

bool create_folder_structure(string_t target, source_list_t *source_list)
{
    bool result = true;
    string_t *target_folder = create_formatted_string("build%c%S", path_separator, target);
    bool create_target_folder = false;
    if (!folder_exists("build"))
    {
        if (0 != mkdir("build"))
            goto error;
        create_target_folder = true;
    }
    else
    {
        if (!folder_exists(target_folder->data))
            create_target_folder = true;
    }

    if (create_target_folder)
    {
        if (0 != mkdir(target_folder->data))
            goto error;
    }

    source_list_iterator_t *iter = create_iterator_from_source_list(source_list);
    while(has_next_source_descriptor(iter))
    {
        source_descriptor_t *source = get_next_source_descriptor(iter);
        if (source->obj_folder->length > 0 && !are_strings_equal(*source->obj_folder, __S(".")))
        {
            string_t *obj_folder = create_formatted_string("%S%c%S", *target_folder, path_separator, *source->obj_folder);
            if (!folder_exists(obj_folder->data))
            {
                if (0!= mkdir(obj_folder->data))
                {
                    free(obj_folder);
                    goto error;
                }
            }
            free(obj_folder);
        }
    }
    destroy_source_list_iterator(iter);

    goto cleanup;

error:
    result = false;

cleanup:
    free(target_folder);
    return result;
}

void make(string_t target, source_list_t *source_list, string_t *exe_name)
{
    const string_t exe_extension = __S(".exe"); 
    source_list_iterator_t *iter = create_iterator_from_source_list(source_list);
    string_builder_t *obj_file_list = NULL;
    while(has_next_source_descriptor(iter))
    {
        source_descriptor_t *source = get_next_source_descriptor(iter);
        string_t *obj_file = create_formatted_string("build%c%S%c%S", path_separator, target, path_separator, *source->obj_file);
        if (obj_file_list)
            obj_file_list = append_char(obj_file_list, ' ');
        obj_file_list = append_string(obj_file_list, *obj_file);
        string_t *cmd = create_formatted_string("gcc %S -c -g -Werror -o %S", 
            *source->c_file, *obj_file);
        printf("%s\n", cmd->data);
        system(cmd->data);
        free(cmd);
        free(obj_file);
    }
    destroy_source_list_iterator(iter);
    string_t *cmd_link = create_formatted_string("gcc %S -o build%c%S%c%S%S",
        *((string_t*)obj_file_list), path_separator, target, path_separator, *exe_name, exe_extension);
    free(obj_file_list);
    printf("%s\n", cmd_link->data);
    system(cmd_link->data);
    free(cmd_link);
}
