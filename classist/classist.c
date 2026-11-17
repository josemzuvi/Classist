#include "classist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

__attribute__((weak)) extern const MetaEntry __start_classist_meta[];
__attribute__((weak)) extern const MetaEntry __stop_classist_meta[];

typedef struct {
	char* name;
	char* type;
} Field;

typedef struct {
	char* name;
	char* ret;
	char* sig;
	bool is_static;
} Method;

typedef struct {
	char* raw_import;
} ImportDef;

typedef struct {
	char* raw_include;
} IncludeDef;

typedef struct {
	bool is_class;          // true if declared with class(...), false if plain module (imports/includes only)
	char* source_file;      // e.g. "src/main.cls.h"
	char* pkg;              // e.g. "animal" or ""
	char* pkg_path;         // e.g. "animal" or ""
	char* pkg_prefix;       // e.g. "animal" or ""
	char* full_name;        // e.g. "animal_Animal" or "main"
	char* name;             // e.g. "Animal" or "main"
	Field* fields;
	int field_count;
	int field_capacity;
	Method* methods;
	int method_count;
	int method_capacity;
	ImportDef* imports;
	int import_count;
	int import_capacity;
	IncludeDef* includes;
	int include_count;
	int include_capacity;
} UnitDef;

typedef struct {
	UnitDef* items;
	int count;
	int capacity;
} UnitRegistry;

static UnitRegistry registry = {0};

static char* pkg_to_path(const char* pkg) {
	if (!pkg || strlen(pkg) == 0) return strdup("");
	char* res = strdup(pkg);
	for (int i = 0; res[i]; i++) {
		if (res[i] == '.') res[i] = '/';
	}
	return res;
}

static char* pkg_to_prefix(const char* pkg) {
	if (!pkg || strlen(pkg) == 0) return strdup("");
	char* res = strdup(pkg);
	for (int i = 0; res[i]; i++) {
		if (res[i] == '.') res[i] = '_';
	}
	return res;
}

static void ensure_field_capacity(UnitDef* u) {
	if (u->field_count >= u->field_capacity) {
		u->field_capacity = u->field_capacity == 0 ? 8 : u->field_capacity * 2;
		u->fields = realloc(u->fields, sizeof(Field) * u->field_capacity);
	}
}

static void ensure_method_capacity(UnitDef* u) {
	if (u->method_count >= u->method_capacity) {
		u->method_capacity = u->method_capacity == 0 ? 8 : u->method_capacity * 2;
		u->methods = realloc(u->methods, sizeof(Method) * u->method_capacity);
	}
}

static void ensure_import_capacity(UnitDef* u) {
	if (u->import_count >= u->import_capacity) {
		u->import_capacity = u->import_capacity == 0 ? 8 : u->import_capacity * 2;
		u->imports = realloc(u->imports, sizeof(ImportDef) * u->import_capacity);
	}
}

static void ensure_include_capacity(UnitDef* u) {
	if (u->include_count >= u->include_capacity) {
		u->include_capacity = u->include_capacity == 0 ? 8 : u->include_capacity * 2;
		u->includes = realloc(u->includes, sizeof(IncludeDef) * u->include_capacity);
	}
}

static UnitDef* create_unit(bool is_class, const char* name, const char* current_pkg, const char* source_file) {
	if (registry.count >= registry.capacity) {
		registry.capacity = registry.capacity == 0 ? 8 : registry.capacity * 2;
		registry.items = realloc(registry.items, sizeof(UnitDef) * registry.capacity);
	}
	UnitDef* u = &registry.items[registry.count++];
	memset(u, 0, sizeof(UnitDef));
	u->is_class = is_class;
	u->name = strdup(name);
	u->source_file = source_file ? strdup(source_file) : strdup("");
	u->pkg = current_pkg ? strdup(current_pkg) : strdup("");
	u->pkg_path = pkg_to_path(u->pkg);
	u->pkg_prefix = pkg_to_prefix(u->pkg);

	if (u->pkg_prefix && strlen(u->pkg_prefix) > 0) {
		char full[512];
		snprintf(full, sizeof(full), "%s_%s", u->pkg_prefix, u->name);
		u->full_name = strdup(full);
	} else {
		u->full_name = strdup(u->name);
	}

	return u;
}

