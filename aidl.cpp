/*
 *   Copyright 2012 Remiel.C.Lee
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "aidl_language.h"
#include "options.h"
#include "search_path.h"
#include "Type.h"
#include "generate_java.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>

#ifdef HAVE_MS_C_RUNTIME
#include <io.h>
#include <sys/stat.h>
#endif

#ifndef O_BINARY
#  define O_BINARY  0
#endif

using namespace std;

static void
test_document(document_item_type* d)
{
    while (d) {
        if (d->item_type == INTERFACE_TYPE_BINDER) {
            interface_type* c = (interface_type*)d;
            printf("interface %s %s {\n", c->package, c->name.data);
            interface_item_type *q = (interface_item_type*)c->interface_items;
            while (q) {
                if (q->item_type == METHOD_TYPE) {
                    method_type *m = (method_type*)q;
                    printf("  %s %s(", m->type.type.data, m->name.data);
                    arg_type *p = m->args;
                    while (p) {
                        printf("%s %s",p->type.type.data,p->name.data);
                        if (p->next) printf(", ");
                        p=p->next;
                    }
                    printf(")");
                    printf(";\n");
                }
                q=q->next;
            }
            printf("}\n");
        }
        else if (d->item_type == USER_DATA_TYPE) {
            user_data_type* b = (user_data_type*)d;
            if ((b->flattening_methods & PARCELABLE_DATA) != 0) {
                printf("parcelable %s %s;\n", b->package, b->name.data);
            }
            if ((b->flattening_methods & RPC_DATA) != 0) {
                printf("flattenable %s %s;\n", b->package, b->name.data);
            }
        }
        else {
            printf("UNKNOWN d=0x%08lx d->item_type=%d\n", (long)d, d->item_type);
        }
        d = d->next;
    }
}

// ==========================================================
int
convert_direction(const char* direction)
{
    if (direction == NULL) {
        return IN_PARAMETER;
    }
    if (0 == strcmp(direction, "in")) {
        return IN_PARAMETER;
    }
    if (0 == strcmp(direction, "out")) {
        return OUT_PARAMETER;
    }
    return INOUT_PARAMETER;
}

// ==========================================================
struct import_info {
    const char* from;
    const char* filename;
    buffer_type statement;
    const char* neededClass;
    document_item_type* doc;
    struct import_info* next;
};

document_item_type* g_document = NULL;
static map<string, import_info*> import_info_map;

static void
main_document_parsed(document_item_type* d)
{
    g_document = d;
}

static void
main_import_parsed(buffer_type* statement)
{
    import_info* import = (import_info*)malloc(sizeof(import_info));
    memset(import, 0, sizeof(import_info));
    import->from = strdup(g_currentFilename);
    import->statement.lineno = statement->lineno;
    import->statement.data = strdup(statement->data);
    import->statement.extra = NULL;
    import->next = import_info_map[g_currentFilename];
    import->neededClass = parse_import_statement(statement->data);
    import_info_map[g_currentFilename] = import;
}

static ParserCallbacks g_mainCallbacks = {
    &main_document_parsed,
    &main_import_parsed
};

char*
parse_import_statement(const char* text)
{
    const char* end;
    int len;

    while (isspace(*text)) {
        text++;
    }
    while (!isspace(*text)) {
        text++;
    }
    while (isspace(*text)) {
        text++;
    }
    end = text;
    while (!isspace(*end) && *end != ';') {
        end++;
    }
    len = end-text;

    char* rv = (char*)malloc(len+1);
    memcpy(rv, text, len);
    rv[len] = '\0';

    return rv;
}

// ==========================================================
static void
import_import_parsed(buffer_type* statement)
{
}

static ParserCallbacks g_importCallbacks = {
    &main_document_parsed,
    &import_import_parsed
};

// ==========================================================
static int
check_filename(const char* filename, const char* package, buffer_type* name)
{
    const char* p;
    string expected;
    string fn;
    size_t len;
    char cwd[MAXPATHLEN];
    bool valid = false;

#ifdef HAVE_WINDOWS_PATHS
    if (isalpha(filename[0]) && filename[1] == ':'
        && filename[2] == OS_PATH_SEPARATOR) {
#else
    if (filename[0] == OS_PATH_SEPARATOR) {
#endif
        fn = filename;
    } else {
        fn = getcwd(cwd, sizeof(cwd));
        len = fn.length();
        if (fn[len-1] != OS_PATH_SEPARATOR) {
            fn += OS_PATH_SEPARATOR;
        }
        fn += filename;
    }

    if (package) {
        expected = package;
        expected += '.';
    }

    len = expected.length();
    for (size_t i=0; i<len; i++) {
        if (expected[i] == '.') {
            expected[i] = OS_PATH_SEPARATOR;
        }
    }

    p = strchr(name->data, '.');
    len = p ? p-name->data : strlen(name->data);
    expected.append(name->data, len);
    
    expected += ".aidl";

    len = fn.length();
    valid = (len >= expected.length());

    if (valid) {
        p = fn.c_str() + (len - expected.length());

#ifdef HAVE_WINDOWS_PATHS
        if (OS_PATH_SEPARATOR != '/') {
            // Input filename under cygwin most likely has / separators
            // whereas the expected string uses \\ separators. Adjust
            // them accordingly.
          for (char *c = const_cast<char *>(p); *c; ++c) {
                if (*c == '/') *c = OS_PATH_SEPARATOR;
            }
        }
#endif

#ifdef OS_CASE_SENSITIVE
        valid = (expected == p);
#else
        valid = !strcasecmp(expected.c_str(), p);
#endif
    }

    if (!valid) {
        fprintf(stderr, "%s:%d interface %s should be declared in a file"
                " called %s.\n",
                filename, name->lineno, name->data, expected.c_str());
        return 1;
    }

    return 0;
}

static int
check_filenames(const char* filename, document_item_type* items)
{
    int err = 0;
    while (items) {
        if (items->item_type == USER_DATA_TYPE) {
            user_data_type* p = (user_data_type*)items;
            err |= check_filename(filename, p->package, &p->name);
        }
        else if (items->item_type == INTERFACE_TYPE_BINDER
                || items->item_type == INTERFACE_TYPE_RPC) {
            interface_type* c = (interface_type*)items;
            err |= check_filename(filename, c->package, &c->name);
        }
        else {
            fprintf(stderr, "aidl: internal error unkown document type %d.\n",
                        items->item_type);
            return 1;
        }
        items = items->next;
    }
    return err;
}

// ==========================================================
static const char*
kind_to_string(int kind)
{
    switch (kind)
    {
        case Type::INTERFACE:
            return "an interface";
        case Type::USERDATA:
            return "a user data";
        default:
            return "ERROR";
    }
}

static char*
rfind(char* str, char c)
{
    char* p = str + strlen(str) - 1;
    while (p >= str) {
        if (*p == c) {
            return p;
        }
        p--;
    }
    return NULL;
}

static int
gather_types(const char* filename, document_item_type* items)
{
    int err = 0;
    while (items) {
        Type* type;
        if (items->item_type == USER_DATA_TYPE) {
            user_data_type* p = (user_data_type*)items;
            type = new UserDataType(p->package ? p->package : "", p->name.data,
                    false, ((p->flattening_methods & PARCELABLE_DATA) != 0),
                    ((p->flattening_methods & RPC_DATA) != 0), filename, p->name.lineno);
        }
        else if (items->item_type == INTERFACE_TYPE_BINDER
                || items->item_type == INTERFACE_TYPE_RPC) {
            interface_type* c = (interface_type*)items;
            type = new InterfaceType(c->package ? c->package : "",
                            c->name.data, false, c->oneway,
                            filename, c->name.lineno);
        }
        else {
            fprintf(stderr, "aidl: internal error %s:%d\n", __FILE__, __LINE__);
            return 1;
        }

        Type* old = NAMES.Find(type->QualifiedName());
        if (old == NULL) {
            NAMES.Add(type);

            if (items->item_type == INTERFACE_TYPE_BINDER) {
                // for interfaces, also add the stub and proxy types, we don't
                // bother checking these for duplicates, because the parser
                // won't let us do it.
                interface_type* c = (interface_type*)items;

                string name = c->name.data;
                name += ".Stub";
                Type* stub = new Type(c->package ? c->package : "",
                                        name, Type::GENERATED, false, false, false,
                                        filename, c->name.lineno);
                NAMES.Add(stub);

                name = c->name.data;
                name += ".Stub.Proxy";
                Type* proxy = new Type(c->package ? c->package : "",
                                        name, Type::GENERATED, false, false, false,
                                        filename, c->name.lineno);
                NAMES.Add(proxy);
            }
            else if (items->item_type == INTERFACE_TYPE_RPC) {
                // for interfaces, also add the service base type, we don't
                // bother checking these for duplicates, because the parser
                // won't let us do it.
                interface_type* c = (interface_type*)items;

                string name = c->name.data;
                name += ".ServiceBase";
                Type* base = new Type(c->package ? c->package : "",
                                        name, Type::GENERATED, false, false, false,
                                        filename, c->name.lineno);
                NAMES.Add(base);
            }
        } else {
            if (old->Kind() == Type::BUILT_IN) {
                fprintf(stderr, "%s:%d attempt to redefine built in class %s\n",
                            filename, type->DeclLine(),
                            type->QualifiedName().c_str());
                err = 1;
            }
            else if (type->Kind() != old->Kind()) {
                const char* oldKind = kind_to_string(old->Kind());
                const char* newKind = kind_to_string(type->Kind());

                fprintf(stderr, "%s:%d attempt to redefine %s as %s,\n",
                            filename, type->DeclLine(),
                            type->QualifiedName().c_str(), newKind);
                fprintf(stderr, "%s:%d    previously defined here as %s.\n",
                            old->DeclFile().c_str(), old->DeclLine(), oldKind);
                err = 1;
            }
        }

        items = items->next;
    }
    return err;
}

// ==========================================================
static bool
matches_keyword(const char* str)
{
    static const char* KEYWORDS[] = { "abstract", "assert", "boolean", "break",
        "byte", "case", "catch", "char", "class", "const", "continue",
        "default", "do", "double", "else", "enum", "extends", "final",
        "finally", "float", "for", "goto", "if", "implements", "import",
        "instanceof", "int", "interface", "long", "native", "new", "package",
        "private", "protected", "public", "return", "short", "static",
        "strictfp", "super", "switch", "synchronized", "this", "throw",
        "throws", "transient", "try", "void", "volatile", "while",
        "true", "false", "null",
        NULL
    };
    const char** k = KEYWORDS;
    while (*k) {
        if (0 == strcmp(str, *k)) {
            return true;
        }
        k++;
    }
    return false;
}

static int
check_method(const char* filename, int kind, method_type* m)
{
    int err = 0;

    // return type
    Type* returnType = NAMES.Search(m->type.type.data);
    if (returnType == NULL) {
        fprintf(stderr, "%s:%d unknown return type %s\n", filename,
                    m->type.type.lineno, m->type.type.data);
        err = 1;
        return err;
    }

    if (returnType == EVENT_FAKE_TYPE) {
        if (kind != INTERFACE_TYPE_RPC) {
            fprintf(stderr, "%s:%d event methods only supported for rpc interfaces\n",
                    filename, m->type.type.lineno);
            err = 1;
        }
    } else {
        if (!(kind == INTERFACE_TYPE_BINDER ? returnType->CanWriteToParcel()
                    : returnType->CanWriteToRpcData())) {
            fprintf(stderr, "%s:%d return type %s can't be marshalled.\n", filename,
                        m->type.type.lineno, m->type.type.data);
            err = 1;
        }
    }

    if (m->type.dimension > 0 && !returnType->CanBeArray()) {
        fprintf(stderr, "%s:%d return type %s%s can't be an array.\n", filename,
                m->type.array_token.lineno, m->type.type.data,
                m->type.array_token.data);
        err = 1;
    }

    if (m->type.dimension > 1) {
        fprintf(stderr, "%s:%d return type %s%s only one"
                " dimensional arrays are supported\n", filename,
                m->type.array_token.lineno, m->type.type.data,
                m->type.array_token.data);
        err = 1;
    }

    int index = 1;

    arg_type* arg = m->args;
    while (arg) {
        Type* t = NAMES.Search(arg->type.type.data);

        // check the arg type
        if (t == NULL) {
            fprintf(stderr, "%s:%d parameter %s (%d) unknown type %s\n",
                    filename, m->type.type.lineno, arg->name.data, index,
                    arg->type.type.data);
            err = 1;
            goto next;
        }

        if (t == EVENT_FAKE_TYPE) {
            fprintf(stderr, "%s:%d parameter %s (%d) event can not be used as a parameter %s\n",
                    filename, m->type.type.lineno, arg->name.data, index,
                    arg->type.type.data);
            err = 1;
            goto next;
        }
        
        if (!(kind == INTERFACE_TYPE_BINDER ? t->CanWriteToParcel() : t->CanWriteToRpcData())) {
            fprintf(stderr, "%s:%d parameter %d: '%s %s' can't be marshalled.\n",
                        filename, m->type.type.lineno, index,
                        arg->type.type.data, arg->name.data);
            err = 1;
        }

        if (returnType == EVENT_FAKE_TYPE
                && convert_direction(arg->direction.data) != IN_PARAMETER) {
            fprintf(stderr, "%s:%d parameter %d: '%s %s' All paremeters on events must be 'in'.\n",
                    filename, m->type.type.lineno, index,
                    arg->type.type.data, arg->name.data);
            err = 1;
            goto next;
        }

        if (arg->direction.data == NULL
                && (arg->type.dimension != 0 || t->CanBeOutParameter())) {
            fprintf(stderr, "%s:%d parameter %d: '%s %s' can be an out"
                                " parameter, so you must declare it as in,"
                                " out or inout.\n",
                        filename, m->type.type.lineno, index,
                        arg->type.type.data, arg->name.data);
            err = 1;
        }

        if (convert_direction(arg->direction.data) != IN_PARAMETER
                && !t->CanBeOutParameter()
                && arg->type.dimension == 0) {
            fprintf(stderr, "%s:%d parameter %d: '%s %s %s' can only be an in"
                            " parameter.\n",
                        filename, m->type.type.lineno, index,
                        arg->direction.data, arg->type.type.data,
                        arg->name.data);
            err = 1;
        }

        if (arg->type.dimension > 0 && !t->CanBeArray()) {
            fprintf(stderr, "%s:%d parameter %d: '%s %s%s %s' can't be an"
                    " array.\n", filename,
                    m->type.array_token.lineno, index, arg->direction.data,
                    arg->type.type.data, arg->type.array_token.data,
                    arg->name.data);
            err = 1;
        }

        if (arg->type.dimension > 1) {
            fprintf(stderr, "%s:%d parameter %d: '%s %s%s %s' only one"
                    " dimensional arrays are supported\n", filename,
                    m->type.array_token.lineno, index, arg->direction.data,
                    arg->type.type.data, arg->type.array_token.data,
                    arg->name.data);
            err = 1;
        }

        // check that the name doesn't match a keyword
        if (matches_keyword(arg->name.data)) {
            fprintf(stderr, "%s:%d parameter %d %s is named the same as a"
                    " Java or aidl keyword\n",
                    filename, m->name.lineno, index, arg->name.data);
            err = 1;
        }
        
next:
        index++;
        arg = arg->next;
    }

    return err;
}

// ==========================================================
static int
exactly_one_interface(const char* filename, const document_item_type* items, const Options& options,
                      bool* onlyParcelable)
{
    if (items == NULL) {
        fprintf(stderr, "%s: file does not contain any interfaces\n",
                            filename);
        return 1;
    }

    const document_item_type* next = items->next;
    if (items->next != NULL) {
        int lineno = -1;
        if (next->item_type == INTERFACE_TYPE_BINDER) {
            lineno = ((interface_type*)next)->interface_token.lineno;
        }
        else if (next->item_type == INTERFACE_TYPE_RPC) {
            lineno = ((interface_type*)next)->interface_token.lineno;
        }
        else if (next->item_type == USER_DATA_TYPE) {
            lineno = ((user_data_type*)next)->keyword_token.lineno;
        }
        fprintf(stderr, "%s:%d aidl can only handle one interface per file\n",
                            filename, lineno);
        return 1;
    }

    if (items->item_type == USER_DATA_TYPE) {
        *onlyParcelable = true;
        if (options.failOnParcelable) {
            fprintf(stderr, "%s:%d aidl can only generate code for interfaces, not"
                            " parcelables or flattenables,\n", filename,
                            ((user_data_type*)items)->keyword_token.lineno);
            fprintf(stderr, "%s:%d .aidl files that only declare parcelables or flattenables"
                            "may not go in the Makefile.\n", filename,
                            ((user_data_type*)items)->keyword_token.lineno);
            return 1;
        }
    } else {
        *onlyParcelable = false;
    }

    return 0;
}

// ==========================================================
void
generate_dep_file(const Options& options, const document_item_type* items)
{
    /* we open the file in binary mode to ensure that the same output is
     * generated on all platforms !!
     */
    FILE* to = NULL;
    if (options.autoDepFile) {
        string fileName = options.outputFileName + ".d";
        to = fopen(fileName.c_str(), "wb");
    } else {
        to = fopen(options.depFileName.c_str(), "wb");
    }

    if (to == NULL) {
        return;
    }

    const char* slash = "\\";
    if (import_info_map.empty()) {
        slash = "";
    }

    if (items->item_type == INTERFACE_TYPE_BINDER || items->item_type == INTERFACE_TYPE_RPC) {
        fprintf(to, "%s: \\\n", options.outputFileName.c_str());
    } else {
        // parcelable: there's no output file.
        fprintf(to, " : \\\n");
    }
    fprintf(to, "  %s %s\n", options.inputFileName.c_str(), slash);

    map<string, import_info*>::iterator it;
    for (it = import_info_map.begin();it != import_info_map.end();) {
        import_info* import = (*it).second;
        ++it;
        bool isFinal = (it == import_info_map.end());
        while (import) {
            if (isFinal && import->next == NULL) {
                slash = "";
            }
            if (import->filename) {
                fprintf(to, "  %s %s\n", import->filename, slash);
            }
            import = import->next;
        }
    }

    fprintf(to, "\n");

    fclose(to);
}

