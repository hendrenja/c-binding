/*
 * c_api.c
 *
 *  Created on: Oct 10, 2012
 *      Author: sander
 */

#include "api.h"
#include "driver/gen/c/common/common.h"

/* Walk all types */
static
int16_t c_apiWalkType(
    corto_type o,
    c_apiWalk_t* data)
{
    /* Generate __create function */
    if (c_apiTypeCreateChild(o, data)) {
        goto error;
    }

    /* Generate __declare macro */
    if (c_apiTypeDeclareChild(o, data)) {
        goto error;
    }

    /* Generate __update function */
    if (c_apiTypeUpdate(o, data)) {
        goto error;
    }

    return 0;
error:
    return -1;
}

/* Walk non-void types */
static
int16_t c_apiWalkNonVoid(
    corto_type o,
    c_apiWalk_t* data)
{

    /* Generate __set function */
    if (c_apiTypeSet(o, data)) {
        goto error;
    }

    return 0;
error:
    return -1;
}

/* Forward objects for which code will be generated. */
static
int c_apiWalk(
    corto_object o,
    void* userData)
{
    c_apiWalk_t* data = userData;
    corto_id building_macro;
    bool bootstrap = !strcmp(g_getAttribute(data->g, "bootstrap"), "true");

    c_buildingMacro(data->g, building_macro);

    if (corto_class_instanceof(corto_type_o, o) && !corto_instanceof(corto_native_type_o, o)) {
        g_fileWrite(data->header, "/* %s */\n", corto_fullpath(NULL, o));

        data->current = o;

        /* Build nameconflict cache */
        if (corto_type(o)->kind == CORTO_COMPOSITE) {
            data->memberCache = corto_genMemberCacheBuild(o);
        }

        if (c_apiWalkType(corto_type(o), userData)) {
            goto error;
        }

        if (corto_type(o)->kind != CORTO_VOID || corto_type(o)->reference) {
            if (c_apiWalkNonVoid(corto_type(o), userData)) {
                goto error;
            }
        }

        corto_id id;
        corto_id localId;
        c_short_id(data->g, localId, o);
        c_id(data->g, id, o);

        if (!bootstrap && corto_parentof(o) != root_o) {
            if (strcmp(id, localId)) {
                g_fileWrite(data->header, "\n");
                g_fileWrite(data->header, "#define %s__create %s__create\n", localId, id);
                g_fileWrite(data->header, "#define %s__create_auto %s__create_auto\n", localId, id);
                g_fileWrite(data->header, "#define %s__declare %s__declare\n", localId, id);
                g_fileWrite(data->header, "#define %s__update %s__update\n", localId, id);
                if (corto_type(o)->kind != CORTO_VOID || corto_type(o)->reference) {
                    g_fileWrite(data->header, "#define %s__assign %s__assign\n", localId, id);
                    g_fileWrite(data->header, "#define %s__set %s__set\n", localId, id);
                    g_fileWrite(data->header, "#define %s__unset %s__unset\n", localId, id);
                }
            }
        }

        if (!bootstrap && corto_instanceof(corto_interface_o, o) &&
            corto_check_attr(o, CORTO_ATTR_NAMED) &&
            corto_parentof(o) == g_getCurrent(data->g))
        {
            corto_id objectId;
            strcpy(objectId, corto_idof(o));
            objectId[0] = toupper(objectId[0]);

            if (strcmp(localId, objectId)) {
                g_fileWrite(data->header, "\n");
                g_fileWrite(data->header, "#ifdef %s\n", building_macro);
                g_fileWrite(data->header, "#define %s__create %s__create\n", objectId, id);
                g_fileWrite(data->header, "#define %s__create_auto %s__create_auto\n", objectId, id);
                g_fileWrite(data->header, "#define %s__declare %s__declare\n", objectId, id);
                g_fileWrite(data->header, "#define %s__update %s__update\n", objectId, id);
                if (corto_type(o)->kind != CORTO_VOID || corto_type(o)->reference) {
                    g_fileWrite(data->header, "#define %s__assign %s__assign\n", objectId, id);
                    g_fileWrite(data->header, "#define %s__set %s__set\n", objectId, id);
                    g_fileWrite(data->header, "#define %s__unset %s__unset\n", objectId, id);
                }
                g_fileWrite(data->header, "#endif\n");
            }
        }

        /* Clear nameconflict cache */
        if (corto_type(o)->kind == CORTO_COMPOSITE) {
            if (corto_interface(o)->kind == CORTO_DELEGATE) {
                if (c_apiDelegateCall(corto_delegate(o), userData)) {
                    goto error;
                }
                if (c_apiDelegateInitCallback(corto_delegate(o), FALSE, userData)) {
                    goto error;
                }
                if (c_apiDelegateInitCallback(corto_delegate(o), TRUE, userData)) {
                    goto error;
                }
            }
            corto_genMemberCacheClean(data->memberCache);

            if (strcmp(g_getAttribute(data->g, "bootstrap"), "true") && corto_parentof(o) != root_o) {
                if (strcmp(id, localId)) {
                    g_fileWrite(data->header, "#define %s__call %s__call\n", localId, id);
                    g_fileWrite(data->header, "#define %s__init_c %s__init_c\n", localId, id);
                    g_fileWrite(data->header, "#define %s__init_c_instance %s__init_c_instance\n", localId, id);
                    g_fileWrite(data->header, "#define %s__init_c_instance_auto %s__init_c_instance_auto\n", localId, id);
                }
            }
        }

        g_fileWrite(data->header, "\n");
    }

    return 1;
error:
    return 0;
}

