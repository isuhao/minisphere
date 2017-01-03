#include "cell.h"
#include "script.h"

#include "api.h"
#include "build.h"
#include "fs.h"
#include "target.h"
#include "tool.h"
#include "utility.h"

static build_t*  get_current_build (duk_context* js);
static void      make_file_targets (const char* wildcard, const path_t* path, const path_t* subdir, vector_t* targets, bool recursive);

static duk_ret_t js_files           (duk_context* ctx);
static duk_ret_t js_system_name     (duk_context* ctx);
static duk_ret_t js_system_version  (duk_context* ctx);
static duk_ret_t js_new_Tool        (duk_context* ctx);
static duk_ret_t js_Tool_finalize   (duk_context* ctx);
static duk_ret_t js_Tool_build      (duk_context* ctx);
static duk_ret_t js_Target_finalize (duk_context* ctx);
static duk_ret_t js_Target_get_name (duk_context* ctx);
static duk_ret_t js_Target_get_path (duk_context* ctx);

bool
script_eval(build_t* build)
{
	void*        file_data;
	size_t       file_size;
	duk_context* js_env;
	path_t*      script_path;
	lstring_t*   source;

	script_path = path_dup(build_in_path(build));
	path_append(script_path, "Cellscript.js");
	if (!(file_data = fslurp(path_cstr(script_path), &file_size))) {
		printf("ERROR: unable to open Cellscript.js, does it exist?\n");
		return false;
	}
	source = lstr_from_cp1252(file_data, file_size);
	free(file_data);
	
	// note: no fatal error handler set here.  if a JavaScript exception is thrown
	//       and nothing catches it, Cell will crash.
	js_env = duk_create_heap_default();
	
	// initialize the Cellscript API
	api_init(js_env);
	api_define_function(js_env, NULL, "files", js_files);
	api_define_function(js_env, "system", "name", js_system_name);
	api_define_function(js_env, "system", "version", js_system_version);
	api_define_class(js_env, "Target", NULL, js_Target_finalize);
	api_define_property(js_env, "Target", "name", js_Target_get_name, NULL);
	api_define_property(js_env, "Target", "path", js_Target_get_path, NULL);
	api_define_class(js_env, "Tool", js_new_Tool, js_Tool_finalize);
	api_define_method(js_env, "Tool", "build", js_Tool_build);

	// stash the build pointer for easier access by API calls
	duk_push_global_stash(js_env);
	duk_push_pointer(js_env, build);
	duk_put_prop_string(js_env, -2, "buildPtr");
	duk_pop(js_env);

	// execute the Cellscript
	duk_push_lstring(js_env, lstr_cstr(source), lstr_len(source));
	duk_push_string(js_env, "Cellscript.js");
	duk_compile(js_env, 0x0);
	if (duk_pcall(js_env, 0) != DUK_EXEC_SUCCESS)
		printf("%s\n", duk_to_string(js_env, -1));
	duk_destroy_heap(js_env);
	lstr_free(source);
	
	printf("ERROR: not implemented yet\n");

	return false;
}

static build_t*
get_current_build(duk_context* js)
{
	build_t* build;

	duk_push_global_stash(js);
	duk_get_prop_string(js, -1, "buildPtr");
	build = duk_get_pointer(js, -1);
	duk_pop_2(js);
	return build;
}

static void
make_file_targets(const char* wildcard, const path_t* path, const path_t* subdir, vector_t* targets, bool recursive)
{
	// note: 'targets' should be a vector_t initialized to sizeof(target_t*).

	path_t*      file_path;
	vector_t*    list;
	path_t*      name;
	target_t*    target;

	iter_t iter;
	path_t* *p_path;

	list = fs_list_dir(NULL, path_cstr(path));
	
	iter = vector_enum(list);
	while (p_path = vector_next(&iter)) {
		if (!path_is_file(*p_path) && recursive) {
			name = path_new_dir(path_filename(*p_path));
			file_path = path_dup(*p_path);
			if (subdir != NULL)
				path_rebase(name, subdir);
			make_file_targets(wildcard, file_path, name, targets, true);
			path_free(file_path);
			path_free(name);
		}
		else if (path_is_file(*p_path) && wildcmp(path_filename(*p_path), wildcard)) {
			name = path_new(path_filename(*p_path));
			file_path = path_dup(*p_path);
			if (subdir != NULL)
				path_rebase(name, subdir);
			target = target_new(name, file_path, NULL);
			vector_push(targets, &target);
			path_free(file_path);
			path_free(name);
		}
	}

	iter = vector_enum(list);
	while (p_path = vector_next(&iter))
		path_free(*p_path);
	vector_free(list);
}

