#include "strings/strings.h"
#include "json/json.h"
#include "files/path.h"
#include "files/files.h"
#include "files/folders.h"
#include "source_list.h"
#include "vector.h"
#include "tree_map.h"
#include "allocator.h"
#include "folder_tree.h"
#include "graphs/tree.h"
#include "graphs/tree_traversal.h"
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

typedef struct project_descriptor_t project_descriptor_t;

typedef enum
{
    project_type_application,
    project_type_library
} project_type_t;

struct project_descriptor_t
{
    tree_node_t            base;
    wide_string_t         *name;
    string_t              *fixed_name;
    wide_string_t         *description;
    wide_string_t         *author;
    project_type_t         type;
    full_path_t          **sources;
    size_t                 sources_count;
    string_t             **headers;
    size_t                 headers_count;
    string_t              *path;
    project_descriptor_t **depends;
    size_t                 depends_count;
};

typedef struct
{
    project_descriptor_t *project;
    source_list_t *source_list;
    vector_t *header_list;
} project_build_info_t;

json_element_t * read_json_from_file(const char *file_name, bool silent_mode);
project_descriptor_t * parse_project_descriptor(json_element_t *root, const char *file_name, tree_map_t *all_projects, bool is_root);
void destroy_project_descriptor(project_descriptor_t *project);
source_list_t * build_source_list(project_descriptor_t *project, vector_t *object_file_list, folder_tree_t *folder_tree);
vector_t * build_header_list(project_descriptor_t *project);
project_build_info_t *calculate_project_build_info(project_descriptor_t *project,
        vector_t *object_file_list, folder_tree_t *folder_tree);
void destroy_project_build_info(project_build_info_t *info);
void make_project(string_t *target_folder, project_build_info_t *info, vector_t *object_file_list);