void c_apiLocalDefinition(
    corto_type t,
    c_apiWalk_t *data,
    char *func,
    char *id)
{
    corto_id localId, buildingMacro;

    c_buildingMacro(data->g, buildingMacro);
    g_shortOid(data->g, t, localId);

    if (strcmp(g_getAttribute(data->g, "bootstrap"), "true") && corto_parentof(t) != root_o) {
        g_fileWrite(data->header, "\n");
        g_fileWrite(data->header, "#if defined(%s) && !defined(__cplusplus)\n", buildingMacro);
        g_fileWrite(data->header, "#define %s%s %s%s\n", localId, func, id, func);
        g_fileWrite(data->header, "#endif\n");
    }
}

/* Open headerfile, write standard header. */
static
g_file c_apiHeaderOpen(
    c_apiWalk_t *data)
{
    g_file result;
    corto_bool local = !strcmp(g_getAttribute(data->g, "local"), "true");
    corto_bool app = !strcmp(g_getAttribute(data->g, "app"), "true");
    corto_id headerFileName, path;
    corto_id name = {0};
    char *namePtr = g_getName(data->g), *ptr = name + 1;
    bool bootstrap = !strcmp(g_getAttribute(data->g, "bootstrap"), "true");

    corto_object pkg;
    if (bootstrap) {
        pkg = g_getCurrent(data->g);
    } else {
        pkg = g_getPackage(data->g);
    }

    if (!app && !local) {
        corto_fullpath(name, pkg);

        while (*namePtr == *ptr && *namePtr && *ptr) {
            namePtr ++;
            ptr ++;
        }

        if (ptr[0] == '/') {
            ptr ++;
        }
    }

    if (!ptr[0]) {
        strcpy(name, "_api");
        ptr = name;
    }

    /* Create file */
    sprintf(headerFileName, "%s.h", ptr);

    if (!local && !app) {
        g_fileWrite(data->mainHeader, "#include <%s/c/%s>\n", g_getName(data->g), headerFileName);
    }

    /* Create file */
    result = g_fileOpen(data->g, headerFileName);

    /* Obtain path for macro */
    corto_path(path, root_o, pkg, "_");
    strupper(path);

    /* Print standard comments and includes */
    g_fileWrite(result, "/* %s\n", headerFileName);
    g_fileWrite(result, " *\n");
    g_fileWrite(result, " * API convenience functions for C-language.\n");
    g_fileWrite(result, " * This file contains generated code. Do not modify!\n");
    g_fileWrite(result, " */\n\n");
    g_fileWrite(result, "#ifndef %s__API_H\n", path);
    g_fileWrite(result, "#define %s__API_H\n\n", path);

    c_includeFrom(data->g, result, corto_o, "corto.h");
    if (bootstrap) {
        g_fileWrite(result, "#include <%s/_project.h>\n", g_getName(data->g));
    } else {
        c_includeFrom(data->g, result, pkg, "_project.h");
        c_includeFrom(data->g, result, pkg, "_type.h");
        g_fileWrite(result, "\n");
    }

    g_fileWrite(result, "#ifdef __cplusplus\n");
    g_fileWrite(result, "extern \"C\" {\n");
    g_fileWrite(result, "#endif\n\n");

    return result;
}

/* Close headerfile */
static
void c_apiHeaderClose(
    g_file file)
{
    /* Print standard comments and includes */
    g_fileWrite(file, "\n");
    g_fileWrite(file, "#ifdef __cplusplus\n");
    g_fileWrite(file, "}\n");
    g_fileWrite(file, "#endif\n");
    g_fileWrite(file, "#endif\n\n");
}

