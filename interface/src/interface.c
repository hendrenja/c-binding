/*
 * c_interface.c
 *
 *  Created on: Oct 4, 2012
 *      Author: sander
 */

#include <corto/corto.h>
#include <driver/gen/c/common/common.h>
#include <corto/util/cdiff/cdiff.h>

#define GENERATED_MARKER "This is a managed file. Do not delete this comment."

typedef struct c_interfaceExisting {
    corto_string id;
    corto_string src;
} c_interfaceExisting;

typedef struct c_interfaceWalk_t c_interfaceWalk_t;
struct c_interfaceWalk_t {
    g_generator g;
    int16_t(*walkAction)(corto_object,c_interfaceWalk_t*);
    cdiff_file source;
    g_file wrapper;
    g_file mainHeader;
    g_file interfaceHeader;
    corto_uint16 firstComma;
    bool generateHeader;
    bool generateSource;
    corto_id sizeExpr;
    corto_ll generated;
    bool mainWritten;
    corto_ll existedBeforeGenerating;
};

static
cdiff_file c_interfaceOpenFile(
    corto_string name,
    c_interfaceWalk_t *data)
{
    corto_id filepath;
    char *filename = g_filePath(data->g, filepath, "%s", name);
    cdiff_file result = cdiff_file_open(filename);
    if (result) {
        corto_ll_append(data->generated, corto_strdup(name));
        cdiff_file_write(result, "/* %s */\n\n", GENERATED_MARKER);
    }
    return result;
}

static
int c_interfaceParamNameSource(
    corto_parameter *o,
    void *userData)
{
    c_interfaceWalk_t* data = userData;
    corto_id name;
    if (data->firstComma) {
        g_fileWrite(data->wrapper, ", ");
    }
    g_fileWrite(data->wrapper, "%s", c_param_name(o->name, name));
    data->firstComma++;
    return 1;
}

static
int c_interfaceParamTypeSource(
    corto_parameter *o,
    void *userData)
{
    c_interfaceWalk_t* data = userData;
    corto_id type;
    if (data->firstComma) {
        g_fileWrite(data->wrapper, ", ");
    }
    g_fileWrite(data->wrapper, "%s", c_param_type(data->g, o, type));
    data->firstComma++;
    return 1;
}

static
g_file c_interfaceWrapperFileOpen(
    g_generator g)
{
    g_file result;
    corto_char fileName[512];
    bool cpp = !strcmp(g_getAttribute(g, "c4cpp"), "true");
    bool bootstrap = !strcmp(g_getAttribute(g, "bootstrap"), "true");

    corto_object o = g_getCurrent(g);
    sprintf(fileName, "_interface.%s", cpp ? "cpp" : "c");

    if (!strcmp(g_getAttribute(g, "bootstrap"), "true")) {
        result = g_fileOpen(g, fileName);
    } else {
        result = g_hiddenFileOpen(g, fileName);
    }

    if(!result) {
        goto error;
    }

    /* Print standard comments and includes */
    g_fileWrite(result, "/* %s\n", fileName);
    g_fileWrite(result, " *\n");
    g_fileWrite(result, " * This file contains interface methods for %s.\n",
        corto_fullpath(NULL, o));
    g_fileWrite(result, " */\n\n");

    if (!bootstrap) {
        corto_buffer include = CORTO_BUFFER_INIT;
        c_include_toBuffer(g, &include, g_getCurrent(g));
        char *includeStr = corto_buffer_str(&include);
        g_fileWrite(result, includeStr);
        free(includeStr);

        c_includeFrom_toBuffer(g, &include, g_getCurrent(g), "_load.h");
        includeStr = corto_buffer_str(&include);
        g_fileWrite(result, includeStr);
        free(includeStr);
    } else {
        corto_buffer include = CORTO_BUFFER_INIT;
        c_include_toBuffer(g, &include, corto_o);
        char *includeStr = corto_buffer_str(&include);
        g_fileWrite(result, includeStr);
        free(includeStr);
    }

    return result;
error:
    return NULL;
}