int main(void)
{
    json_element_t *root = read_json_from_file("factory.json", false);
    tree_map_t *all_projects = create_tree_map((void*)compare_strings);
    project_descriptor_t * root_project = parse_project_descriptor(root, "factory.json", all_projects, true);
    destroy_json_element(&root->base);

    vector_t *object_file_list = create_vector();

    string_t root_folder_name = __S("build");
    string_t target = __S("debug");
    folder_tree_t *build_folder = create_folder_tree();
    folder_tree_t *target_folder = create_folder_subtree(build_folder, &target);

    tree_traversal_result_t * sorted_project_list = topological_sort(&root_project->base);
    size_t count = sorted_project_list->count;
    vector_t *full_build_info = create_vector_ext(get_system_allocator(), count);
    for (size_t i = 0; i < count; i++)
    {
        project_descriptor_t *project = (project_descriptor_t*)sorted_project_list->list[count - i - 1];
        project_build_info_t *info = calculate_project_build_info(project, object_file_list, target_folder);
        add_item_to_vector(full_build_info, info);
    } 
    destroy_tree_traversal_result(sorted_project_list);

    make_folders(root_folder_name, build_folder);
    string_t *target_folder_path = create_formatted_string("%S%c%S", root_folder_name, path_separator, target);
    for (size_t i = 0; i < count; i++)
    {
        make_project(target_folder_path, (project_build_info_t*)full_build_info->data[i], object_file_list);
    }

    destroy_vector_and_content(object_file_list, free);
    free(target_folder_path);
    destroy_folder_tree(build_folder);
    destroy_vector_and_content(full_build_info, (void*)destroy_project_build_info);
    destroy_tree_map_and_content(all_projects, NULL, (void*)destroy_project_descriptor);
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

size_t get_number_of_project_descriptor_children(const tree_node_t *iface)
{
    const project_descriptor_t *project = (const project_descriptor_t*)iface;
    return project->depends_count;
}

tree_node_t * get_child_of_project_descriptor(const tree_node_t *iface, size_t index)
{
    const project_descriptor_t *project = (const project_descriptor_t*)iface;
    return index < project->depends_count ? (tree_node_t*)project->depends[index] : NULL;
}

static const tree_node_vtbl_t project_descriptor_vtbl =
{
    get_number_of_project_descriptor_children,
    get_child_of_project_descriptor
};

project_descriptor_t * parse_project_descriptor(json_element_t *root, const char *file_name, tree_map_t *all_projects, bool is_root)
{
    if (root->base.type != json_object)
    {
        fprintf(stderr,
            "'%s', invalid format, expected a JSON object the contains a project descriptor\n", file_name);
        return NULL;
    }

    json_pair_t *elem_name = get_pair_from_json_object(root->data.object, L"name");
    if (!elem_name || elem_name->value->base.type != json_string)
    {
        fprintf(stderr,
            "'%s', the project descriptor does not contain a name\n", file_name);
        goto error;
    }
    wide_string_t *project_name = (wide_string_t*)elem_name->value->data.string_value;

    const pair_t *record = get_pair_from_tree_map(all_projects, project_name);
    if (record)
        return (project_descriptor_t*)record->value;

    project_descriptor_t *project = nnalloc(sizeof(project_descriptor_t));
    memset(project, 0, sizeof(project_descriptor_t));
    project->base.vtbl = &project_descriptor_vtbl;
    project->name = duplicate_wide_string(*project_name);
    add_pair_to_tree_map(all_projects, project->name, project);

    project->name = duplicate_wide_string(*project_name);
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
        project->type = is_root ? project_type_application : project_type_library;
    }

    json_pair_t *elem_sources = get_pair_from_json_object(root->data.object, L"sources");
    if (elem_sources)
    {
        bool bad_file_name = false;
        if (elem_sources->value->base.type == json_string)
        {
            project->sources = nnalloc(sizeof(full_path_t*) * 1);
            string_t * full_path = wide_string_to_string(*elem_sources->value->data.string_value, '?', &bad_file_name);
            project->sources[0] = split_path(*full_path);
            free(full_path);
            project->sources_count = 1;
        }
        else if (elem_sources->value->base.type == json_array)
        {
            size_t count = elem_sources->value->data.array->count;
            project->sources = nnalloc(sizeof(full_path_t*) * count);
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
                "'%s', the source files list contains a bad filename\n", file_name);
            goto error;
        }
    }

    json_pair_t *elem_headers = get_pair_from_json_object(root->data.object, L"headers");
    if (elem_headers)
    {
        bool bad_folder_name = false;
        if (elem_headers->value->base.type == json_string)
        {
            project->headers = nnalloc(sizeof(string_t*) * 1);
            string_t * header_path = wide_string_to_string(*elem_headers->value->data.string_value, '?', &bad_folder_name);
            project->headers[0] = header_path;
            project->headers_count = 1;
        }
        else if (elem_headers->value->base.type == json_array)
        {
            size_t count = elem_headers->value->data.array->count;
            project->headers = nnalloc(sizeof(string_t*) * count);
            for (size_t i = 0; i < count; i++)
            {
                json_element_t *elem_header = get_element_from_json_array(elem_headers->value->data.array, i);
                if  (elem_header && elem_header->base.type == json_string)
                {
                    string_t * header_path = wide_string_to_string(*elem_header->data.string_value, '?', &bad_folder_name);
                    project->headers[project->headers_count++] = header_path;
                }
                if (bad_folder_name)
                    break;
            }
        }
        if (bad_folder_name)
        {
            fprintf(stderr,
                "'%s', the headers list contains a bad folder name\n", file_name);
            goto error;
        }
    }

    json_pair_t *elem_depends = get_pair_from_json_object(root->data.object, L"depends");
    if (!elem_depends)
        elem_depends = get_pair_from_json_object(root->data.object, L"dependencies");
    if (elem_depends)
    {
        if (elem_depends->value->base.type != json_array)
        {
            fprintf(stderr,
                "'%s', invalid format, expected a list of dependencies\n", file_name);
            goto error;
        }
        size_t count = elem_depends->value->data.array->count;
        project->depends = nnalloc(sizeof(project_descriptor_t*) * count);
        project->depends_count = count;
        for (size_t i = 0; i < count; i++)
        {
            json_element_t *elem_dependency = get_element_from_json_array(elem_depends->value->data.array, i);
            project_descriptor_t *other_project = parse_project_descriptor(elem_dependency, file_name, all_projects, false);
            if (!other_project)
                goto error;
            project->depends[i] = other_project;
        }
    }

    json_pair_t *elem_path = get_pair_from_json_object(root->data.object, L"path");
    if (elem_path && elem_path->value->base.type == json_string)
    {
        bool bad_folder_name = false;
        project->path = wide_string_to_string(*elem_path->value->data.string_value, '?', &bad_folder_name);
        if (bad_folder_name)
        {
            fprintf(stderr,
                "'%s', the project path is incorrect\n", file_name);
            goto error;
        }
        fix_path_separators(project->path->data);
    }
    if (!project->path && is_root)
    {
        project->path = duplicate_string(__S("."));
    }

    if (!project->sources_count)
    {
        fprintf(stderr,
            "'%s', the project descriptor does not contain a list of source files\n", file_name);
        goto error;
    }

    if (project->type == project_type_library && !project->headers_count)
    {
        fprintf(stderr,
            "'%s', the library project descriptor does not contain a list of headers\n", file_name);
        goto error;
    }

    return project;

