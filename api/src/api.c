/*
 * c_api.c
 *
 *  Created on: Oct 10, 2012
 *      Author: sander
 */

#include "api.h"
#include "corto/gen/c/common/common.h"

/* Walk all types */
static corto_int16 c_apiWalkType(corto_type o, c_apiWalk_t* data) {

    /* Generate _create function */
    if (c_apiTypeCreate(o, data)) {
        goto error;
    }

    /* Generate _createChild function */
    if (c_apiTypeCreateChild(o, data)) {
        goto error;
    }

    /* Generate _update function */
    if (c_apiTypeUpdate(o, data)) {
        goto error;
    }

    g_fileWrite(data->header, "\n");

    return 0;
error:
    return -1;
}

/* Walk non-void types */
static corto_int16 c_apiWalkNonVoid(corto_type o, c_apiWalk_t* data) {

    /* Generate _declare function */
    if (c_apiTypeDeclare(o, data)) {
        goto error;
    }

    /* Generate _declareChild function */
    if (c_apiTypeDeclareChild(o, data)) {
        goto error;
    }

    /* Generate _define function */
    if (c_apiTypeDefine(o, data)) {
        goto error;
    }

    /* Generate _set function */
    if (c_apiTypeSet(o, data)) {
        goto error;
    }

    /* Generate _str function */
    if (c_apiTypeStr(o, data)) {
        goto error;
    }

    /* Generate _fromStr function */
    if (c_apiTypeFromStr(o, data)) {
        goto error;
    }

    /* Generate _copy function */
    /* if (c_apiTypeCopy(o, data)) {
        goto error;
    } */

    /* Generate _compare function */
    if (c_apiTypeCompare(o, data)) {
        goto error;
    }

    g_fileWrite(data->header, "\n");

    return 0;
error:
    return -1;
}

/* Walk all types */
static corto_int16 c_apiWalkNonReference(corto_type o, c_apiWalk_t* data) {

    /* Generate _init function */
    if (c_apiTypeInit(o, data)) {
        goto error;
    }

    /* Generate _deinit function */
    if (c_apiTypeDeinit(o, data)) {
        goto error;
    }

    g_fileWrite(data->header, "\n");

    return 0;
error:
    return -1;
}

/* Forward objects for which code will be generated. */
static int c_apiWalk(corto_object o, void* userData) {
    c_apiWalk_t* data = userData;

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

        if (corto_type(o)->kind != CORTO_VOID) {
            if (c_apiWalkNonVoid(corto_type(o), userData)) {
                goto error;
            }
            if (!corto_type(o)->reference) {
                if (c_apiWalkNonReference(corto_type(o), userData)) {
                    goto error;
                }
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
        }
    }

    return 1;
error:
    return 0;
}

/* Open headerfile, write standard header. */
static g_file c_apiHeaderOpen(c_apiWalk_t *data) {
    g_file result;
    corto_id headerFileName, path;
    corto_id name;
    corto_fullpath(name, g_getCurrent(data->g));
    char *namePtr = g_getName(data->g), *ptr = name + 1;

    while (*namePtr == *ptr) {
        namePtr ++;
        ptr ++;
    }

    if (ptr[0] == '/') {
        ptr ++;
    }

    if (!ptr[0]) {
        strcpy(name, "_api");
        ptr = name;
    }

    /* Create file */
    sprintf(headerFileName, "%s.h", ptr);

    g_fileWrite(data->mainHeader, "#include <%s/c/%s>\n", g_getName(data->g), headerFileName);

    /* Create file */
    result = g_fileOpen(data->g, headerFileName);

    /* Obtain path for macro */
    corto_path(path, root_o, g_getCurrent(data->g), "_");
    corto_strupper(path);

    /* Print standard comments and includes */
    g_fileWrite(result, "/* %s\n", headerFileName);
    g_fileWrite(result, " *\n");
    g_fileWrite(result, " * API convenience functions for C-language.\n");
    g_fileWrite(result, " * This file contains generated code. Do not modify!\n");
    g_fileWrite(result, " */\n\n");
    g_fileWrite(result, "#ifndef %s__API_H\n", path);
    g_fileWrite(result, "#define %s__API_H\n\n", path);

    c_includeFrom(data->g, result, corto_o, "corto.h");
    if (!strcmp(g_getAttribute(data->g, "bootstrap"), "true")) {
        g_fileWrite(result, "#include <%s/_project.h>\n", g_getName(data->g));
    } else {
        c_includeFrom(data->g, result, g_getCurrent(data->g), "_project.h");
    }

    g_fileWrite(result, "#ifdef __cplusplus\n");
    g_fileWrite(result, "extern \"C\" {\n");
    g_fileWrite(result, "#endif\n");

    return result;
}

