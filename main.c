#include "strings/strings.h"
#include "json/json.h"
#include <stdlib.h>
#include <stdio.h>

json_element_t * read_json_from_file(const char *file_name, bool silent_mode);

int main(void)
{
    json_element_t *root = read_json_from_file("factory.json", false);
    wide_string_t *js_string = json_element_to_simple_string(&root->base);
    wprintf(L"%s\n", js_string->data);
    free(js_string);
    destroy_json_element(&root->base);
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
