#pragma once
#include <stdlib.h>

typedef enum {
	PACKAGE,
	IMPORT,
	INCLUDE_LIB,
	CLASS_NAME,
	ATT_VAR,
	FUNC,
	STATIC_FUNC,
	END_CLASS,
} MetaType;

typedef struct {
	MetaType type;
	char* name;
	char* rep;
	char* ret;
	char* file;
} MetaEntry;

#define CONCAT_IMPL(a,b) a##b
#define CONCAT(a,b) CONCAT_IMPL(a,b)

#define REGISTER_METAENTRY(m_type, m_name, m_rep, m_ret) \
	static const MetaEntry CONCAT(_meta_entry_, __COUNTER__) \
		__attribute__((used, section("classist_meta"), aligned(sizeof(void*)))) = {\
			.type = m_type,\
			.name = m_name,\
			.rep = m_rep,\
			.ret = m_ret,\
			.file = __FILE__,\
		}

#define package(pkg) REGISTER_METAENTRY(PACKAGE, NULL, #pkg, NULL)
#define import(pkg) REGISTER_METAENTRY(IMPORT, NULL, #pkg, NULL)
#define include(lib) REGISTER_METAENTRY(INCLUDE_LIB, NULL, #lib, NULL)
#define class(cls_name) REGISTER_METAENTRY(CLASS_NAME, #cls_name, NULL, NULL);
#define var(a_type, a_name) REGISTER_METAENTRY(ATT_VAR, #a_name, #a_type, NULL)
#define func(f_ret, f_name, f_sig) REGISTER_METAENTRY(FUNC, #f_name, #f_sig, #f_ret)
#define static(f_ret, f_name, f_sig) REGISTER_METAENTRY(STATIC_FUNC, #f_name, #f_sig, #f_ret)
#define endClass REGISTER_METAENTRY(END_CLASS, NULL, NULL, NULL);
