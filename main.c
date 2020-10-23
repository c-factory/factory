#include "strings/strings.h"
#include "json/json.h"
#include "allocator.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
    wide_string_t *name;
} project_description_t;

json_element_t * read_json_from_file(const char *file_name, bool silent_mode);
project_description_t * parse_project_description(json_element_t *root, const char *file_name);
void destroy_project_description(project_description_t *descr);

int main(void)
{
    json_element_t *root = read_json_from_file("factory.json", false);
    wide_string_t *js_string = json_element_to_simple_string(&root->base);
    project_description_t * descr = parse_project_description(root, "factory.json");
    wprintf(L"%s\n", js_string->data);
    free(js_string);
    destroy_json_element(&root->base);
    destroy_project_description(descr);
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

project_description_t * parse_project_description(json_element_t *root, const char *file_name)
{
    if (root->base.type != json_object)
    {
        fprintf(stderr,
            "'%s', invalid format, expected a JSON object the contains a project description", file_name);
        return NULL;
    }

    project_description_t *descr = nnalloc(sizeof(project_description_t));
    memset(descr, 0, sizeof(project_description_t));

    json_pair_t *elem_name = get_pair_from_json_object(root->data.object, L"name");
    if (!elem_name || elem_name->value->base.type != json_string)
    {
        fprintf(stderr,
            "'%s', the project description does not contain a name", file_name);
        goto error;
    }
    descr->name = duplicate_wide_string(*elem_name->value->data.string_value);

    return descr;

error:
    destroy_project_description(descr);
    return NULL;
}

void destroy_project_description(project_description_t *descr)
{
    free(descr->name);
    free(descr);
}