/* Open sourcefile */
static
g_file c_apiSourceOpen(
    g_generator g)
{
    g_file result;
    corto_id sourceFileName;
    corto_bool cpp = !strcmp(g_getAttribute(g, "c4cpp"), "true");
    corto_bool local = !strcmp(g_getAttribute(g, "local"), "true");
    corto_bool app = !strcmp(g_getAttribute(g, "app"), "true");
    corto_id name = {0};
    char *namePtr = g_getName(g), *ptr = name + 1;
    corto_bool hidden = FALSE;

    if (!app && !local) {
        corto_fullpath(name, g_getCurrent(g));

        while (*namePtr == *ptr && *namePtr && *ptr) {
            namePtr ++;
            ptr ++;
        }

        if (ptr[0] == '/') {
            ptr ++;
        }
    } else {
        hidden = TRUE;
    }

    if (!ptr[0]) {
        strcpy(name, "_api");
        ptr = name;
    }

    /* Create file */
    sprintf(sourceFileName, "%s.%s", ptr, cpp ? "cpp" : "c");

    if (hidden) {
        result = g_hiddenFileOpen(g, "%s", sourceFileName);
    } else {
        result = g_fileOpen(g, "%s", sourceFileName);
    }

    if (!result) {
        goto error;
    }

    /* Print standard comments and includes */
    g_fileWrite(result, "/* %s\n", sourceFileName);
    g_fileWrite(result, " *\n");
    g_fileWrite(result, " * API convenience functions for C-language.\n");
    g_fileWrite(result, " * This file contains generated code. Do not modify!\n");
    g_fileWrite(result, " */\n\n");

    if (!local && !app) {
        g_fileWrite(result, "#include <%s/c/c.h>\n", g_getName(g));
    } else {
        g_fileWrite(result, "#include <include/_api.h>\n");
        g_fileWrite(result, "#include <include/_load.h>\n", g_getName(g));
    }

    g_fileWrite(result, "\n");

    return result;
error:
    return NULL;
}

static
int c_apiVariableWalk(
    void *o,
    void *userData)
{
    c_apiWalk_t *data = userData;

    corto_bool local = !strcmp(g_getAttribute(data->g, "local"), "true");
    corto_bool app = !strcmp(g_getAttribute(data->g, "app"), "true");

    if (!corto_isbuiltin(o) && (!g_mustParse(data->g, o) || (!app && !local))) {
        corto_id varId, typeId, packageId;
        corto_fullpath(packageId, g_getCurrent(data->g));
        c_varId(data->g, o, varId);
        c_typeret(data->g, corto_typeof(o), C_ByReference, false, typeId);
        g_fileWrite(data->source, "corto_type _%s;\n", varId);
        g_fileWrite(
            data->source,
            "#define %s _%s ? _%s : (_%s = *(corto_type*)corto_load_sym(\"%s\", &_package, \"%s\"))\n",
            varId, varId, varId, varId, packageId, varId);
        g_fileWrite(data->source, "\n");
    }

    return 1;
}

static
int c_apiVariableHasVariableDefs(
    void *o,
    void *userData)
{
    c_apiWalk_t *data = userData;

    corto_bool local = !strcmp(g_getAttribute(data->g, "local"), "true");
    corto_bool app = !strcmp(g_getAttribute(data->g, "app"), "true");

    if (!corto_isbuiltin(o) && (!g_mustParse(data->g, o) || (!app && !local))) {
        return 0;
    }

    return 1;
}

static
bool c_api_typeInList(
    corto_ll types,
    corto_type t)
{
    corto_iter it = corto_ll_iter(types);
    while (corto_iter_hasNext(&it)) {
        corto_type type_in_list = corto_iter_next(&it);
        if (type_in_list == t) {
            return true;
        } else
        if (!corto_check_attr(t, CORTO_ATTR_NAMED) &&
            !corto_check_attr(type_in_list, CORTO_ATTR_NAMED))
        {
            if (corto_compare(type_in_list, t) == CORTO_EQ) {
                return true;
            }
        }
    }
    return false;
}

static
int16_t c_apiAddDependent_item(
    corto_walk_opt *opt,
    corto_value *info,
    void *userData)
{
    corto_type t = corto_value_typeof(info);
    corto_ll types = userData;

    if (t->kind != CORTO_PRIMITIVE) {
        if (!c_api_typeInList(types, t)) {
            corto_ll_append(types, t);
        }
    }

    return 0;
}