/* Generate implementation for virtual methods */
static
int c_interfaceGenerateVirtual(
    corto_method o,
    c_interfaceWalk_t* data)
{
    corto_id id, return_typeId, typeVarId, methodVarId, typeId;
    bool returnsValue;
    char *nameString = NULL;
    bool isInterface = corto_interface(corto_parentof(o))->kind == CORTO_INTERFACE;

    if (isInterface) {
        g_fullOid(data->g, o, id);
        g_fullOid(data->g, corto_parentof(o), typeId);
        c_varId(data->g, o, methodVarId);
        c_varId(data->g, corto_parentof(o), typeVarId);

        if (((corto_function)o)->return_type &&
            ((corto_function(o)->return_type->kind != CORTO_VOID) ||
             corto_function(o)->return_type->reference))
        {
            returnsValue = TRUE;
            c_typeret(data->g, corto_function(o)->return_type, C_ByValue, false, return_typeId);
        } else {
            returnsValue = FALSE;
            strcpy(return_typeId, "void");
        }

        /* Create a wrapper file if it was not already created */
        if (!data->wrapper) {
            data->wrapper = c_interfaceWrapperFileOpen(data->g);
            if (!data->wrapper) {
                goto error;
            }
        }

        corto_buffer declBuffer = CORTO_BUFFER_INIT;
        if (c_decl(
            data->g,
            &declBuffer,
            corto_function(o),
            TRUE,
            TRUE /* never write 'this' in public header */,
            FALSE))
        {
            goto error;
        }

        char *decl = corto_buffer_str(&declBuffer);

        /* Write casting macro to header */
        c_writeExport(data->g, data->interfaceHeader);
        g_fileWrite(data->interfaceHeader, "\n");
        g_fileWrite(data->interfaceHeader, "%s;\n", decl);
        g_fileWrite(data->wrapper, "%s\n", decl);
        free(decl);

        /* Obtain string for function name */
        nameString = c_escapeString(corto_idof(o));

        /* Begin of function */
        g_fileWrite(data->wrapper, "\n{\n");
        g_fileIndent(data->wrapper);
        g_fileWrite(data->wrapper, "static corto_uint32 _methodId;\n");
        g_fileWrite(data->wrapper, "corto_method _method;\n");
        if (returnsValue) {
            g_fileWrite(data->wrapper, "%s _result;\n", return_typeId);
        }
        g_fileWrite(data->wrapper, "corto_interface _abstract;\n\n");

        g_fileWrite(data->wrapper, "_abstract = corto_interface(corto_typeof(_this));\n\n");
        g_fileWrite(data->wrapper, "/* Determine methodId once, then cache it for subsequent calls. */\n");
        g_fileWrite(data->wrapper, "if (!_methodId) {\n");
        g_fileIndent(data->wrapper);
        if (!isInterface) {
            g_fileWrite(data->wrapper, "_methodId = corto_interface_resolve_method_id(_abstract, \"%s\");\n", nameString);
        } else {
            g_fileWrite(data->wrapper, "_methodId = corto_interface_resolve_method_id(%s, \"%s\");\n", typeVarId, nameString);
        }
        g_fileDedent(data->wrapper);
        g_fileWrite(data->wrapper, "}\n");
        g_fileWrite(
          data->wrapper,
          "corto_assert(_methodId, \"method '%s' not found in '%%s'%%s%%s\", corto_fullpath(NULL, _abstract), corto_lasterr() ? \": \" : \"\", corto_lasterr() ? corto_lasterr() : \"\");\n\n",
          nameString);
        g_fileWrite(data->wrapper, "/* Lookup method-object. */\n");
        if (isInterface) {
            g_fileWrite(data->wrapper, "_method = corto_class_resolve_interface_method((corto_class)_abstract, %s, _methodId);\n",
                typeVarId);
        } else {
            g_fileWrite(data->wrapper, "_method = corto_interface_resolve_method_by_id(_abstract, _methodId);\n");
        }
        g_fileWrite(data->wrapper,
          "corto_assert(_method != NULL, \"unresolved method '%%s::%s@%%d'\", corto_idof(_this), _methodId);\n\n",
          nameString);

        g_fileWrite(data->wrapper, "if (corto_function(_method)->kind == CORTO_PROCEDURE_CDECL) {\n");
        g_fileIndent(data->wrapper);
        if (returnsValue) {
            g_fileWrite(data->wrapper, "_result = ");
        }
        data->firstComma = 1;
        g_fileWrite(data->wrapper, "((%s ___ (*)(corto_object", return_typeId);
        if (!c_paramWalk(o, c_interfaceParamTypeSource, data)) {
            goto error;
        }
        g_fileWrite(data->wrapper, "))((corto_function)_method)->fptr)(_this");
        if (!c_paramWalk(o, c_interfaceParamNameSource, data)) {
            goto error;
        }
        g_fileWrite(data->wrapper, ");\n");
        g_fileDedent(data->wrapper);
        g_fileWrite(data->wrapper, "} else {\n");
        g_fileIndent(data->wrapper);

        if (returnsValue) {
            g_fileWrite(data->wrapper, "corto_invoke(corto_function(_method), &_result, _this");
        } else {
            g_fileWrite(data->wrapper, "corto_invoke(corto_function(_method), NULL, _this");
        }
        data->firstComma = 3;
        if (!c_paramWalk(o, c_interfaceParamNameSource, data)) {
            goto error;
        }
        g_fileWrite(data->wrapper, ");\n");

        g_fileDedent(data->wrapper);
        g_fileWrite(data->wrapper, "}\n");

        if (returnsValue) {
            g_fileWrite(data->wrapper, "\n");
            g_fileWrite(data->wrapper, "return _result;\n");
        }
        g_fileDedent(data->wrapper);
        g_fileWrite(data->wrapper, "}\n");

        corto_dealloc(nameString);
    }

    return 0;
error:
    if (nameString) corto_dealloc(nameString);
    return -1;
}