static void add_field(UnitDef* u, const char* name, const char* type) {
	ensure_field_capacity(u);
	Field* f = &u->fields[u->field_count++];
	f->name = strdup(name);
	f->type = strdup(type);
}

static void add_method(UnitDef* u, const char* name, const char* ret, const char* sig, bool is_static) {
	ensure_method_capacity(u);
	Method* m = &u->methods[u->method_count++];
	m->name = strdup(name);
	m->ret = strdup(ret ? ret : "void");
	m->sig = strdup(sig ? sig : "()");
	m->is_static = is_static;
}

static void add_import(UnitDef* u, const char* raw_import) {
	ensure_import_capacity(u);
	ImportDef* imp = &u->imports[u->import_count++];
	imp->raw_import = strdup(raw_import);
}

static void add_include(UnitDef* u, const char* raw_include) {
	ensure_include_capacity(u);
	IncludeDef* inc = &u->includes[u->include_count++];
	inc->raw_include = strdup(raw_include);
}

// Extracts the base filename without ".cls.h" extension
// e.g. "src/main.cls.h" -> "main", "animal/Animal.cls.h" -> "Animal"
static char* extract_file_basename(const char* file_path) {
	const char* slash = strrchr(file_path, '/');
	const char* start = slash ? slash + 1 : file_path;
	char* name = strdup(start);
	char* dot = strstr(name, ".cls.h");
	if (dot) *dot = '\0';
	return name;
}

static void parse_meta_entries() {
	if (!__start_classist_meta || !__stop_classist_meta) {
		return;
	}

	const MetaEntry* cur = __start_classist_meta;
	const MetaEntry* end = __stop_classist_meta;

	char* current_pkg = NULL;
	UnitDef* current_unit = NULL;
	const char* last_file = NULL;

	char* pending_imports[64];
	int pending_import_count = 0;

	char* pending_includes[64];
	int pending_include_count = 0;

	while (cur < end) {
		const char* cur_file = cur->file ? cur->file : "";

		// When changing files, if the previous file did not open a class, wrap it as a plain module unit
		if (last_file && strcmp(last_file, cur_file) != 0) {
			if (!current_unit && (pending_import_count > 0 || pending_include_count > 0)) {
				char* base_name = extract_file_basename(last_file);
				UnitDef* simple = create_unit(false, base_name, current_pkg, last_file);
				for (int i = 0; i < pending_import_count; i++) {
					add_import(simple, pending_imports[i]);
					free(pending_imports[i]);
				}
				pending_import_count = 0;

				for (int i = 0; i < pending_include_count; i++) {
					add_include(simple, pending_includes[i]);
					free(pending_includes[i]);
				}
				pending_include_count = 0;
				free(base_name);
			}
			// Reset current package between different files
			if (current_pkg) {
				free(current_pkg);
				current_pkg = NULL;
			}
			current_unit = NULL;
		}
		last_file = cur_file;

		switch (cur->type) {
			case PACKAGE:
				if (current_pkg) free(current_pkg);
				current_pkg = cur->rep ? strdup(cur->rep) : NULL;
				break;

			case IMPORT:
				if (cur->rep) {
					if (current_unit) {
						add_import(current_unit, cur->rep);
					} else if (pending_import_count < 64) {
						pending_imports[pending_import_count++] = strdup(cur->rep);
					}
				}
				break;

			case INCLUDE_LIB:
				if (cur->rep) {
					if (current_unit) {
						add_include(current_unit, cur->rep);
					} else if (pending_include_count < 64) {
						pending_includes[pending_include_count++] = strdup(cur->rep);
					}
				}
				break;

			case CLASS_NAME:
				current_unit = create_unit(true, cur->name, current_pkg, cur_file);
				for (int i = 0; i < pending_import_count; i++) {
					add_import(current_unit, pending_imports[i]);
					free(pending_imports[i]);
				}
				pending_import_count = 0;

				for (int i = 0; i < pending_include_count; i++) {
					add_include(current_unit, pending_includes[i]);
					free(pending_includes[i]);
				}
				pending_include_count = 0;
				break;

			case ATT_VAR:
				if (current_unit && current_unit->is_class) {
					add_field(current_unit, cur->name, cur->rep);
				}
				break;

			case FUNC:
				if (current_unit && current_unit->is_class) {
					add_method(current_unit, cur->name, cur->ret, cur->rep, false);
				}
				break;

			case STATIC_FUNC:
				if (current_unit && current_unit->is_class) {
					add_method(current_unit, cur->name, cur->ret, cur->rep, true);
				}
				break;

			case END_CLASS:
				current_unit = NULL;
				break;

			default:
				break;
		}
		cur++;
	}

	// Check the last processed file if it never opened a class
	if (!current_unit && (pending_import_count > 0 || pending_include_count > 0) && last_file) {
		char* base_name = extract_file_basename(last_file);
		UnitDef* simple = create_unit(false, base_name, current_pkg, last_file);
		for (int i = 0; i < pending_import_count; i++) {
			add_import(simple, pending_imports[i]);
			free(pending_imports[i]);
		}
		for (int i = 0; i < pending_include_count; i++) {
			add_include(simple, pending_includes[i]);
			free(pending_includes[i]);
		}
		free(base_name);
	}

	if (current_pkg) free(current_pkg);
}

