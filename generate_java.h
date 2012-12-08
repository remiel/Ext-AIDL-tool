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

#ifndef GENERATE_JAVA_H
#define GENERATE_JAVA_H

#include "aidl_language.h"
#include "AST.h"

#include <string>
#include <map>

using namespace std;

int generate_java(const string& filename, const string& originalSrc,
                interface_type* iface, map<string, method_type*>* method_map);

Class* generate_binder_interface_class(const interface_type* iface, map<string, method_type*>* method_map);
Class* generate_rpc_interface_class(const interface_type* iface);

string gather_comments(extra_text_type* extra);
string append(const char* a, const char* b);

class VariableFactory
{
public:
    VariableFactory(const string& base); // base must be short
    Variable* Get(Type* type);
    Variable* Get(int index);
private:
    vector<Variable*> m_vars;
    string m_base;
    int m_index;
};

#endif // GENERATE_JAVA_H

