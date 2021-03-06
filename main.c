#include "strings.h"
#include "json.h"
#include "path.h"
#include "files.h"
#include "folders.h"
#include "vector.h"
#include "tree_map.h"
#include "allocator.h"
#include "tree.h"
#include "tree_traversal.h"

#include "source_list.h"
#include "folder_tree.h"
#include "stdlib_names.h"
#include "compiler.h"

#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <assert.h>

const string_t build_folder_name = { "build", 5 };
const string_t ext_folder_name = { "ext", 3 };
const string_t exe_extension =
#ifdef _WIN32
	{".exe", 4 };
#else
        {".bin", 4 };
#endif
const string_t obj_extension = { ".o", 2 };

typedef struct project_descriptor_t project_descriptor_t;

typedef enum
{
    project_type_application,
    project_type_library
} project_type_t;

struct project_descriptor_t
{
    tree_node_t                base;
    wide_string_t             *name;
    string_t                  *fixed_name;
    wide_string_t             *description;
    wide_string_t             *author;
    project_type_t             type;
    struct
    {
        full_path_t          **list;
        size_t                 count;
    } sources;
    struct
    {
        string_t             **list;
        size_t                 count;
    } headers;
    string_t                  *path;
    struct
    {
        project_descriptor_t **list;
        size_t                 count;
    } depends;
    struct
    {
        string_t             **list;
        size_t                 count;
    } url;
    long int                   stdlib_mask;
    bool                       unresolved;
};

typedef struct
{
    project_descriptor_t *project;
    source_list_t *source_list;
    vector_t *header_list;
    long int stdlib_mask;
} project_build_info_t;

json_element_t * read_json_from_file(const char *file_name, bool silent_mode);
project_descriptor_t * parse_project_descriptor(json_element_t *root, const char *file_name, tree_map_t *all_projects,
    bool is_root, bool is_temporary);
project_descriptor_t * get_first_unresolved_project(project_descriptor_t *root_project);
bool resolve_dependencies(project_descriptor_t *project, tree_map_t *all_projects);
bool resolve_dependencies(project_descriptor_t *project, tree_map_t *all_projects);
void destroy_project_descriptor(project_descriptor_t *project);
bool make_target(string_t target, tree_traversal_result_t * sorted_project_list);
source_list_t * build_source_list(project_descriptor_t *project, vector_t *object_file_list, folder_tree_t *folder_tree);
vector_t * build_header_list(project_descriptor_t *project, long int *stdlib_mask);
project_build_info_t *calculate_project_build_info(project_descriptor_t *project,
        vector_t *object_file_list, folder_tree_t *folder_tree);
void destroy_project_build_info(project_build_info_t *info);
void make_project(const compiler_t *compiler, string_t *target_folder, project_build_info_t *info, vector_t *object_file_list);

static string_t * make_path_2(string_t first_part, string_t second_part)
{
    size_t length = first_part.length + second_part.length + 1;
    string_t *path = nnalloc(sizeof(string_t) + length + 1);
    path->data = (char*)(path + 1);
    path->length = length;
    memcpy(path->data, first_part.data, first_part.length);
    path->data[first_part.length] = path_separator;
    memcpy(path->data + first_part.length + 1, second_part.data, second_part.length);
    path->data[length] = '\0';
    return path;
}

int main(void)
{
    json_element_t *root = read_json_from_file("factory.json", false);
    if (!root)
        return -1;
    tree_map_t *all_projects = create_tree_map((void*)compare_wide_strings);
    project_descriptor_t * root_project = parse_project_descriptor(root, "factory.json", all_projects, true, false);
    destroy_json_element(&root->base);
    if (!root_project)
        goto cleanup;

    project_descriptor_t * unresolved_project = get_first_unresolved_project(root_project);
    while (unresolved_project)
    {
        if (!resolve_dependencies(unresolved_project, all_projects))
        {
            fprintf(stderr,
                "The project '%s' contains unresolved dependencies\n", unresolved_project->fixed_name->data);
            goto cleanup;
        }
        unresolved_project = get_first_unresolved_project(root_project);
    }

    tree_traversal_result_t * sorted_project_list = topological_sort(&root_project->base);
    make_target(__S("debug"), sorted_project_list);
    make_target(__S("release"), sorted_project_list);
    destroy_tree_traversal_result(sorted_project_list);

cleanup:
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
    return project->depends.count;
}