static int mkdir_p(const char* path) {
	char tmp[1024];
	char* p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	if (tmp[len - 1] == '/')
		tmp[len - 1] = 0;

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
				return -1;
			}
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return -1;
	}
	return 0;
}

static bool file_exists(const char* filepath) {
	struct stat buffer;
	return (stat(filepath, &buffer) == 0);
}

static void replace_type_identifier(char* out, size_t out_sz, const char* str, const char* old_name, const char* new_name) {
	size_t old_len = strlen(old_name);
	out[0] = '\0';
	const char* p = str;
	while (*p) {
		if (strncmp(p, old_name, old_len) == 0) {
			bool boundary_before = (p == str) || (!isalnum((unsigned char)*(p - 1)) && *(p - 1) != '_');
			bool boundary_after = (!isalnum((unsigned char)*(p + old_len)) && *(p + old_len) != '_');
			if (boundary_before && boundary_after) {
				strncat(out, new_name, out_sz - strlen(out) - 1);
				p += old_len;
				continue;
			}
		}
		size_t cur_len = strlen(out);
		if (cur_len + 1 < out_sz) {
			out[cur_len] = *p;
			out[cur_len + 1] = '\0';
		}
		p++;
	}
}

static void format_params(char* out, size_t out_sz, const UnitDef* u, const char* raw_sig, bool is_static) {
	const char* s = raw_sig ? raw_sig : "()";
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '(') s++;

	char inner[512];
	strncpy(inner, s, sizeof(inner));
	inner[sizeof(inner) - 1] = '\0';
	char* end = strrchr(inner, ')');
	if (end) *end = '\0';

	char* p = inner;
	while (*p == ' ' || *p == '\t') p++;
	char* q = p + strlen(p) - 1;
	while (q >= p && (*q == ' ' || *q == '\t')) { *q = '\0'; q--; }

	char replaced[512];
	replace_type_identifier(replaced, sizeof(replaced), p, u->name, u->full_name);

	if (is_static) {
		if (strlen(replaced) == 0 || strcmp(replaced, "void") == 0) {
			snprintf(out, out_sz, "(void)");
		} else {
			snprintf(out, out_sz, "(%s)", replaced);
		}
	} else {
		if (strlen(replaced) == 0 || strcmp(replaced, "void") == 0) {
			snprintf(out, out_sz, "(%s* this)", u->full_name);
		} else {
			snprintf(out, out_sz, "(%s* this, %s)", u->full_name, replaced);
		}
	}
}

