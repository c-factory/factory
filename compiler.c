/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    The structure that defines compiler
*/

#include "compiler.h"
#include "path.h"
#include "stdlib_names.h"

static string_t * create_include_files_list_for_gcc(vector_t *list)
{
    string_builder_t *result = NULL;
    for (size_t i = 0; i < list->size; i++)
    {
        if (i)
            result = append_char(result, ' ');
        result = append_formatted_string(result, "-I%S", *((string_t*)list->data[i]));
    }
    return (string_t*)result;
}

static string_t * create_cmd_line_compile_for_gcc_debug(string_t *c_file, string_t *h_files, string_t *obj_file)
{
    if (h_files)
        return create_formatted_string("gcc %S -c -g -Werror %S -o %S", *c_file, *h_files, *obj_file);
    return create_formatted_string("gcc %S -c -g -Werror -o %S", *c_file, *obj_file);
}

static string_t * create_cmd_line_compile_for_gcc_release(string_t *c_file, string_t *h_files, string_t *obj_file)
{
    if (h_files)
        return create_formatted_string("gcc %S -c -O3 -Werror %S -o %S", *c_file, *h_files, *obj_file);
    return create_formatted_string("gcc %S -c -O3 -Werror -o %S", *c_file, *obj_file);
}

char *gcc_stdlib_names[] = 
{
    "pthread",
    "m",
#ifdef _WIN32
    "ws2_32"
#else
    NULL
#endif
};

static string_t * create_cmd_line_link_for_gcc(string_t *target_folder, vector_t *object_file_list,
     long int stdlib_mask, string_t *exe_file)
{
    string_builder_t *obj_files = NULL;
    for (size_t i = 0; i < object_file_list->size; i++)
    {
        if (i)
            obj_files = append_char(obj_files, ' ');
        obj_files = append_formatted_string(obj_files, "%S%c%S", 
            *target_folder, path_separator, *((string_t*)object_file_list->data[i]));
    }
    string_t *cmd;
    if (stdlib_mask == 0)
    {
        cmd = create_formatted_string("gcc %S -o %S%c%S",
            *((string_t*)obj_files), *target_folder, path_separator, *exe_file);
    }
    else
    {
        string_builder_t *libraries = create_string_builder(0);
        for (size_t j = 0; j < l_unknown; j++)
        {
            if (stdlib_mask & (1 << j))
            {
                char *lib = gcc_stdlib_names[j];
                if (lib)
                    libraries = append_formatted_string(libraries, " -l%s", lib);
            }
        }
        cmd = create_formatted_string("gcc %S%S -o %S%c%S",
            *((string_t*)obj_files), *((string_t*)libraries), *target_folder, path_separator, *exe_file);
        free(libraries);
    }
    free(obj_files);
    return cmd;
}

static const compiler_t gcc_debug =
{
    create_include_files_list_for_gcc,
    create_cmd_line_compile_for_gcc_debug,
    create_cmd_line_link_for_gcc
};

static const compiler_t gcc_release = 
{
    create_include_files_list_for_gcc,
    create_cmd_line_compile_for_gcc_release,
    create_cmd_line_link_for_gcc
};

const compiler_t * get_appropriate_compiler(string_t target)
{
    if (are_strings_equal(target, __S("debug")))
        return &gcc_debug;
    return &gcc_release;
}