// ==========================================================
static string
generate_outputFileName2(const Options& options, const buffer_type& name, const char* package)
{
    string result;

    // create the path to the destination folder based on the
    // interface package name
    result = options.outputBaseFolder;
    result += OS_PATH_SEPARATOR;

    string packageStr = package;
    size_t len = packageStr.length();
    for (size_t i=0; i<len; i++) {
        if (packageStr[i] == '.') {
            packageStr[i] = OS_PATH_SEPARATOR;
        }
    }

    result += packageStr;

    // add the filename by replacing the .aidl extension to .java
    const char* p = strchr(name.data, '.');
    len = p ? p-name.data : strlen(name.data);

    result += OS_PATH_SEPARATOR;
    result.append(name.data, len);
    result += ".java";

    return result;
}

// ==========================================================
static string
generate_outputFileName(const Options& options, const document_item_type* items)
{
    // items has already been checked to have only one interface.
    if (items->item_type == INTERFACE_TYPE_BINDER || items->item_type == INTERFACE_TYPE_RPC) {
        interface_type* type = (interface_type*)items;

        return generate_outputFileName2(options, type->name, type->package);
    } else if (items->item_type == USER_DATA_TYPE) {
        user_data_type* type = (user_data_type*)items;
        return generate_outputFileName2(options, type->name, type->package);
    }

    // I don't think we can come here, but safer than returning NULL.
    string result;
    return result;
}



