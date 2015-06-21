#include "minisphere.h"
#include "api.h"
#include "utility.h"

struct script
{
	unsigned int  refcount;
	bool          is_in_use;
	duk_uarridx_t id;
};

static script_t* script_from_js_function (void* heapptr);

static bool s_have_coffeescript = false;
static int  s_next_id = 0;

void
initialize_scripts(void)
{
	duk_push_global_stash(g_duk);
	duk_push_array(g_duk);
	duk_put_prop_string(g_duk, -2, "scripts");
	duk_pop(g_duk);

	// load CoffeeScript compiler if it exists
	console_log(1, "Initializing CoffeeScript\n");
	if (sfs_fexist(g_fs, "~sys/coffee-script.js", NULL)) {
		if (!try_evaluate_file("~sys/coffee-script.js"))
			duk_throw(g_duk);
		s_have_coffeescript = true;
	}
	else {
		console_log(1, "~sys/coffee-script.js not found");
		s_have_coffeescript = false;
	}
}

bool
try_evaluate_file(const char* path)
{
	const char* extension;
	sfs_file_t* file = NULL;
	lstring_t*  source;
	char*       slurp;
	size_t      size;

	// load the source text from the script file
	if (!(slurp = sfs_fslurp(g_fs, path, "scripts", &size)))
		goto on_error;
	source = lstr_from_buf(slurp, size);
	free(slurp);

	// is it a CoffeeScript file?
	extension = strrchr(path, '.');
	if (extension != NULL && strcasecmp(extension, ".coffee") == 0) {
		if (!s_have_coffeescript) {
			duk_push_error_object(g_duk, DUK_ERR_ERROR,
				"No CoffeeScript support, unable to compile '%s'\n", path);
			goto on_error;
		}
		duk_push_global_object(g_duk);
		duk_get_prop_string(g_duk, -1, "CoffeeScript");
		duk_get_prop_string(g_duk, -1, "compile");
		duk_push_lstring_t(g_duk, source);
		if (duk_pcall(g_duk, 1) != DUK_EXEC_SUCCESS)
			goto on_error;
		duk_remove(g_duk, -2);
		duk_remove(g_duk, -2);
	}
	else
		duk_push_lstring_t(g_duk, source);
	
	// ready for launch in T-10...9...*munch*
	duk_push_string(g_duk, path);
	if (duk_pcompile(g_duk, DUK_COMPILE_EVAL) != DUK_EXEC_SUCCESS)
		goto on_error;
	if (duk_pcall(g_duk, 0) != DUK_EXEC_SUCCESS)
		goto on_error;
	return true;

on_error:
	if (!duk_is_error(g_duk, -1))
		duk_push_error_object(g_duk, DUK_ERR_ERROR, "Script '%s' not found\n", path);
	return false;
}

script_t*
compile_script(const lstring_t* source, const char* fmt_name, ...)
{
	va_list ap;
	
	script_t* script;
	
	if (!(script = calloc(1, sizeof(script_t))))
		return NULL;
	
	// duk_get_heapptr() doesn't give us a strong reference, so we stash
	// the compiled script to avoid garbage collection mishaps.
	duk_push_global_stash(g_duk);
	duk_get_prop_string(g_duk, -1, "scripts");
	duk_push_lstring_t(g_duk, source);
	va_start(ap, fmt_name);
	duk_push_vsprintf(g_duk, fmt_name, ap);
	va_end(ap);
	duk_compile(g_duk, 0x0);
	duk_put_prop_index(g_duk, -2, s_next_id);
	duk_pop_2(g_duk);

	script->id = s_next_id++;
	return ref_script(script);
}

script_t*
ref_script(script_t* script)
{
	++script->refcount;
	return script;
}

void
free_script(script_t* script)
{
	if (script == NULL || --script->refcount > 0)
		return;
	
	// unstash the compiled function, it's now safe to GC
	duk_push_global_stash(g_duk);
	duk_get_prop_string(g_duk, -1, "scripts");
	duk_del_prop_index(g_duk, -1, script->id);
	duk_pop_2(g_duk);

	free(script);
}

void
run_script(script_t* script, bool allow_reentry)
{
	bool was_in_use;

	if (script == NULL)  // NULL is allowed, it's a no-op
		return;
	
	if (script->is_in_use && !allow_reentry)
		return;  // do nothing if an instance is already running
	was_in_use = script->is_in_use;

	// ref the script in case it gets freed during execution. the owner
	// may be destroyed in the process and we don't want to crash.
	ref_script(script);
	
	// get the compiled script from the stash and run it
	script->is_in_use = true;
	duk_push_global_stash(g_duk);
	duk_get_prop_string(g_duk, -1, "scripts");
	duk_get_prop_index(g_duk, -1, script->id);
	duk_call(g_duk, 0);
	duk_pop_3(g_duk);
	script->is_in_use = was_in_use;

	free_script(script);
}

script_t*
duk_require_sphere_script(duk_context* ctx, duk_idx_t index, const char* name)
{
	lstring_t* codestring;
	script_t*  script;

	index = duk_require_normalize_index(ctx, index);

	if (duk_is_callable(ctx, index)) {
		// caller passed function directly
		script = script_from_js_function(duk_get_heapptr(ctx, index));
	}
	else if (duk_is_string(ctx, index)) {
		// caller passed code string, compile it
		codestring = duk_require_lstring_t(ctx, index);
		script = compile_script(codestring, false, "%s", name);
		lstr_free(codestring);
	}
	else if (duk_is_null_or_undefined(ctx, index))
		return NULL;
	else
		duk_error_ni(ctx, -1, DUK_ERR_TYPE_ERROR, "Script must be string, function, or null/undefined");
	return script;
}

static script_t*
script_from_js_function(void* heapptr)
{
	script_t* script;

	if (!(script = calloc(1, sizeof(script_t))))
		return NULL;

	duk_push_global_stash(g_duk);
	if (!duk_get_prop_string(g_duk, -1, "scripts")) {
		duk_pop(g_duk);
		duk_push_array(g_duk); duk_put_prop_string(g_duk, -2, "scripts");
		duk_get_prop_string(g_duk, -1, "scripts");
	}
	script->id = s_next_id++;
	duk_push_heapptr(g_duk, heapptr);
	duk_put_prop_index(g_duk, -2, script->id);
	duk_pop_2(g_duk);

	return ref_script(script);
}
