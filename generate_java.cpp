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

#include "generate_java.h"
#include "Type.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =================================================
VariableFactory::VariableFactory(const string& base)
    :m_base(base),
     m_index(0)
{
}

Variable*
VariableFactory::Get(Type* type)
{
    char name[100];
    sprintf(name, "%s%d", m_base.c_str(), m_index);
    m_index++;
    Variable* v = new Variable(type, name);
    m_vars.push_back(v);
    return v;
}

Variable*
VariableFactory::Get(int index)
{
    return m_vars[index];
}

// =================================================
string
gather_comments(extra_text_type* extra)
{
    string s;
    while (extra) {
        if (extra->which == SHORT_COMMENT) {
            s += extra->data;
        }
        else if (extra->which == LONG_COMMENT) {
            s += "/*";
            s += extra->data;
            s += "*/";
        }
        extra = extra->next;
    }
    return s;
}

string
append(const char* a, const char* b)
{
    string s = a;
    s += b;
    return s;
}

// =================================================
int
generate_java(const string& filename, const string& originalSrc,
                interface_type* iface, map<string, method_type*>* method_map)
{
    Class* cl;

    if (iface->document_item.item_type == INTERFACE_TYPE_BINDER) {
        cl = generate_binder_interface_class(iface, method_map);
    }
    else if (iface->document_item.item_type == INTERFACE_TYPE_RPC) {
        cl = generate_rpc_interface_class(iface);
    }

    Document* document = new Document;
        document->comment = "";
        if (iface->package) document->package = iface->package;
        document->originalSrc = originalSrc;
        document->classes.push_back(cl);

//    printf("outputting... filename=%s\n", filename.c_str());
    FILE* to;
    if (filename == "-") {
        to = stdout;
    } else {
       /* open file in binary mode to ensure that the tool produces the
        * same output on all platforms !!
        */
        to = fopen(filename.c_str(), "wb");
        if (to == NULL) {
            fprintf(stderr, "unable to open %s for write\n", filename.c_str());
            return 1;
        }
    }

    document->Write(to);

    fclose(to);
    return 0;
}