/* Generate methods for class */
static
int c_interfaceProcedure(
    corto_object o,
    void *userData)
{
    c_interfaceWalk_t *data = userData;
    bool defined = FALSE;

    /* Only generate code for procedures */
    if (corto_class_instanceof(corto_procedure_o, corto_typeof(o))) {
        corto_id fullname, functionName;
        corto_type return_type = corto_function(o)->return_type;
        corto_string doStubs = g_getAttribute(data->g, "stubs");
        bool cpp = !strcmp(g_getAttribute(data->g, "c4cpp"), "true");
        bool isInterface = false;
        if (corto_instanceof(corto_method_o, o) &&
            corto_instanceof(corto_interface_o, corto_parentof(o)))
        {
            isInterface = corto_interface(corto_parentof(o))->kind == CORTO_INTERFACE;
        }

        g_fileWrite(data->interfaceHeader, "\n");

        if (!isInterface) {
            corto_fullpath(fullname, o);
            c_short_id(data->g, functionName, o);

            defined = corto_check_state(o, CORTO_VALID) && (corto_function(o)->kind != CORTO_PROCEDURE_STUB);

            /* Check whether generation of stubs must be forced */
            if (doStubs) {
                if (!strcmp(doStubs, "true")) {
                    defined = TRUE;
                } else if(!strcmp(doStubs, "false")) {
                    defined = FALSE;
                }
            }

            /* Write forward declaration */
            c_writeExport(data->g, data->interfaceHeader);
            g_fileWrite(data->interfaceHeader, "\n");

            corto_buffer declBuffer = CORTO_BUFFER_INIT;
            if (c_decl(data->g, &declBuffer, corto_function(o), FALSE, TRUE /* never use 'this' in public headers */, FALSE)) {
                goto error;
            }
            char *decl = corto_buffer_str(&declBuffer);
            g_fileWrite(data->interfaceHeader, "%s;\n", decl);
            free(decl);

            /* Write to sourcefile */
            cdiff_file_write(data->source, "\n");

            /* Open element for function */
            cdiff_file_elemBegin(data->source, functionName);

            /* Write header for function */
            if (c_decl(data->g, &declBuffer, corto_function(o), FALSE, cpp, TRUE)) {
                goto error;
            }
            decl = corto_buffer_str(&declBuffer);

            cdiff_file_headerBegin(data->source);
            cdiff_file_write(data->source, "%s\n{", decl);
            cdiff_file_headerEnd(data->source);
            free(decl);

            /* Write body for function if this is first time function is written */
            if (!cdiff_file_bodyBegin(data->source)) {
                cdiff_file_write(data->source, "\n");
                cdiff_file_indent(data->source);
                cdiff_file_write(data->source, "/* Insert implementation */\n");
                cdiff_file_dedent(data->source);

                /* Lookup if there is an existing implementation. */
                if (defined) {
                    corto_id id;
                    corto_uint32 i;
                    corto_parameter *p;

                    if ((return_type->kind != CORTO_VOID) || (return_type->reference)) {
                        corto_id specifier;
                        c_typeret(data->g, return_type, C_ByValue, false, specifier);
                        cdiff_file_write(data->source, "%s _result;\n", specifier);
                    } else {
                        return_type = NULL;
                    }

                    /* If function is already defined, it is already implemented. The generator will generate a stub instead. */
                    cdiff_file_write(data->source, "corto_invoke(corto_function(%s_o)", g_fullOid(data->g, o, id));
                    if (return_type) {
                        cdiff_file_write(data->source, ",&_result");
                    } else {
                        cdiff_file_write(data->source, ",NULL");
                    }
                    if (corto_class_instanceof(corto_interface_o, corto_parentof(o))) {
                        if (corto_procedure(corto_typeof(o))->has_this) {
                            if (!cpp) {
                                cdiff_file_write(data->source, ",this");
                            } else {
                                cdiff_file_write(data->source, ",_this");
                            }
                        }
                    }
                    for (i=0; i<corto_function(o)->parameters.length; i++) {
                        p = &corto_function(o)->parameters.buffer[i];
                        cdiff_file_write(data->source, ",%s", g_id(data->g, p->name, id));
                    }
                    cdiff_file_write(data->source, ");\n");
                    if (return_type) {
                        cdiff_file_write(data->source, "return _result;\n");
                    }
                    cdiff_file_dedent(data->source);
                }

                cdiff_file_write(data->source, "}\n");
                cdiff_file_bodyEnd(data->source);
            }

            cdiff_file_elemEnd(data->source);
        }

        if (corto_function(o)->overridable) {
            c_interfaceGenerateVirtual(o, data);
        }
    }

    return 1;
error:
    return 0;
}