static
void c_apiAddDependentTypes(
    corto_ll types)
{
    corto_walk_opt opt;
    corto_walk_init(&opt);
    opt.metaprogram[CORTO_MEMBER] = c_apiAddDependent_item;
    opt.metaprogram[CORTO_ELEMENT] = c_apiAddDependent_item;
    corto_iter it = corto_ll_iter(types);
    while (corto_iter_hasNext(&it)) {
        corto_type t = corto_iter_next(&it);
        corto_metawalk(&opt, t, types);
    }
}

static
int c_apiWalkPackages(
    corto_object o,
    void* userData)
{
    c_apiWalk_t *data = userData;
    corto_bool bootstrap = !strcmp(g_getAttribute(data->g, "bootstrap"), "true");

    CORTO_UNUSED(o);

    /* Open API header */
    data->header = c_apiHeaderOpen(data);
    if (!data->header) {
        goto error;
    }

    /* Open API source file */
    data->source = c_apiSourceOpen(data->g);
    if (!data->source) {
        goto error;
    }

    data->collections = c_findType(data->g, corto_collection_o);
    data->iterators = c_findType(data->g, corto_iterator_o);
    data->args = NULL;

    /* Define local variables for package objects if functions are generated in
     * a separate package (to prevent cyclic dependencies between libs) */
    if (!bootstrap) {
        data->types = c_findType(data->g, corto_type_o);

        /* For all types, collect non-primitive dependent types that are not in
         * this package, as we will need object variables for those too. */
        c_apiAddDependentTypes(data->types);

        /* Walk over collected types */
        int has_defs = !corto_ll_walk(data->types, c_apiVariableHasVariableDefs, data);
        if (data->types && has_defs) {
            g_fileWrite(data->source, "static corto_dl _package;\n");
            corto_ll_walk(data->types, c_apiVariableWalk, data);
        }
        if (data->types) {
            corto_ll_free(data->types);
        }
    }

    if (!g_walkRecursive(data->g, c_apiWalk, data)) {
        goto error;
    }

    corto_ll_walk(data->collections, c_apiCollectionWalk, data);
    corto_ll_walk(data->iterators, c_apiIteratorWalk, data);

    corto_ll_free(data->collections);
    corto_ll_free(data->iterators);

    c_apiHeaderClose(data->header);

    return 1;
error:
    return 0;
}

/* Generator main */
int16_t genmain(g_generator g) {
    corto_bool local = !strcmp(g_getAttribute(g, "local"), "true");
    corto_bool app = !strcmp(g_getAttribute(g, "app"), "true");
    bool cpp = !strcmp(g_getAttribute(g, "c4cpp"), "true");

    c_apiWalk_t walkData;
    memset(&walkData, 0, sizeof(walkData));
    char *cwd = corto_strdup(corto_cwd());

    /* Create project files */
    if (!local && !app) {
        if (!corto_file_test("c/project.json")) {
            corto_int8 ret, sig;
            corto_id cmd;
            sprintf(
                cmd,
                "corto create package %s/c %s --unmanaged --notest --nobuild --silent -o c",
                g_getName(g),
                cpp ? "--use-cpp" : "");

            sig = corto_proc_cmd(cmd, &ret);
            if (sig || ret) {
                corto_throw("failed to setup project for '%s/c'", g_getName(g));
                goto error;
            }

            /* Overwrite rakefile */
            g_file rakefile = g_fileOpen(g, "c/project.json");
            if (!rakefile) {
                corto_throw("failed to open c/project.json");
                goto error;
            }
            g_fileWrite(rakefile, "{\n");
            g_fileWrite(rakefile, "    \"id\": \"%s/c\",\n", g_getName(g));
            g_fileWrite(rakefile, "    \"type\": \"package\",\n");
            g_fileWrite(rakefile, "    \"value\": {\n");
            g_fileWrite(rakefile, "        \"use\": [\"corto\"],\n");
            g_fileWrite(rakefile, "        \"language\": \"c\",\n");
            if (cpp) {
                g_fileWrite(rakefile, "        \"c4cpp\": true,\n");
            }
            g_fileWrite(rakefile, "        \"managed\": false\n");
            g_fileWrite(rakefile, "    }\n");
            g_fileWrite(rakefile, "}\n");
            g_fileClose(rakefile);
        }

        corto_chdir("c");

        walkData.mainHeader = g_fileOpen(g, "c.h");
        if (!walkData.mainHeader) {
            goto error;
        }

    } else {
        walkData.mainHeader = NULL;
    }
    walkData.g = g;

    if (!g_walkNoScope(g, c_apiWalkPackages, &walkData)) {
        goto error;
    }

    if (!local) {
        corto_chdir(cwd);
        corto_dealloc(cwd);
    }

    return 0;
error:
    return -1;
}