static void generate_header(const UnitDef* u, const char* out_dir) {
	char dir_path[1024];
	char file_path[1024];

	if (u->pkg_path && strlen(u->pkg_path) > 0) {
		snprintf(dir_path, sizeof(dir_path), "%s/%s", out_dir, u->pkg_path);
		snprintf(file_path, sizeof(file_path), "%s/%s/%s.h", out_dir, u->pkg_path, u->name);
	} else {
		snprintf(dir_path, sizeof(dir_path), "%s", out_dir);
		snprintf(file_path, sizeof(file_path), "%s/%s.h", out_dir, u->name);
	}

	if (mkdir_p(dir_path) != 0) {
		fprintf(stderr, "[Classist] Error creating directory %s: %s\n", dir_path, strerror(errno));
		return;
	}

	FILE* f = fopen(file_path, "w");
	if (!f) {
		fprintf(stderr, "[Classist] Error opening file %s for writing: %s\n", file_path, strerror(errno));
		return;
	}

	printf("[Classist] Generating header: %s\n", file_path);

	fprintf(f, "// Automatically generated by Classist. DO NOT EDIT DIRECTLY.\n");
	fprintf(f, "#pragma once\n\n");

	if (u->include_count > 0) {
		for (int i = 0; i < u->include_count; i++) {
			fprintf(f, "#include %s\n", u->includes[i].raw_include);
		}
		fprintf(f, "\n");
	}

	if (u->is_class) {
		// 1. Data struct definition
		fprintf(f, "typedef struct {\n");
		for (int i = 0; i < u->field_count; i++) {
			char field_type[256];
			replace_type_identifier(field_type, sizeof(field_type), u->fields[i].type, u->name, u->full_name);
			fprintf(f, "\t%s %s;\n", field_type, u->fields[i].name);
		}
		fprintf(f, "} %s;\n\n", u->full_name);

		// 2. Class function table struct
		fprintf(f, "struct __type__class__%s {\n", u->full_name);
		for (int i = 0; i < u->method_count; i++) {
			Method* m = &u->methods[i];
			char params[512];
			char ret_type[256];
			replace_type_identifier(ret_type, sizeof(ret_type), m->ret, u->name, u->full_name);
			format_params(params, sizeof(params), u, m->sig, m->is_static);
			fprintf(f, "\t%s (*%s)%s;\n", ret_type, m->name, params);
		}
		fprintf(f, "};\n\n");

		fprintf(f, "extern const struct __type__class__%s __class__%s;\n\n", u->full_name, u->full_name);

		// 3. Ergonomic macro accessor
		fprintf(f, "#ifdef import_%s\n\n", u->full_name);
		fprintf(f, "#define %s_type() %s\n", u->name, u->full_name);
		fprintf(f, "#define %s_class(function) __class__%s.function\n", u->name, u->full_name);
		fprintf(f, "#define %s_macro(_1, MACRO, ...) MACRO\n", u->name);
		fprintf(f, "#define %s(...) %s_macro(__VA_ARGS__ __VA_OPT__(,) %s_class, %s_type)(__VA_ARGS__)\n\n",
			u->name, u->name, u->name, u->name);
		fprintf(f, "#endif\n\n");
	}

	// Compilation unit block #ifdef ..._C
	fprintf(f, "#ifdef %s_C\n\n", u->full_name);

	if (u->is_class) {
		fprintf(f, "#ifndef import_%s\n", u->full_name);
		fprintf(f, "#define import_%s\n", u->full_name);
		fprintf(f, "#define %s_type() %s\n", u->name, u->full_name);
		fprintf(f, "#define %s_class(function) __class__%s.function\n", u->name, u->full_name);
		fprintf(f, "#define %s_macro(_1, MACRO, ...) MACRO\n", u->name);
		fprintf(f, "#define %s(...) %s_macro(__VA_ARGS__ __VA_OPT__(,) %s_class, %s_type)(__VA_ARGS__)\n",
			u->name, u->name, u->name, u->name);
		fprintf(f, "#endif\n\n");
	}

	if (u->import_count > 0) {
		fprintf(f, "// Imports required by %s\n", u->name);
		for (int i = 0; i < u->import_count; i++) {
			const char* imp = u->imports[i].raw_import;
			char flag[512];
			snprintf(flag, sizeof(flag), "import_%s", imp);
			for (int k = 0; flag[k]; k++) {
				if (flag[k] == '.') flag[k] = '_';
			}
			char path[512];
			snprintf(path, sizeof(path), "%s.h", imp);
			char* dot = strrchr(path, '.');
			for (int k = 0; path + k < dot; k++) {
				if (path[k] == '.') path[k] = '/';
			}

			fprintf(f, "#ifndef %s\n", flag);
			fprintf(f, "#define %s\n", flag);
			fprintf(f, "#endif\n");
			fprintf(f, "#include \"%s\"\n\n", path);
		}
	}

	if (u->is_class) {
		fprintf(f, "// Macro to instantiate and initialize the class function table\n");
		fprintf(f, "#define endClass \\\n");
		fprintf(f, "const struct __type__class__%s __class__%s = { \\\n", u->full_name, u->full_name);
		for (int i = 0; i < u->method_count; i++) {
			Method* m = &u->methods[i];
			fprintf(f, "\t.%s = %s, \\\n", m->name, m->name);
		}
		fprintf(f, "};\n\n");
	}

	fprintf(f, "#endif // %s_C\n", u->full_name);

	fclose(f);
}