/* Check if there are procedures in the scope of an object. */
static
int c_interfaceCheckProcedures(
    void *o,
    void *ctx)
{
    CORTO_UNUSED(ctx);

    /* If the type of the type of the object is a procedure, return 0. */
    if (corto_class_instanceof(corto_procedure_o, corto_typeof(o))) {
        return 0;
    }
    if (corto_instanceof(corto_delegate_o, o)) {
        return 0;
    }

    return 1;
}

/* Open generator headerfile */
static
corto_int16 c_interfaceHeaderWrite(
    g_generator g,
    g_file result,
    corto_object o,
    corto_string name,
    corto_string headerFileName,
    bool mainHeader,
    c_interfaceWalk_t *data)
{
    bool bootstrap = !strcmp(g_getAttribute(g, "bootstrap"), "true");
    bool local = !strcmp(g_getAttribute(g, "local"), "true");
    bool app = !strcmp(g_getAttribute(g, "app"), "true");
    bool c4cpp = !strcmp(g_getAttribute(g, "c4cpp"), "true");

    bool error = FALSE;
    corto_id path;

    CORTO_UNUSED(data);

    strcpy(path, name);
    strupper(path);
    char *ptr, ch;
    for (ptr = path; (ch = *ptr); ptr++) if (ch == '/') *ptr = '_';

    g_fileWrite(result, "/* %s\n", headerFileName);
    if (mainHeader) {
        g_fileWrite(result, " * This is the main package file. Include this file in other projects.\n");
        g_fileWrite(result, " * Only modify inside the header-end and body-end sections.\n");
    } else {
        g_fileWrite(result, " * This file is generated. Do not modify the contents of this file.\n");
    }

    g_fileWrite(result, " */\n\n");
    g_fileWrite(result, "#ifndef %s_H\n", path);
    g_fileWrite(result, "#define %s_H\n", path);

    int header_count = 0;

    if (bootstrap) {
        g_fileWrite(result, "\n");
        g_fileWrite(result, "#include <%s/_project.h>\n", g_getProjectName(g));
        header_count ++;
    } else if (mainHeader) {
        g_fileWrite(result, "\n");
        if (o && (o != corto_o)) {
            /* Do not include corto.h in headers of builtin packages */
            c_includeFrom(g, result, corto_o, "corto.h");
            header_count ++;
        }
        c_includeFrom(g, result, g_getPackage(g), "_project.h");
        header_count ++;
    }

    if (mainHeader) {
        if (c_includeDependencies(data->g, result, NULL) || header_count) {
            g_fileWrite(result, "\n");
        }
    }

    if (error) {
        goto error;
    }

    /* If a header exists, write it */
    if (mainHeader) {
        corto_string snippet;
        if ((snippet = g_fileLookupHeader(result, ""))) {
            g_fileWrite(result, "/* $header()");
            g_fileWrite(result, "%s", snippet);
            g_fileWrite(result, "$end */\n");
        } else {
            g_fileWrite(result, "/* $header() */\n");
            g_fileWrite(result, "/* Definitions that are required by package headers (native types) go here. */\n");
            g_fileWrite(result, "/* $end */\n");
        }
    }

    if (g_getCurrent(g)) {
        corto_object from = g_getPackage(g);

        if (bootstrap) {
            from = g_getCurrent(g);
        }

        g_fileWrite(result, "\n");

        if (mainHeader) {
            c_includeFrom(g, result, from, "_type.h");
            c_includeFrom(g, result, from, "_interface.h");
            c_includeFrom(g, result, from, "_load.h");
            c_includeFrom(g, result, from, "_binding.h");
        }

        if (mainHeader && !bootstrap) {
            if (local || app) {
                c_includeFrom(g, result, from, "_api.h");
                if (c4cpp) {
                    c_includeFrom(g, result, from, "_cpp.h");
                }
            } else {
                c_includeFrom(g, result, from, "c/_api.h");
                if (c4cpp) {
                    g_fileWrite(result, "#ifdef __cplusplus\n");
                    c_includeFrom(g, result, from, "cpp/cpp.h");
                    g_fileWrite(result, "#endif\n");
                }
            }
        }
    }

    if (mainHeader) {
        corto_string snippet;

        g_fileWrite(result, "\n");
        if ((snippet = g_fileLookupSnippet(result, ""))) {
            g_fileWrite(result, "/* $body()");
            g_fileWrite(result, "%s", snippet);
            g_fileWrite(result, "$end */\n");
        } else {
            g_fileWrite(result, "/* $body() */\n");
            g_fileWrite(result, "/* Definitions here that need your package headers go here. */\n", snippet);
            g_fileWrite(result, "/* $end */\n");
        }
    }

    if (!mainHeader) {
        g_fileWrite(result, "\n");
        g_fileWrite(result, "#ifdef __cplusplus\n");
        g_fileWrite(result, "extern \"C\" {\n");
        g_fileWrite(result, "#endif\n");
    }

    return 0;
error:
    return -1;
}