// ==========================================================
static void
check_outputFilePath(const string& path) {
    size_t len = path.length();
    for (size_t i=0; i<len ; i++) {
        if (path[i] == OS_PATH_SEPARATOR) {
            string p = path.substr(0, i);
            if (access(path.data(), F_OK) != 0) {
#ifdef HAVE_MS_C_RUNTIME
                _mkdir(p.data());
#else
                mkdir(p.data(), S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP);
#endif
            }
        }
    }
}


// ==========================================================
static int
parse_preprocessed_file(const string& filename)
{
    int err;

    FILE* f = fopen(filename.c_str(), "rb");
    if (f == NULL) {
        fprintf(stderr, "aidl: can't open preprocessed file: %s\n",
                filename.c_str());
        return 1;
    }

    int lineno = 1;
    char line[1024];
    char type[1024];
    char fullname[1024];
    while (fgets(line, sizeof(line), f)) {
        // skip comments and empty lines
        if (!line[0] || strncmp(line, "//", 2) == 0) {
          continue;
        }

        sscanf(line, "%s %[^; \r\n\t];", type, fullname);

        char* packagename;
        char* classname = rfind(fullname, '.');
        if (classname != NULL) {
            *classname = '\0';
            classname++;
            packagename = fullname;
        } else {
            classname = fullname;
            packagename = NULL;
        }

        //printf("%s:%d:...%s...%s...%s...\n", filename.c_str(), lineno,
        //        type, packagename, classname);
        document_item_type* doc;
        
        if (0 == strcmp("parcelable", type)) {
            user_data_type* parcl = (user_data_type*)malloc(
                    sizeof(user_data_type));
            memset(parcl, 0, sizeof(user_data_type));
            parcl->document_item.item_type = USER_DATA_TYPE;
            parcl->keyword_token.lineno = lineno;
            parcl->keyword_token.data = strdup(type);
            parcl->package = packagename ? strdup(packagename) : NULL;
            parcl->name.lineno = lineno;
            parcl->name.data = strdup(classname);
            parcl->semicolon_token.lineno = lineno;
            parcl->semicolon_token.data = strdup(";");
            parcl->flattening_methods = PARCELABLE_DATA;
            doc = (document_item_type*)parcl;
        }
        else if (0 == strcmp("flattenable", type)) {
            user_data_type* parcl = (user_data_type*)malloc(
                    sizeof(user_data_type));
            memset(parcl, 0, sizeof(user_data_type));
            parcl->document_item.item_type = USER_DATA_TYPE;
            parcl->keyword_token.lineno = lineno;
            parcl->keyword_token.data = strdup(type);
            parcl->package = packagename ? strdup(packagename) : NULL;
            parcl->name.lineno = lineno;
            parcl->name.data = strdup(classname);
            parcl->semicolon_token.lineno = lineno;
            parcl->semicolon_token.data = strdup(";");
            parcl->flattening_methods = RPC_DATA;
            doc = (document_item_type*)parcl;
        }
        else if (0 == strcmp("interface", type)) {
            interface_type* iface = (interface_type*)malloc(
                    sizeof(interface_type));
            memset(iface, 0, sizeof(interface_type));
            iface->document_item.item_type = INTERFACE_TYPE_BINDER;
            iface->interface_token.lineno = lineno;
            iface->interface_token.data = strdup(type);
            iface->package = packagename ? strdup(packagename) : NULL;
            iface->name.lineno = lineno;
            iface->name.data = strdup(classname);
            iface->open_brace_token.lineno = lineno;
            iface->open_brace_token.data = strdup("{");
            iface->close_brace_token.lineno = lineno;
            iface->close_brace_token.data = strdup("}");
            doc = (document_item_type*)iface;
        }
        else {
            fprintf(stderr, "%s:%d: bad type in line: %s\n",
                    filename.c_str(), lineno, line);
            return 1;
        }
        err = gather_types(filename.c_str(), doc);
        lineno++;
    }

    if (!feof(f)) {
        fprintf(stderr, "%s:%d: error reading file, line to long.\n",
                filename.c_str(), lineno);
        return 1;
    }

    fclose(f);
    return 0;
}
// ==========================================================
static map<string, document_item_type*> file_import_map;
static map<string, document_item_type*> name_import_map;