error:
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
    for (size_t i = 0; i < project->headers_count; i++)
        free(project->headers[i]);
    free(project->headers);
    free(project->path);
    free(project->depends);
    free(project);
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

const string_t obj_extension = { "o", 1 };

static string_t * create_obj_file_name(string_t *project_name, string_t *path, string_t *short_c_name)
{
    file_name_t *fn = split_file_name(*short_c_name);
    string_builder_t *obj_name = append_formatted_string(NULL, "%S%c", *project_name, path_separator);
    if (path->length > 0 && !are_strings_equal(*path, __S(".")))
        obj_name = append_formatted_string(obj_name, "%S%c", *path, path_separator);
    obj_name = append_formatted_string(obj_name, "%S.%S",
        (fn->extension->length == 0 || are_strings_equal(*fn->extension, __S("c"))) ? *fn->name : *short_c_name,
        obj_extension);
    destroy_file_name(fn);
    return (string_t*)obj_name;
}

source_list_t * build_source_list(project_descriptor_t *project, vector_t *object_file_list, folder_tree_t *folder_tree)
{
    source_list_t *source_list = create_source_list();
    folder_tree_t *project_folder = create_folder_subtree(folder_tree, project->fixed_name);
    for (size_t i = 0; i < project->sources_count; i++)
    {
        full_path_t *fp = project->sources[i];
        if (index_of_char_in_string(*fp->file_name, '*') == fp->file_name->length)
        {
            string_t *c_file = create_c_file_name(*project->path, fp->path, fp->file_name); 
            string_t *obj_file = create_obj_file_name(project->fixed_name, fp->path, fp->file_name);
            add_source_to_list(source_list, project, c_file, obj_file);
            add_item_to_vector(object_file_list, duplicate_string(*obj_file));
            add_folder_to_tree(project_folder, fp->path);
            if (!file_exists(c_file->data))
            {
                fprintf(stderr, "File '%s' not found\n", c_file->data);
                destroy_source_list(source_list);
                return NULL;
            }
        }
        else
        {
            file_name_template_t *tmpl = create_file_name_template(*fp->file_name);
            string_t *folder_path = create_formatted_string("%S%c%S", *project->path, path_separator, *fp->path);
            DIR *dir = opendir(folder_path->data);
            struct dirent *dent;
            bool found_files = false;
            if(dir != NULL)
            {
                while((dent = readdir(dir)) != NULL)
                {
                    string_t file_name = _S(dent->d_name);
                    if (file_name_matches_template(file_name, tmpl))
                    {
                        found_files = true;
                        string_t *c_file = create_c_file_name(*project->path, fp->path, &file_name);
                        string_t *obj_file = create_obj_file_name(project->fixed_name, fp->path, &file_name);
                        add_source_to_list(source_list, project, c_file, obj_file);
                    }
                }
                closedir(dir);
            }
            free(folder_path);
            destroy_file_name_template(tmpl);
            if (found_files)
            {
                add_folder_to_tree(project_folder, fp->path);
                string_t *object_files = create_formatted_string("%S%c%S%c*.%S",
                    *project->fixed_name, path_separator, *fp->path, path_separator, obj_extension);
                add_item_to_vector(object_file_list, object_files);
            }
        }
    }
    return source_list;
}