/* Close headerfile */
static
void c_interfaceHeaderFileClose(
    g_file file,
    bool mainHeader,
    c_interfaceWalk_t *data)
{
    CORTO_UNUSED(data);

    /* Print standard comments and includes */
    if (!mainHeader) {
        g_fileWrite(file, "\n");
        g_fileWrite(file, "#ifdef __cplusplus\n");
        g_fileWrite(file, "}\n");
        g_fileWrite(file, "#endif\n");
    }
    g_fileWrite(file, "\n");
    g_fileWrite(file, "#endif\n\n");
}

/* Open generator sourcefile */
static
cdiff_file c_interfaceSourceFileOpen(
    corto_object o,
    corto_string name,
    c_interfaceWalk_t *data)
{
    cdiff_file result;
    corto_id fileName;
    bool cpp = !strcmp(g_getAttribute(data->g, "c4cpp"), "true");

    if (o) {
        c_filename(data->g, fileName, o, cpp ? "cpp" : "c");
    } else {
        strcpy(fileName, name);
    }

    result = c_interfaceOpenFile(fileName, data);
    if (!result) {
        goto error;
    }

    /* Include main header */
    if (o) {
        corto_id header;
        cdiff_file_write(result, "#include <%s>\n", c_mainheader(data->g, header));
        cdiff_file_write(result, "\n");
    }

    return result;
error:
    return NULL;
}