static int
parse_imports()
{
    int err = 0;
    g_callbacks = &g_mainCallbacks;
    bool new_import = 1;
    while (new_import) {
        new_import = 0;
        map<string, import_info*>::iterator it;
        for (it = import_info_map.begin();it != import_info_map.end();++it) {
            import_info* import = (*it).second;
            while (import) {
                if (NAMES.Find(import->neededClass) == NULL && import->filename == NULL) {
                    import->filename = find_import_file(import->neededClass);
                    if (!import->filename) {
                        fprintf(stderr, "%s:%d: couldn't find import for class %s\n",
                                import->from, import->statement.lineno,
                                import->neededClass);
                        err |= 1;
                    } else {
                        // DO NOT parse parsed class again
                        if (file_import_map.find(import->filename) == file_import_map.end()) {
                            err |= parse_aidl(import->filename);
                            import->doc = g_document;
                            if (import->doc == NULL) {
                                err |= 1;
                            } else {
                                file_import_map[import->filename] = import->doc;
                                char *name = strrchr(import->neededClass, '.');
                                if (name)
                                    name_import_map[name + 1] = import->doc;
                                else
                                    name_import_map[import->neededClass] = import->doc;
                                new_import = 1;
                            }
                        }
                    }
                }
                import = import->next;
            }
        }
    }
    
#if 0
    map<string, import_info*>::iterator it;
    for (it = import_info_map.begin();it != import_info_map.end();++it) {
        import_info* import = (*it).second;
        while (import) {
            printf("parse_imports filename = %s\n", import->filename);
            import = import->next;
        }
    }
#endif
    return err;
}

