/*
    Copyright (c) 2020 Ivan Kniazkov <ivan.kniazkov.com>

    Implementation of the list of standard libraries
*/

#include "stdlib_names.h"

wchar_t *stdlib_names[] = 
{
    L"threads",
    L"math",
    L"sockets"
};

stdlib_t parse_stdlib_name(const wide_string_t *name)
{
    size_t i;
    for (i = 0; i < l_unknown; i++)
    {
        if (0 == wcscmp(name->data, stdlib_names[i]))
            break;
    }
    return (stdlib_t)i;
}