static
corto_int16 c_interfaceWriteMain(
    cdiff_file source,
    c_interfaceWalk_t* data)
{
    CORTO_UNUSED(data);
    bool cpp = !strcmp(g_getAttribute(data->g, "c4cpp"), "true");

    cdiff_file_write(source, "\n");

    cdiff_file_elemBegin(source, "cortomain");

    cdiff_file_headerBegin(source);
    if (cpp) {
        cdiff_file_write(source, "extern \"C\"\n");
    }
    cdiff_file_write(source, "int cortomain(int argc, char *argv[]) {");
    cdiff_file_headerEnd(source);

    if (!cdiff_file_bodyBegin(source)) {
        cdiff_file_write(source, "\n");
        cdiff_file_write(source, "\n");
        cdiff_file_indent(source);
        cdiff_file_write(source, "/* Insert implementation */\n");
        cdiff_file_write(source, "\n");
        cdiff_file_write(source, "return 0;\n");
        cdiff_file_dedent(source);
        cdiff_file_write(source, "}\n");
        cdiff_file_bodyEnd(source);
    }

    cdiff_file_elemEnd(source);

    return 0;
}

/* Generate interface for class */
static
corto_int16 c_interfaceObject(
    corto_object o,
    c_interfaceWalk_t* data)
{
    bool isBootstrap = !strcmp(g_getAttribute(data->g, "bootstrap"), "true");
    int hasProcedures = !corto_scope_walk(o, c_interfaceCheckProcedures, NULL);
    bool isTopLevelObject = (o == g_getCurrent(data->g)) && corto_instanceof(corto_package_o, o);

    /* Open sourcefile */
    data->source = c_interfaceSourceFileOpen(o, NULL, data);
    if (!data->source) {
        goto error;
    }

    if (!corto_instanceof(corto_package_o, o)) {
        g_fileWrite(
            data->interfaceHeader, "\n\n/* %s %s */\n",
            corto_fullpath(NULL, corto_typeof(o)),
            corto_path(NULL, root_o, o, "/"));
    }

    /* Walk scope */
    corto_objectseq procs = corto_scope_claim(o);
    corto_int32 i;
    for (i = 0; i < procs.length; i ++) {
        if (g_mustParse(data->g, procs.buffer[i])) {
            if (!c_interfaceProcedure(procs.buffer[i], data)) {
                break;
            }
        }
    }
    corto_scope_release(procs);
    if (i != procs.length) {
        goto error;
    }

    if (isTopLevelObject && hasProcedures) {
        /* put newline between procedures and headers */
        g_fileWrite(data->interfaceHeader, "\n");
    }

    /* If top level file, generate main function */
    if (isTopLevelObject && !isBootstrap) {
        data->mainWritten = TRUE;
        if (c_interfaceWriteMain(data->source, data)) {
            goto error;
        }
    }

    if (cdiff_file_close(data->source)) {
        corto_throw(NULL);
        goto error;
    }

    return 0;
error:
    return 1;
}

/* Walk interfaces */
static
int c_interfaceWalk(
    corto_object o,
    void *userData)
{
    c_interfaceWalk_t *data = userData;
    bool isBootstrap = !strcmp(g_getAttribute(data->g, "bootstrap"), "true");
    int hasProcedures = !corto_scope_walk(o, c_interfaceCheckProcedures, NULL);
    bool isTopLevelObject = (o == g_getCurrent(data->g)) && corto_instanceof(corto_package_o, o);

    /* Parse interfaces */
    if (hasProcedures || ((isTopLevelObject && !isBootstrap) && !strcmp(g_getProjectName(data->g), corto_idof(o)))) {
        if (data->walkAction(o, data)) {
            goto error;
        }
    }

    /* Walk scope of object */
    corto_objectseq scope = corto_scope_claim(o);
    corto_int32 i;
    for (i = 0; i < scope.length; i++) {
        if (g_mustParse(data->g, scope.buffer[i])) {
            if (!c_interfaceWalk(scope.buffer[i], data)) {
                break;
            }
        }
    }
    corto_scope_release(scope);
    if (i != scope.length) {
        goto error;
    }

    return 1;
error:
    return 0;
}

