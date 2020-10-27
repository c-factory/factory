#include "strings/strings.h"
#include "json/json.h"
#include "files/path.h"
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
    wide_string_t  *description;
    wide_string_t  *author;
    project_type_t  type;
    full_path_t   **sources;
    size_t          sources_count;
} project_descriptor_t;

json_element_t * read_json_from_file(const char *file_name, bool silent_mode);
project_descriptor_t * parse_project_descriptor(json_element_t *root, const char *file_name);
void destroy_project_descriptor(project_descriptor_t *descr);

int main(void)
{
    json_element_t *root = read_json_from_file("factory.json", false);
    wide_string_t *js_string = json_element_to_simple_string(&root->base);
    project_descriptor_t * descr = parse_project_descriptor(root, "factory.json");
    wprintf(L"%s\n", js_string->data);
    free(js_string);
    destroy_json_element(&root->base);
    destroy_project_descriptor(descr);
    DIR *dir = opendir(".");
    struct dirent *de;
    if(dir != NULL) {
		while((de = readdir(dir)) != NULL)
			printf("%s\n", de->d_name);
		closedir(dir);
	}
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
                "The file '%s' is not encoded by UTF-8", file_name);
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
            "The file '%s' can't be parsed, %s", 
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
            "'%s', invalid format, expected a JSON object the contains a project descriptor", file_name);
        return NULL;
    }

    project_descriptor_t *descr = nnalloc(sizeof(project_descriptor_t));
    memset(descr, 0, sizeof(project_descriptor_t));

    json_pair_t *elem_name = get_pair_from_json_object(root->data.object, L"name");
    if (!elem_name || elem_name->value->base.type != json_string)
    {
        fprintf(stderr,
            "'%s', the project descriptor does not contain a name", file_name);
        goto error;
    }
    descr->name = duplicate_wide_string(*elem_name->value->data.string_value);

    json_pair_t *elem_description = get_pair_from_json_object(root->data.object, L"description");
    if (elem_description && elem_description->value->base.type == json_string)
        descr->description = duplicate_wide_string(*elem_description->value->data.string_value);

    json_pair_t *elem_author = get_pair_from_json_object(root->data.object, L"author");
    if (elem_author && elem_author->value->base.type == json_string)
        descr->author = duplicate_wide_string(*elem_author->value->data.string_value);

    json_pair_t *elem_type = get_pair_from_json_object(root->data.object, L"type");
    if (elem_type)
    {
        if (elem_type->value->base.type != json_string)
        {
            wide_string_t *elem_wstr = json_element_to_simple_string(&elem_type->value->base);
            string_t *elem_str = encode_utf8_string(*elem_wstr);
            fprintf(stderr,
                "'%s', the project descriptor contains unsupported project type: '%s'",
                file_name, elem_str->data);
                free(elem_wstr);
                free(elem_str);
            goto error;
        }
        if (are_wide_strings_equal(*elem_type->value->data.string_value, __W(L"application")))
            descr->type = project_type_application;
        else if (are_wide_strings_equal(*elem_type->value->data.string_value, __W(L"library")))
            descr->type = project_type_library;
        else
        {
            string_t *type = encode_utf8_string(*elem_type->value->data.string_value);
            fprintf(stderr,
                "'%s', the project descriptor contains unsupported project type: '%s'",
                file_name, type->data);
            free(type);
            goto error;
        }
    }
    else
    {
        descr->type = project_type_application;
    }

    json_pair_t *elem_sources = get_pair_from_json_object(root->data.object, L"sources");
    if (elem_sources)
    {
        bool bad_file_name = false;
        if (elem_sources->value->base.type == json_string)
        {
            descr->sources = nnalloc(sizeof(string_t*) * 1);
            string_t * full_path = wide_string_to_string(*elem_sources->value->data.string_value, '?', &bad_file_name);
            descr->sources[0] = split_path(*full_path);
            free(full_path);
            descr->sources_count = 1;
        }
        else if (elem_sources->value->base.type == json_array)
        {
            size_t count = elem_sources->value->data.array->count;
            descr->sources = nnalloc(sizeof(wide_string_t*) * count);
            for (size_t i = 0; i < count; i++)
            {
                json_element_t *elem_source = get_element_from_json_array(elem_sources->value->data.array, i);
                if  (elem_source && elem_source->base.type == json_string)
                {
                    string_t * full_path = wide_string_to_string(*elem_source->data.string_value, '?', &bad_file_name);
                    descr->sources[descr->sources_count++] = split_path(*full_path);
                    free(full_path);
                }
                if (bad_file_name)
                    break;
            }
        }
        if (bad_file_name)
        {
            fprintf(stderr,
                "'%s', the source file list contains a bad filename", file_name);
            goto error;
        }
    }
    if (!descr->sources_count)
    {
        fprintf(stderr,
            "'%s', the project descriptor does not contain a list of source files", file_name);
        goto error;
    }
    
    return descr;

error:
    destroy_project_descriptor(descr);
    return NULL;
}

void destroy_project_descriptor(project_descriptor_t *descr)
{
    free(descr->name);
    free(descr->description);
    free(descr->author);
    for (size_t i = 0; i < descr->sources_count; i++)
        destroy_full_path(descr->sources[i]);
    free(descr->sources);
    free(descr);
}