/* Close headerfile */
static void c_apiHeaderClose(g_file file) {

    /* Print standard comments and includes */
    g_fileWrite(file, "\n");
    g_fileWrite(file, "#ifdef __cplusplus\n");
    g_fileWrite(file, "}\n");
    g_fileWrite(file, "#endif\n");
    g_fileWrite(file, "#endif\n\n");
}

/* Open sourcefile */
static g_file c_apiSourceOpen(g_generator g) {
    g_file result;
    corto_id sourceFileName;
    corto_bool cpp = !strcmp(g_getAttribute(g, "c4cpp"), "true");
    corto_id name;
    corto_fullpath(name, g_getCurrent(g));
    char *namePtr = g_getName(g), *ptr = name + 1;

    while (*namePtr == *ptr) {
        namePtr ++;
        ptr ++;
    }

    if (ptr[0] == '/') {
        ptr ++;
    }

    if (!ptr[0]) {
        strcpy(name, "_api");
        ptr = name;
    }

    /* Create file */
    sprintf(sourceFileName, "%s.%s", ptr, cpp ? "cpp" : "c");
    result = g_fileOpen(g, sourceFileName);

    if (!result) {
        goto error;
    }

    /* Print standard comments and includes */
    g_fileWrite(result, "/* %s\n", sourceFileName);
    g_fileWrite(result, " *\n");
    g_fileWrite(result, " * API convenience functions for C-language.\n");
    g_fileWrite(result, " * This file contains generated code. Do not modify!\n");
    g_fileWrite(result, " */\n\n");

    c_include(g, result, g_getCurrent(g));
    g_fileWrite(result, "#include <%s/c/c.h>\n", g_getName(g));

    return result;
error:
    return NULL;
}

static int c_apiWalkPackages(corto_object o, void* userData) {
    c_apiWalk_t *data = userData;

    g_generator g = data->g;

    data->header = c_apiHeaderOpen(data);
    if (!data->header) {
        goto error;
    }

    data->source = c_apiSourceOpen(g);
    if (!data->source) {
        goto error;
    }

    if (!g_walkRecursive(g, c_apiWalk, data)) {
        goto error;
    }

    c_apiHeaderClose(data->header);

    return 1;
error:
    return 0;
}

/* Generator main */
corto_int16 corto_genMain(g_generator g) {
    c_apiWalk_t walkData;
    memset(&walkData, 0, sizeof(walkData));
    char *cwd = corto_strdup(corto_cwd());

    /* Create project files */
    if (!corto_fileTest("c/rakefile")) {
        corto_int8 ret, sig;
        corto_id cmd;
        sprintf(cmd, "corto create package %s/c --unmanaged --notest --nobuild --silent", g_getName(g));
        sig = corto_proccmd(cmd, &ret);
        if (sig || ret) {
            corto_seterr("failed to setup project for '%s/c'", 
                g_getName(g));
            goto error;
        }

        /* Overwrite rakefile */
        g_file rakefile = g_fileOpen(g, "c/rakefile");
        if (!rakefile) {
            corto_seterr("failed to open c/rakefile: %s", corto_lasterr());
            goto error;
        }
        g_fileWrite(rakefile, "PACKAGE = '%s/c'\n\n", g_getName(g));
        g_fileWrite(rakefile, "NOCORTO = true\n");
        g_fileWrite(rakefile, "USE_PACKAGE = ['%s']\n\n", g_getName(g));
        g_fileWrite(rakefile, "require \"#{ENV['CORTO_BUILD']}/package\"\n");
        g_fileClose(rakefile);
    }

    corto_chdir("c");

    walkData.g = g;
    walkData.mainHeader = g_fileOpen(g, "c.h");
    if (!walkData.mainHeader) {
        goto error;
    }

    walkData.collections = c_findType(g, corto_collection_o);
    walkData.iterators = c_findType(g, corto_iterator_o);
    walkData.args = NULL;

    /* Default prefixes for corto namespaces */
    g_parse(g, corto_o, FALSE, FALSE, "");
    g_parse(g, corto_lang_o, FALSE, FALSE, "corto");
    g_parse(g, corto_core_o, FALSE, FALSE, "corto");
    g_parse(g, corto_native_o, FALSE, FALSE, "corto_native");
    g_parse(g, corto_secure_o, FALSE, FALSE, "corto_secure");

    if (!g_walkNoScope(g, c_apiWalkPackages, &walkData)) {
        goto error;
    }

    corto_llWalk(walkData.collections, c_apiCollectionWalk, &walkData);
    corto_llWalk(walkData.iterators, c_apiIteratorWalk, &walkData);

    corto_llFree(walkData.collections);
    corto_llFree(walkData.iterators);

    corto_chdir(cwd);
    corto_dealloc(cwd);

    return 0;
error:
    return -1;
}