// ==========================================================
static map<string, method_type*> origin_method_names;
static map<string, method_type*> ext_method_names;
static vector<const char*> interface_stack;
static int
gather_method_from_parent(const char* origin_filename, const char *child, ext_interface_type *ext)
{
    Type* type = NAMES.Search(ext->name.data);
    map<string, document_item_type*>::iterator it = file_import_map.find(type->DeclFile());
    if (it != name_import_map.end()) {
        document_item_type* items = (*it).second;
        int err = 0;
        while (items) {
            // (nothing to check for USER_DATA_TYPE)
            if (items->item_type == INTERFACE_TYPE_BINDER
                    || items->item_type == INTERFACE_TYPE_RPC) {
                interface_type* c = (interface_type*)items;
                int N = interface_stack.size();
                for (int i = 0;i < N;++i) {
#if 0
    printf("stack = %s\n", interface_stack[i]);
#endif
                    if (!strcmp(c->name.data, interface_stack[i])) {
                        // check circular reference
                        fprintf(stderr, "There exsits a circular reference! Please see \"%s\" and \"%s\"\n", child, ext->name.data);
                        return 1;
                    }
                }
                interface_stack.push_back(c->name.data);
                if (strcmp(c->name.data, ext->name.data)) {
                    items = items->next;
                    continue;
                }

                interface_item_type* member = c->interface_items;
                while (member) {
                    if (member->item_type == METHOD_TYPE) {
                        method_type* m = (method_type*)member;

                        err |= check_method(type->DeclFile().c_str(), items->item_type, m);
                        if (err) return err;

                        // prevent duplicate methods
                        bool origin = (origin_method_names.find(m->name.data) == origin_method_names.end());
                        bool ext = (ext_method_names.find(m->name.data) == ext_method_names.end());
                        if (origin && ext) {
                            ext_method_names[m->name.data] = m;
                        } else {
                            if (!origin) {
                                method_type* origin_method = origin_method_names[m->name.data];
                                fprintf(stderr,"%s:%d    attempt to redefine method %s,\n",
                                    origin_filename, origin_method->name.lineno, origin_method->name.data);
                                fprintf(stderr, "%s:%d    previously defined here.\n",
                                    type->DeclFile().c_str(), m->name.lineno);
                            }
                            if (!ext) {
                                fprintf(stderr, "%s:%d    method %s has been duplicate defined in the ext interface. Please check!\n",
                                    type->DeclFile().c_str(), m->name.lineno, m->name.data);
                            }
                            err = 1;
                        }
                    }
                    member = member->next;
                }
                ext_interface_type *ext_interface = c->ext_interfaces;
                while (ext_interface) {
                    err |= gather_method_from_parent(origin_filename, c->name.data, ext_interface);
                    if (err) return err;
                    ext_interface = ext_interface->next;
                }
                interface_stack.pop_back();
            }
            items = items->next;
        }
    } else {
        fprintf(stderr, "Can't find the data about: %s\n", type->DeclFile().c_str());
        return 1;
    }
    return 0;
}