static void add_project_headers_to_list(project_descriptor_t *project, vector_t *header_list)
{
    if (!project->headers_count)
        return;

    if (project->path->length > 0 && !are_strings_equal(*project->path, __S(".")))
    {
        for (size_t i = 0; i < project->headers_count; i++)
        {
            string_t *header = create_formatted_string("%S%c%S", *project->path, path_separator, *project->headers[i]);
            add_item_to_vector(header_list, header);
        }
    }
    else
    {
        for (size_t i = 0; i < project->headers_count; i++)
        {
            add_item_to_vector(header_list, duplicate_string(*project->headers[i]));
        }
    }
}

vector_t * build_header_list(project_descriptor_t *project)
{
    vector_t *header_list = create_vector();
    add_project_headers_to_list(project, header_list);
    return header_list;
}

project_build_info_t *calculate_project_build_info(project_descriptor_t *project,
        vector_t *object_file_list, folder_tree_t *folder_tree)
{
    project_build_info_t *info = nnalloc(sizeof(project_build_info_t));
    info->project = project;
    info->source_list = build_source_list(project, object_file_list, folder_tree);
    info->header_list = build_header_list(project);
    return info;
}

void destroy_project_build_info(project_build_info_t *info)
{
    destroy_source_list(info->source_list);
    destroy_vector_and_content(info->header_list, free);
    free(info);
}

void make_project(string_t *target_folder, project_build_info_t *info, vector_t *object_file_list)
{
    // headers
    string_builder_t *headers_string = NULL;
    for (size_t i = 0; i < info->header_list->size; i++)
    {
        if (i)
            headers_string = append_char(headers_string, ' ');
        headers_string = append_formatted_string(headers_string, "-I%S", *((string_t*)info->header_list->data[i]));
    }

    // building
    printf("Building project %s...\n", info->project->fixed_name->data);
    source_list_iterator_t *iter = create_iterator_from_source_list(info->source_list);
    while(has_next_source_descriptor(iter))
    {
        source_descriptor_t *source = get_next_source_descriptor(iter);
        string_t *obj_file = create_formatted_string("%S%c%S", *target_folder, path_separator, *source->obj_file);
        string_t *cmd = create_formatted_string("gcc %S -c -g -Werror %S -o %S", 
            *source->c_file, headers_string ? *((string_t*)headers_string) : __S(""), *obj_file);
        printf("%s\n", cmd->data);
        system(cmd->data);
        free(cmd);
        free(obj_file);
    }
    destroy_source_list_iterator(iter);
    free(headers_string);

    // linking
    if (info->project->type == project_type_application)
    {
        printf("Linking...\n");
        const string_t exe_extension = __S(".exe");
        string_builder_t *objects_string = NULL;
        for (size_t i = 0; i < object_file_list->size; i++)
        {
            if (i)
                objects_string = append_char(objects_string, ' ');
            objects_string = append_formatted_string(objects_string, "%S%c%S", 
                *target_folder, path_separator, *((string_t*)object_file_list->data[i]));
        }
        string_t *cmd_link = create_formatted_string("gcc %S -o %S%c%S%S",
            *((string_t*)objects_string), *target_folder, path_separator, *info->project->fixed_name, exe_extension);
        printf("%s\n", cmd_link->data);
        system(cmd_link->data);
        free(cmd_link);
        free(objects_string);
    }
}