tree_node_t * get_child_of_project_descriptor(const tree_node_t *iface, size_t index)
{
    const project_descriptor_t *project = (const project_descriptor_t*)iface;
    return index < project->depends.count ? (tree_node_t*)project->depends.list[index] : NULL;
}

static const tree_node_vtbl_t project_descriptor_vtbl =
{
    get_number_of_project_descriptor_children,
    get_child_of_project_descriptor
};

project_descriptor_t * parse_project_descriptor(json_element_t *root, const char *file_name, tree_map_t *all_projects,
    bool is_root, bool is_temporary)
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

    if (!is_temporary)
    {
        const pair_t *record = get_pair_from_tree_map(all_projects, project_name);
        if (record)
            return (project_descriptor_t*)record->value;
    }

    project_descriptor_t *project = nnalloc(sizeof(project_descriptor_t));
    memset(project, 0, sizeof(project_descriptor_t));
    project->base.vtbl = &project_descriptor_vtbl;
    project->name = duplicate_wide_string(*project_name);
    if (!is_temporary)
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
            project->sources.list = nnalloc(sizeof(full_path_t*) * 1);
            string_t * full_path = wide_string_to_string(*elem_sources->value->data.string_value, '?', &bad_file_name);
            project->sources.list[0] = split_path(*full_path);
            free(full_path);
            project->sources.count = 1;
        }
        else if (elem_sources->value->base.type == json_array)
        {
            size_t count = elem_sources->value->data.array->count;
            project->sources.list = nnalloc(sizeof(full_path_t*) * count);
            for (size_t i = 0; i < count; i++)
            {
                json_element_t *elem_source = get_element_from_json_array(elem_sources->value->data.array, i);
                if  (elem_source && elem_source->base.type == json_string)
                {
                    string_t * full_path = wide_string_to_string(*elem_source->data.string_value, '?', &bad_file_name);
                    project->sources.list[project->sources.count++] = split_path(*full_path);
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
            project->headers.list = nnalloc(sizeof(string_t*) * 1);
            string_t * header_path = wide_string_to_string(*elem_headers->value->data.string_value, '?', &bad_folder_name);
            fix_path_separators(header_path->data);
            project->headers.list[0] = header_path;
            project->headers.count = 1;
        }
        else if (elem_headers->value->base.type == json_array)
        {
            size_t count = elem_headers->value->data.array->count;
            project->headers.list = nnalloc(sizeof(string_t*) * count);
            for (size_t i = 0; i < count; i++)
            {
                json_element_t *elem_header = get_element_from_json_array(elem_headers->value->data.array, i);
                if  (elem_header && elem_header->base.type == json_string)
                {
                    string_t * header_path = wide_string_to_string(*elem_header->data.string_value, '?', &bad_folder_name);
                    fix_path_separators(header_path->data);
                    project->headers.list[project->headers.count++] = header_path;
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
        project->depends.list = nnalloc(sizeof(project_descriptor_t*) * count);
        project->depends.count = count;
        for (size_t i = 0; i < count; i++)
        {
            json_element_t *elem_dependency = get_element_from_json_array(elem_depends->value->data.array, i);
            project_descriptor_t *other_project = parse_project_descriptor(elem_dependency, file_name, all_projects, false, false);
            if (!other_project)
                goto error;
            project->depends.list[i] = other_project;
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

    json_pair_t *elem_url = get_pair_from_json_object(root->data.object, L"url");
    if (elem_url)
    {
        bool bad_url = false;
        if (elem_url->value->base.type == json_string)
        {
            project->url.list = nnalloc(sizeof(string_t*) * 1);
            string_t * url = wide_string_to_string(*elem_url->value->data.string_value, '#', &bad_url);
            project->url.list[0] = url;
            project->url.count = 1;
        }
        else if (elem_url->value->base.type == json_array)
        {
            size_t count = elem_url->value->data.array->count;
            project->url.list = nnalloc(sizeof(string_t*) * count);
            for (size_t i = 0; i < count; i++)
            {
                json_element_t *elem_one_url = get_element_from_json_array(elem_url->value->data.array, i);
                if  (elem_one_url && elem_one_url->base.type == json_string)
                {
                    string_t * url = wide_string_to_string(*elem_one_url->data.string_value, '?', &bad_url);
                    project->url.list[project->url.count++] = url;
                }
                if (bad_url)
                    break;
            }
        }
        if (bad_url)
        {
            fprintf(stderr,
                "'%s', the project URL is incorrect\n", file_name);
            goto error;
        }
    }

    json_pair_t *elem_stdlib = get_pair_from_json_object(root->data.object, L"stdlib");
    if (elem_stdlib)
    {
        const wide_string_t *unknown_name_wide = NULL;
        if (elem_stdlib->value->base.type == json_string)
        {
            stdlib_t lib = parse_stdlib_name(elem_stdlib->value->data.string_value);
            if (lib == l_unknown)
                unknown_name_wide = elem_stdlib->value->data.string_value;
            else
                project->stdlib_mask = 1 << lib;
        }
        else if (elem_stdlib->value->base.type == json_array)
        {
            size_t count = elem_stdlib->value->data.array->count;
            for (size_t i = 0; i < count; i++)
            {
                json_element_t *elem_stdlib_item = get_element_from_json_array(elem_stdlib->value->data.array, i);
                if  (elem_stdlib_item && elem_stdlib_item->base.type == json_string)
                {
                    stdlib_t lib = parse_stdlib_name(elem_stdlib_item->data.string_value);
                    if (lib == l_unknown)
                        unknown_name_wide = elem_stdlib_item->data.string_value;
                    else
                        project->stdlib_mask |= 1 << lib;
                }
                if (unknown_name_wide)
                    break;
            }
        }
        if (unknown_name_wide)
        {
            string_t *unknown_name = wide_string_to_string(*unknown_name_wide, '?', NULL);
            fprintf(stderr,
                "'%s', unknown standard library name: '%s'\n", file_name, unknown_name->data);
            free(unknown_name);
            goto error;
        }
    }
    
    if (!project->path)
    {
        if (is_root)
            project->path = duplicate_string(__S("."));
        else
            project->unresolved = true;        
    }

    if (!project->sources.count)
    {
        if (is_root || project->path)
        {
            fprintf(stderr,
                "'%s', the project descriptor does not contain a list of source files\n", file_name);
            goto error;
        }
        else
        {
            project->unresolved = true;        
        }        
    }

    if (project->type == project_type_library && !project->headers.count)
    {
        if (project->path)
        {
            fprintf(stderr,
                "'%s', the library project descriptor does not contain a list of headers\n", file_name);
            goto error;
        }
        else
        {
            project->unresolved = true;        
        }        
    }

    return project;

error:
    return NULL;
}

project_descriptor_t * get_first_unresolved_project(project_descriptor_t *root_project)
{
    if (root_project->unresolved)
        return root_project;

    for (size_t i = 0; i < root_project->depends.count; i++)
    {
        project_descriptor_t *project = get_first_unresolved_project(root_project->depends.list[i]);
        if (project)
            return project;
    }
    return NULL;
}

bool resolve_dependencies(project_descriptor_t *project, tree_map_t *all_projects)
{
    if (!project->unresolved)
        return true;

    bool result = true;
    bool need_to_download = false;
    string_t *factory_json_path = NULL;

    if (!project->path)
    {
        if (!project->url.count)
        {
            fprintf(stderr,
                "The project '%s' contains no URL where to download it\n", project->fixed_name->data);
            return false;
        }

        bool no_folder = false;
        if (!folder_exists(ext_folder_name.data))
        {
            need_to_download = true;
            no_folder = true;
            if (!make_folder(ext_folder_name.data))
            {
                fprintf(stderr,
                    "Couldn't create folder '%s'\n", ext_folder_name.data);
                return false;
            }
        }
        project->path = make_path_2(ext_folder_name, *project->fixed_name);
        bool folder_is_empty = false;
        if (no_folder || !folder_exists(project->path->data))
        {
            need_to_download = true;
            folder_is_empty = true;
            if (!make_folder(project->path->data))
            {
                fprintf(stderr,
                    "Couldn't create folder '%s'\n", project->path->data);
                return false;
            }
        }
        if (folder_is_empty || !folder_exists_and_not_empty(project->path->data))
        {
            need_to_download = true;
        }
    }

    assert(project->path != NULL);

    if (need_to_download)
    {
        bool downloaded = false;
        printf("\n> Downloading project '%s'...\n", project->fixed_name->data);
        for (size_t i = 0; i < project->url.count; i++)
        {
            string_t *cmd = create_formatted_string("git clone %S %S", *project->url.list[i], *project->path);
            printf("%s\n", cmd->data);
            int exec_result = system(cmd->data);
            free(cmd);
            if (exec_result == 0)
            {
                downloaded = true;
                break;
            }
        }
        if (!downloaded) {
            fprintf(stderr,
                "Couldn't download sources of the project '%s'\n", project->fixed_name->data);
            goto error;
        }
    }

    if (project->headers.count == 0)
    {
        factory_json_path = make_path_2(*project->path, __S("factory.json"));
        json_element_t *root = read_json_from_file(factory_json_path->data, false);
        if (!root)
            goto error;            
        project_descriptor_t * tmp_proj = parse_project_descriptor(root, factory_json_path->data, all_projects, true, true);
        destroy_json_element(&root->base);
        if (!tmp_proj)
            goto error;

        project->sources = tmp_proj->sources;
        tmp_proj->sources.list = NULL;
        tmp_proj->sources.count = 0;

        project->headers = tmp_proj->headers;
        tmp_proj->headers.list = NULL;
        tmp_proj->headers.count = 0;

        project->depends = tmp_proj->depends;
        tmp_proj->depends.list = NULL;
        tmp_proj->depends.count = 0;

        project->stdlib_mask = tmp_proj->stdlib_mask;

        destroy_project_descriptor(tmp_proj);
    }

    assert(project->headers.count > 0);
    goto cleanup;

error:
    result = false;

cleanup:
    free(factory_json_path);

    project->unresolved = !result;
    return result;
}

void destroy_project_descriptor(project_descriptor_t *project)
{
    free(project->name);
    free(project->fixed_name);
    free(project->description);
    free(project->author);
    for (size_t i = 0; i < project->sources.count; i++)
        destroy_full_path(project->sources.list[i]);
    free(project->sources.list);
    for (size_t i = 0; i < project->headers.count; i++)
        free(project->headers.list[i]);
    free(project->headers.list);
    free(project->path);
    free(project->depends.list);
    for (size_t i = 0; i < project->url.count; i++)
        free(project->url.list[i]);
    free(project->url.list);
    free(project);
}

bool make_target(string_t target, tree_traversal_result_t * sorted_project_list)
{
    printf("\n> Making target '%s'...\n", target.data);
    size_t count = sorted_project_list->count;
    vector_t *object_file_list = create_vector();
    folder_tree_t *build_folder = create_folder_tree();
    folder_tree_t *target_folder = create_folder_subtree(build_folder, &target);

    vector_t *full_build_info = create_vector_ext(get_system_allocator(), count);
    for (size_t i = 0; i < count; i++)
    {
        project_descriptor_t *project = (project_descriptor_t*)sorted_project_list->list[count - i - 1];
        project_build_info_t *info = calculate_project_build_info(project, object_file_list, target_folder);
        add_item_to_vector(full_build_info, info);
    } 

    make_folders(build_folder_name, build_folder);
    string_t *target_folder_path = make_path_2(build_folder_name, target);
    const compiler_t *compiler = get_appropriate_compiler(target);
    for (size_t i = 0; i < count; i++)
    {
        make_project(compiler, target_folder_path, (project_build_info_t*)full_build_info->data[i], object_file_list);
    }

    destroy_vector_and_content(object_file_list, free);
    free(target_folder_path);
    destroy_folder_tree(build_folder);
    destroy_vector_and_content(full_build_info, (void*)destroy_project_build_info);
    return true;
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

static string_t * create_obj_file_name(string_t *project_name, string_t *path, string_t *short_c_name)
{
    file_name_t *fn = split_file_name(*short_c_name);
    string_builder_t *obj_name = append_formatted_string(NULL, "%S%c", *project_name, path_separator);
    if (path->length > 0 && !are_strings_equal(*path, __S(".")))
        obj_name = append_formatted_string(obj_name, "%S%c", *path, path_separator);
    obj_name = append_formatted_string(obj_name, "%S%S",
        (fn->extension->length == 0 || are_strings_equal(*fn->extension, __S("c"))) ? *fn->name : *short_c_name,
        obj_extension);
    destroy_file_name(fn);
    return (string_t*)obj_name;
}

source_list_t * build_source_list(project_descriptor_t *project, vector_t *object_file_list, folder_tree_t *folder_tree)
{
    source_list_t *source_list = create_source_list();
    folder_tree_t *project_folder = create_folder_subtree(folder_tree, project->fixed_name);
    for (size_t i = 0; i < project->sources.count; i++)
    {
        full_path_t *fp = project->sources.list[i];
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
            string_t *folder_path = make_path_2(*project->path, *fp->path);
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
                string_t *object_files = create_formatted_string("%S%c%S%c*%S",
                    *project->fixed_name, path_separator, *fp->path, path_separator, obj_extension);
                add_item_to_vector(object_file_list, object_files);
            }
        }
    }
    return source_list;
}

static void add_project_headers_to_list(project_descriptor_t *project, vector_t *header_list, long int *stdlib_mask, tree_set_t *visited_projects)
{
    if (is_there_item_in_tree_set(visited_projects, project))
        return;

    add_item_to_tree_set(visited_projects, project);
    *stdlib_mask |= project->stdlib_mask;

    if (project->headers.count)
    {
        if (project->path->length > 0 && !are_strings_equal(*project->path, __S(".")))
        {
            for (size_t i = 0; i < project->headers.count; i++)
            {
                string_t *header = make_path_2(*project->path, *project->headers.list[i]);
                add_item_to_vector(header_list, header);
            }
        }
        else
        {
            for (size_t i = 0; i < project->headers.count; i++)
            {
                add_item_to_vector(header_list, duplicate_string(*project->headers.list[i]));
            }
        }
    }

    for (size_t i = 0; i < project->depends.count; i++)
    {
        add_project_headers_to_list(project->depends.list[i], header_list, stdlib_mask, visited_projects);
    }
}

vector_t * build_header_list(project_descriptor_t *project, long int *stdlib_mask)
{
    vector_t *header_list = create_vector();
    tree_set_t *visited_projects = create_tree_set(NULL);
    add_project_headers_to_list(project, header_list, stdlib_mask, visited_projects);
    destroy_tree_set(visited_projects);
    return header_list;
}

project_build_info_t *calculate_project_build_info(project_descriptor_t *project,
        vector_t *object_file_list, folder_tree_t *folder_tree)
{
    project_build_info_t *info = nnalloc(sizeof(project_build_info_t));
    info->project = project;
    info->source_list = build_source_list(project, object_file_list, folder_tree);
    info->stdlib_mask = 0;
    info->header_list = build_header_list(project, &info->stdlib_mask);
    return info;
}

void destroy_project_build_info(project_build_info_t *info)
{
    destroy_source_list(info->source_list);
    destroy_vector_and_content(info->header_list, free);
    free(info);
}

void make_project(const compiler_t *compiler, string_t *target_folder, project_build_info_t *info, vector_t *object_file_list)
{
    // building
    printf("\n> Building project '%s'...\n", info->project->fixed_name->data);
    string_t *h_files = compiler->create_include_files_list(info->header_list);
    source_list_iterator_t *iter = create_iterator_from_source_list(info->source_list);
    while(has_next_source_descriptor(iter))
    {
        source_descriptor_t *source = get_next_source_descriptor(iter);
        string_t *obj_file = make_path_2(*target_folder, *source->obj_file);
        string_t *cmd = compiler->create_cmd_line_compile(source->c_file, h_files, obj_file);
        printf("%s\n", cmd->data);
        system(cmd->data);
        free(cmd);
        free(obj_file);
    }
    destroy_source_list_iterator(iter);
    free(h_files);

    // linking
    if (info->project->type == project_type_application)
    {
        printf("\n> Linking...\n");
        string_t *exe_file = create_formatted_string("%S%S", *info->project->fixed_name, exe_extension);
        string_t *cmd = compiler->create_cmd_line_link(target_folder, object_file_list, info->stdlib_mask, exe_file);
        printf("%s\n", cmd->data);
        system(cmd->data);
        free(cmd);
        free(exe_file);
    }
}