static int
gather_method(const char* filename, document_item_type* items)
{
    int err = 0;
    while (items) {
        // (nothing to check for USER_DATA_TYPE)
        if (items->item_type == INTERFACE_TYPE_BINDER
                || items->item_type == INTERFACE_TYPE_RPC) {
            interface_type* c = (interface_type*)items;
            interface_stack.push_back(c->name.data);

            interface_item_type* member = c->interface_items;
            while (member) {
                if (member->item_type == METHOD_TYPE) {
                    method_type* m = (method_type*)member;

                    err |= check_method(filename, items->item_type, m);
                    if (err) return err;

                    // prevent duplicate methods
                    if (origin_method_names.find(m->name.data) == origin_method_names.end()) {
                        origin_method_names[m->name.data] = m;
                    } else {
                        fprintf(stderr,"%s:%d    attempt to redefine method %s,\n",
                            filename, m->name.lineno, m->name.data);
                        method_type* old = origin_method_names[m->name.data];
                        fprintf(stderr, "%s:%d    previously defined here.\n",
                            filename, old->name.lineno);
                        err = 1;
                    }
                }
                member = member->next;
            }
            ext_interface_type *ext_interface = c->ext_interfaces;
            while (ext_interface) {
                err |= gather_method_from_parent(filename, c->name.data, ext_interface);
                if (err) return err;
                ext_interface = ext_interface->next;
            }
            interface_stack.pop_back();
        }
        items = items->next;
    }
#if 0
    printf("Check origin method now: \n");
    map<string, method_type*>::iterator check_iterator;
    for (check_iterator = origin_method_names.begin();check_iterator != origin_method_names.end();++check_iterator) {
        printf("method: %s\n", (*check_iterator).first.c_str());
    }
    printf("end\n");
    printf("Check extend method now: \n");
    for (check_iterator = ext_method_names.begin();check_iterator != ext_method_names.end();++check_iterator) {
        printf("+method: %s\n", (*check_iterator).first.c_str());
    }
    printf("end\n");
#endif
    return 0;
}