static
bool c_interfaceIsGenerated(
    corto_string file)
{
    corto_string content = corto_file_load(file);
    bool result = FALSE;

    if (content) {
        if (strlen(content) > 1024) {
            /* Token must appear before the first 1024 bytes */
            content[1024] = '\0';
        }

        if (strstr(content, GENERATED_MARKER)) {
            result = TRUE;
        }

        corto_dealloc(content);
    }
    return result;
}

static
bool c_interfaceWasGeneratedNow(
    corto_string name,
    c_interfaceWalk_t *data)
{
    corto_iter iter = corto_ll_iter(data->generated);

    while(corto_iter_hasNext(&iter)) {
        corto_string file = corto_iter_next(&iter);
        if (!strcmp(file, name)) {
            return TRUE;
        }
    }

    return FALSE;
}

static
bool c_interfaceFileExistedBeforeGenerating(
    corto_string name,
    c_interfaceWalk_t *data)
{
    corto_iter iter = corto_ll_iter(data->existedBeforeGenerating);

    while(corto_iter_hasNext(&iter)) {
        corto_string file = corto_iter_next(&iter);
        if (!strcmp(file, name)) {
            return TRUE;
        }
    }

    return FALSE;
}

/* Mark files that haven't been regenerated */
static
int c_interfaceMarkUnusedFiles(
    c_interfaceWalk_t *data)
{
    corto_ll files = corto_opendir("./src");
    corto_iter iter = corto_ll_iter(files);

    while(corto_iter_hasNext(&iter)) {
        corto_string file = corto_iter_next(&iter);
        corto_id id;
        sprintf(id, "./src/%s", file);
        if (c_interfaceIsGenerated(id)) {
            if (!c_interfaceWasGeneratedNow(file, data)) {
                if (!strstr(id, ".old")) {
                    corto_id newname;
                    sprintf(newname, "src/%s.old", file);
                    corto_rename (id, newname);
                    corto_info("stale file, please remove (renamed to %s.old)", file, file);
                } else {
                    id[strlen(id) - 4] = '\0';
                    char *orig = strrchr(id, '/');
                    if (orig) orig ++;

                    /* If original file was just generated, but it didn't exist before
                     * this generation, the newly generated file won't contain
                     * any implementation, and the old file likely has a more up
                     * to date version of the implementation. */
                    if (c_interfaceWasGeneratedNow(orig, data) &&
                        !c_interfaceFileExistedBeforeGenerating(orig, data))
                    {
                        corto_info("restoring '%s' to '%s'", file, orig);
                        corto_rm(strarg("src/%s", orig));
                        corto_rename(
                            strarg("src/%s", file),
                            strarg("src/%s", orig)
                        );
                    } else {
                        corto_info("%s: stale file, please remove", file);
                    }
                }
            }
        }
    }

    iter = corto_ll_iter(data->generated);
    while (corto_iter_hasNext(&iter)) {
        corto_dealloc(corto_iter_next(&iter));
    }

    corto_closedir(files);
    corto_ll_free(data->generated);

    return 0;
}

static
corto_int16 c_interfaceWriteMainSource(
    c_interfaceWalk_t *data)
{
    corto_id fileName, header;
    bool cpp = !strcmp(g_getAttribute(data->g, "c4cpp"), "true");

    sprintf(fileName, "main.%s", cpp ? "cpp" : "c");
    cdiff_file file = c_interfaceSourceFileOpen(NULL, fileName, data);
    if (!file) {
        goto error;
    }

    cdiff_file_write(file, "#include <%s>\n", c_mainheader(data->g, header));

    if (c_interfaceWriteMain(file, data)) {
        goto error;
    }

    if (cdiff_file_close(file)) {
        corto_throw(NULL);
        goto error;
    }

    return 0;
error:
    return -1;
}