static void ensure_src_skeleton(const UnitDef* u, const char* src_dir) {
	char dir_path[1024];
	char file_path[1024];

	if (u->pkg_path && strlen(u->pkg_path) > 0) {
		snprintf(dir_path, sizeof(dir_path), "%s/%s", src_dir, u->pkg_path);
		snprintf(file_path, sizeof(file_path), "%s/%s/%s.c", src_dir, u->pkg_path, u->name);
	} else {
		snprintf(dir_path, sizeof(dir_path), "%s", src_dir);
		snprintf(file_path, sizeof(file_path), "%s/%s.c", src_dir, u->name);
	}

	if (file_exists(file_path)) {
		printf("[Classist] Existing source code in %s (will not overwrite)\n", file_path);
		return;
	}

	if (mkdir_p(dir_path) != 0) {
		fprintf(stderr, "[Classist] Error creating directory %s: %s\n", dir_path, strerror(errno));
		return;
	}

	FILE* f = fopen(file_path, "w");
	if (!f) {
		fprintf(stderr, "[Classist] Error creating skeleton %s: %s\n", file_path, strerror(errno));
		return;
	}

	printf("[Classist] Creating skeleton source: %s\n", file_path);

	fprintf(f, "#define %s_C\n", u->full_name);
	if (u->pkg_path && strlen(u->pkg_path) > 0) {
		fprintf(f, "#include \"%s/%s.h\"\n\n", u->pkg_path, u->name);
	} else {
		fprintf(f, "#include \"%s.h\"\n\n", u->name);
	}

	if (u->is_class) {
		for (int i = 0; i < u->method_count; i++) {
			Method* m = &u->methods[i];
			char params[512];
			char ret_type[256];
			replace_type_identifier(ret_type, sizeof(ret_type), m->ret, u->name, u->full_name);
			format_params(params, sizeof(params), u, m->sig, m->is_static);

			fprintf(f, "static %s %s%s {\n", ret_type, m->name, params);
			fprintf(f, "\t// TODO: Implement %s\n", m->name);
			if (strcmp(ret_type, "void") != 0) {
				fprintf(f, "\treturn (%s){0};\n", ret_type);
			}
			fprintf(f, "}\n\n");
		}
		fprintf(f, "endClass\n");
	} else {
		// Plain module (e.g. main.cls.h)
		if (strcmp(u->name, "main") == 0) {
			fprintf(f, "int main(int argc, char** argv) {\n");
			fprintf(f, "\t// TODO: Main program code\n");
			fprintf(f, "\treturn 0;\n");
			fprintf(f, "}\n");
		}
	}

	fclose(f);
}

int main(int argc, char** argv) {
	const char* out_dir = "generated";
	const char* src_dir = "src";

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
			out_dir = argv[++i];
		} else if (strcmp(argv[i], "--src") == 0 && i + 1 < argc) {
			src_dir = argv[++i];
		}
	}

	parse_meta_entries();

	printf("[Classist] Processed %d unit(s) in memory.\n", registry.count);

	for (int i = 0; i < registry.count; i++) {
		UnitDef* u = &registry.items[i];
		printf(" -> [%s] %s, Package: '%s', Fields: %d, Methods: %d, Imports: %d, Includes: %d\n",
			u->is_class ? "Class" : "Module",
			u->name, u->pkg, u->field_count, u->method_count, u->import_count, u->include_count);
		generate_header(u, out_dir);
		ensure_src_skeleton(u, src_dir);
	}

	return 0;
}