// ==========================================================
static int
compile_aidl(Options& options)
{
    int err = 0, N;
    map<string, document_item_type*>::iterator import_iterator;
    document_item_type* import_doc;

    set_import_paths(options.importPaths);

    register_base_types();

    // import the preprocessed file
    N = options.preprocessedFiles.size();
    for (int i=0; i<N; i++) {
        const string& s = options.preprocessedFiles[i];
        err |= parse_preprocessed_file(s);
    }
    if (err != 0) {
        return err;
    }

    // parse the main file
    g_callbacks = &g_mainCallbacks;
    err = parse_aidl(options.inputFileName.c_str());
    document_item_type* mainDoc = g_document;
    g_document = NULL;

    // parse the imports
    err |= parse_imports();
    // from now on, use file_import_map instead of import_info_map

    // bail out now if parsing wasn't successful
    if (err != 0 || mainDoc == NULL) {
        //fprintf(stderr, "aidl: parsing failed, stopping.\n");
        return 1;
    }

    // complain about ones that aren't in the right files
    err |= check_filenames(options.inputFileName.c_str(), mainDoc);
    for (import_iterator = file_import_map.begin();import_iterator != file_import_map.end();++import_iterator) {
        const char *import_filename = (*import_iterator).first.c_str();
        import_doc = (*import_iterator).second;
        err |= check_filenames(import_filename, import_doc);
    }

    // gather the types that have been declared
    err |= gather_types(options.inputFileName.c_str(), mainDoc);
    
    for (import_iterator = file_import_map.begin();import_iterator != file_import_map.end();++import_iterator) {
        const char *import_filename = (*import_iterator).first.c_str();
        import_doc = (*import_iterator).second;
        err |= gather_types(import_filename, import_doc);
    }

#if 0
    printf("---- main doc ----\n");
    test_document(mainDoc);

    for (import_iterator = file_import_map.begin();import_iterator != file_import_map.end();++import_iterator) {
        import_doc = (*import_iterator).second;
        printf("---- import doc ----\n");
        test_document(import_doc);
    }
    NAMES.Dump();
#endif

    // check the referenced types in mainDoc to make sure we've imported them
    const char *inputFileName = options.inputFileName.c_str();
    err |= gather_method(inputFileName, mainDoc);
    /*
    err |= check_types(inputFileName, inputFileName, mainDoc);
    for (import_iterator = file_import_map.begin();import_iterator != file_import_map.end();++import_iterator) {
        const char *import_filename = (*import_iterator).first.c_str();
        import_doc = (*import_iterator).second;
        err |= check_types(inputFileName, import_filename, import_doc);
    }
    */

    // finally, there really only needs to be one thing in mainDoc, and it
    // needs to be an interface.
    bool onlyParcelable = false;
    err |= exactly_one_interface(options.inputFileName.c_str(), mainDoc, options, &onlyParcelable);

    // after this, there shouldn't be any more errors because of the
    // input.
    if (err != 0 || mainDoc == NULL) {
        return 1;
    }

    // if needed, generate the outputFileName from the outputBaseFolder
    if (options.outputFileName.length() == 0 &&
            options.outputBaseFolder.length() > 0) {
        options.outputFileName = generate_outputFileName(options, mainDoc);
    }

    // if we were asked to, generate a make dependency file
    // unless it's a parcelable *and* it's supposed to fail on parcelable
    if ((options.autoDepFile || options.depFileName != "") &&
            !(onlyParcelable && options.failOnParcelable)) {
        // make sure the folders of the output file all exists
        check_outputFilePath(options.outputFileName);
        generate_dep_file(options, mainDoc);
    }

    // they didn't ask to fail on parcelables, so just exit quietly.
    if (onlyParcelable && !options.failOnParcelable) {
        return 0;
    }

    // make sure the folders of the output file all exists
    check_outputFilePath(options.outputFileName);

    map<string, method_type*> method_names[2] = {origin_method_names, ext_method_names};
    err = generate_java(options.outputFileName, options.inputFileName.c_str(),
                        (interface_type*)mainDoc, method_names);

    return err;
}