static duk_ret_t
js_files(duk_context* ctx)
{
	int         num_args;
	const char* pattern;
	path_t*     path;
	bool        recursive = false;
	vector_t*   targets;
	char*       wildcard;

	iter_t iter;
	target_t* *p;

	num_args = duk_get_top(ctx);
	pattern = duk_require_string(ctx, 0);
	if (num_args >= 2)
		recursive = duk_require_boolean(ctx, 1);

	// extract the wildcard, if any, from the given path
	path = path_new(pattern);
	if (!path_is_file(path))
		wildcard = strdup("*");
	else {
		wildcard = strdup(path_filename(path));
		path_strip(path);
	}

	// this is potentially recursive, so we defer to make_file_targets() to construct
	// the targets.  note: 'path' should always be a directory at this point.
	targets = vector_new(sizeof(target_t*));
	make_file_targets(wildcard, path, NULL, targets, true);
	free(wildcard);

	// return all the newly constructed targets as an array.
	duk_push_array(ctx);
	iter = vector_enum(targets);
	while (p = vector_next(&iter)) {
		duk_push_class_obj(ctx, "Target", *p);
		duk_put_prop_index(ctx, -2, (duk_uarridx_t)iter.index);
	}
	return 1;
}

static duk_ret_t
js_system_name(duk_context* ctx)
{
	duk_push_string(ctx, COMPILER_NAME);
	return 1;
}

static duk_ret_t
js_system_version(duk_context* ctx)
{
	duk_push_string(ctx, VERSION_NAME);
	return 1;
}

static duk_ret_t
js_Target_finalize(duk_context* ctx)
{
	target_t* target;

	target = duk_require_class_obj(ctx, 0, "Target");

	target_free(target);
	return 0;
}

static duk_ret_t
js_Target_get_name(duk_context* ctx)
{
	target_t* target;

	duk_push_this(ctx);
	target = duk_require_class_obj(ctx, -1, "Target");

	duk_push_string(ctx, path_cstr(target_name(target)));
	return 1;
}

static duk_ret_t
js_Target_get_path(duk_context* ctx)
{
	target_t* target;

	duk_push_this(ctx);
	target = duk_require_class_obj(ctx, -1, "Target");
	
	duk_push_string(ctx, path_cstr(target_path(target)));
	return 1;
}

static duk_ret_t
js_new_Tool(duk_context* ctx)
{
	tool_t* tool;

	if (!duk_is_constructor_call(ctx))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "constructor requires 'new'");
	duk_require_function(ctx, 0);

	tool = tool_new(ctx, 0);

	duk_push_this(ctx);
	duk_to_class_obj(ctx, -1, "Tool", tool);
	return 1;
}

static duk_ret_t
js_Tool_finalize(duk_context* ctx)
{
	tool_t* tool;

	tool = duk_require_class_obj(ctx, 0, "Tool");

	tool_free(tool);
	return 0;
}

static duk_ret_t
js_Tool_build(duk_context* ctx)
{
	duk_uarridx_t length;
	path_t*       name;
	path_t*       out_path;
	target_t*     source;
	target_t*     target;
	tool_t*       tool;

	duk_uarridx_t i;

	duk_push_this(ctx);
	tool = duk_require_class_obj(ctx, -1, "Tool");
	out_path = path_new(duk_require_string(ctx, 0));
	if (duk_is_array(ctx, 1))
		duk_error_blame(ctx, -1, DUK_ERR_TYPE_ERROR, "array expected (argument 2)");

	name = path_new(path_filename(out_path));
	target = target_new(name, out_path, tool);
	length = (duk_uarridx_t)duk_get_length(ctx, 1);
	for (i = 0; i < length; ++i) {
		duk_get_prop_index(ctx, 1, i);
		source = duk_require_class_obj(ctx, -1, "Target");
		target_add_source(target, source);
		duk_pop(ctx);
	}
	path_free(out_path);

	duk_push_class_obj(ctx, "Target", target);
	return 1;
}