void c_oldmain(
    g_generator g,
    char *fileName,
    corto_object o,
    const char *ext)
{
    corto_object package = c_findPackage(g, o);
    if (package) {
        sprintf(fileName, "src/%s.%s", corto_idof(package), ext);
    } else {
        sprintf(fileName, "src/%s.%s", g_getProjectName(g), ext);
    }
}

/* Entry point for generator */
int genmain(g_generator g) {
    c_interfaceWalk_t walkData;
    bool bootstrap = !strcmp(g_getAttribute(g, "bootstrap"), "true");
    bool cpp = !strcmp(g_getAttribute(g, "c4cpp"), "true");

    /* Create source and include directories */
    corto_mkdir("src");
    corto_mkdir("include");

    /* Check if an old-style main projectfile exists, and if it does rename it
     * to main */
    corto_id oldMain;
    c_oldmain(g, oldMain, g_getCurrent(g), cpp ? "cpp" : "c");
    if (corto_file_test(oldMain) == 1) {
        corto_id newMain;
        sprintf(newMain, "src/main.%s", cpp ? "cpp" : "c");
        if (corto_file_test(newMain) == 0) {
            corto_info("renaming %s to %s", oldMain, newMain);

            /* Old main exists, new main doesn't. Rename! */
            corto_rename(oldMain, newMain);
        }
    }

    walkData.existedBeforeGenerating = corto_opendir("src");

    /* Prepare walkData, create header- and sourcefile */
    walkData.g = g;
    walkData.source = NULL;
    walkData.wrapper = NULL;
    walkData.mainHeader = NULL;
    walkData.interfaceHeader = NULL;
    walkData.generated = corto_ll_new();
    walkData.mainWritten = FALSE;

    if (!bootstrap) {
        corto_id headerFileName, projectName;

        sprintf(headerFileName, "%s.h", g_getProjectName(g));
        strcpy(projectName, g_getName(g));

        g_file mainHeader = g_fileOpen(g, headerFileName);
        if (!mainHeader) {
            corto_throw("failed to open file '%s'", headerFileName);
            goto error;
        }

        walkData.mainHeader = mainHeader;

        /* Write header contents */
        if (c_interfaceHeaderWrite(
            g,
            mainHeader,
            g_getPackage(g),
            projectName,
            headerFileName,
            TRUE,
            &walkData))
        {
            goto error;
        }
    }

    if (g_getCurrent(g)) {
        walkData.interfaceHeader = g_fileOpen(g, "_interface.h");
        if (!walkData.interfaceHeader) {
            goto error;
        }

        corto_id path;
        if (bootstrap) {
            corto_path(path, root_o, g_getCurrent(g), "_");
        } else {
            corto_path(path, root_o, g_getPackage(g), "_");
        }
        strcat(path, "/interface");
        if (c_interfaceHeaderWrite(
            g,
            walkData.interfaceHeader,
            NULL,
            path,
            "_interface.h",
            FALSE,
            &walkData))
        {
            goto error;
        }
    }

    /* Walk objects, generate procedure headers & implementations */
    walkData.walkAction = c_interfaceObject;
    if (!g_walkNoScope(g, c_interfaceWalk, &walkData)) {
        goto error;
    }

    if (walkData.mainHeader) {
        c_interfaceHeaderFileClose(walkData.mainHeader, TRUE, &walkData);
    }

    if (walkData.interfaceHeader) {
        c_interfaceHeaderFileClose(walkData.interfaceHeader, FALSE, &walkData);
    }

    /* If none of the scopes that was generated code for had the same name as
     * the project, or if this project does not have a package, generate a main
     * source and header file separately */
    if (!walkData.mainWritten && !bootstrap) {
        if (c_interfaceWriteMainSource(&walkData)) {
            goto error;
        }
    }

    c_interfaceMarkUnusedFiles(&walkData);

    corto_closedir(walkData.existedBeforeGenerating);

    return 0;
error:
    return -1;
}