static int
preprocess_aidl(const Options& options)
{
    vector<string> lines;
    int err;

    // read files
    int N = options.filesToPreprocess.size();
    for (int i=0; i<N; i++) {
        g_callbacks = &g_mainCallbacks;
        err = parse_aidl(options.filesToPreprocess[i].c_str());
        if (err != 0) {
            return err;
        }
        document_item_type* doc = g_document;
        string line;
        if (doc->item_type == USER_DATA_TYPE) {
            user_data_type* parcelable = (user_data_type*)doc;
            if ((parcelable->flattening_methods & PARCELABLE_DATA) != 0) {
                line = "parcelable ";
            }
            if ((parcelable->flattening_methods & RPC_DATA) != 0) {
                line = "flattenable ";
            }
            if (parcelable->package) {
                line += parcelable->package;
                line += '.';
            }
            line += parcelable->name.data;
        } else {
            line = "interface ";
            interface_type* iface = (interface_type*)doc;
            if (iface->package) {
                line += iface->package;
                line += '.';
            }
            line += iface->name.data;
        }
        line += ";\n";
        lines.push_back(line);
    }

    // write preprocessed file
    int fd = open( options.outputFileName.c_str(), 
                   O_RDWR|O_CREAT|O_TRUNC|O_BINARY,
#ifdef HAVE_MS_C_RUNTIME
                   _S_IREAD|_S_IWRITE);
#else    
                   S_IRUSR|S_IWUSR|S_IRGRP);
#endif            
    if (fd == -1) {
        fprintf(stderr, "aidl: could not open file for write: %s\n",
                options.outputFileName.c_str());
        return 1;
    }

    N = lines.size();
    for (int i=0; i<N; i++) {
        const string& s = lines[i];
        int len = s.length();
        if (len != write(fd, s.c_str(), len)) {
            fprintf(stderr, "aidl: error writing to file %s\n",
                options.outputFileName.c_str());
            close(fd);
            unlink(options.outputFileName.c_str());
            return 1;
        }
    }

    close(fd);
    return 0;
}

// ==========================================================
int
main(int argc, const char **argv)
{
    Options options;
    int result = parse_options(argc, argv, &options);
    if (result) {
        return result;
    }

    switch (options.task)
    {
        case COMPILE_AIDL:
            return compile_aidl(options);
        case PREPROCESS_AIDL:
            return preprocess_aidl(options);
    }
    fprintf(stderr, "aidl: internal error\n");
    return 1;
}
