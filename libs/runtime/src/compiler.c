#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <unicode/utypes.h>
#include <unicode/ucnv.h>

#include "c-ohvu/io/stringref.h"
#include "c-ohvu/data/bdtrie.h"
#include "c-ohvu/data/sexpr.h"
#include "c-ohvu/runtime/compiler.h"

typedef struct variable_bindings {
	uint32_t capture_count;
	uint32_t param_count;
	ovru_variable* captures;
	bdtrie variables;
} variable_bindings;

void* get_variable_binding(uint32_t key_size, const void* key_data, const void* value_data, bdtrie_node* owner) {
	ovru_variable* value = malloc(sizeof(ovru_variable*));
	*value = *((ovru_variable*)value_data);
	return value;
}

void update_variable_binding(void* value, bdtrie_node* owner) {}

void free_variable_binding(void* value) {
	free(value);
}

variable_bindings make_variable_bindings(uint64_t param_count, const ovs_expr_ref** params) {
	variable_bindings b = {
		0,
		param_count,
		NULL,
		{
			NULL,
			get_variable_binding,
			update_variable_binding,
			free_variable_binding
		}
	};
	for (int i = 0; i < param_count; i++) {
		ovru_variable v = { OVRU_PARAMETER, i };
		bdtrie_insert(&b.variables, sizeof(ovs_expr_ref*), params + i, &v);
	}
	return b;
}

typedef struct compile_context {
	struct compile_context* parent;
	ovs_context* ovs_context;
	variable_bindings bindings;
} compile_context;

ovru_result find_variable(ovru_variable* result, compile_context* c, const ovs_expr_ref* symbol) {
	ovru_variable v = { OVRU_CAPTURE, c->bindings.capture_count };
	ovru_variable w = *(ovru_variable*)bdtrie_find_or_insert(&c->bindings.variables, sizeof(ovs_expr_ref*), &symbol, &v).data;

	if (w.index == v.index && w.type == v.type) {
		if (c->parent == NULL) {
			ovs_expr e = { OVS_SYMBOL, .p=symbol };
			printf("\nVariable Not In Scope\n  ");
			ovs_dump_expr(e);
			return OVRU_VARIABLE_NOT_IN_SCOPE;
		}
		ovru_variable capture;
		ovru_result r = find_variable(&capture, c->parent, symbol);
		if (r != OVRU_SUCCESS) {
			return r;
		}

		c->bindings.capture_count++;
	
		ovru_variable* old_captures = c->bindings.captures;
		c->bindings.captures = malloc(sizeof(ovru_variable*) * c->bindings.capture_count);
		if (old_captures != NULL) {
			memcpy(c->bindings.captures, old_captures, sizeof(uint32_t*) * (c->bindings.capture_count - 1));
			free(old_captures);
		}

		c->bindings.captures[c->bindings.capture_count - 1] = capture;
	}

	*result = w;
	return OVRU_SUCCESS;
}

ovru_result compile_quote(ovru_term* result, int32_t part_count, ovs_expr* parts, compile_context* c, ovs_expr e) {
	if (part_count != 1) {
		printf("\nInvalid Quote Length\n  ");
		ovs_dump_expr(e);
		return OVRU_INVALID_QUOTE_LENGTH;
	}

	*result = (ovru_term){ .quote=parts[0] };

	return OVRU_SUCCESS;
}

ovru_result compile_statement(ovru_statement* result, ovs_expr s, compile_context* c);

void* get_ref(ovs_expr e) {
	return (void*)ovs_ref(e.p);
}

ovru_result compile_lambda(ovru_term* result, int32_t part_count, ovs_expr* parts, compile_context* c, ovs_expr e) {
	if (part_count != 2) {
		printf("\nInvalid Lambda Length\n  ");
		ovs_dump_expr(e);
		return OVRU_INVALID_LAMBDA_LENGTH;
	}

	ovs_expr params_decl = parts[0];
	ovs_expr body_decl = parts[1];

	ovs_expr_ref** params;
	int32_t param_count = ovs_delist_of(c->ovs_context->root_tables, params_decl, (void***)&params, get_ref);
	if (param_count < 0) {
		printf("\nInvalid Parameter Terminator\n  ");
		ovs_dump_expr(e);
		return OVRU_INVALID_PARAMETER_TERMINATOR;
	}

	compile_context lambda_context = {
		c,
		c->ovs_context,
		make_variable_bindings(param_count, (const ovs_expr_ref**) params)
	};

	ovru_statement body;
	ovru_result success = compile_statement(&body, body_decl, &lambda_context);

	if (success == OVRU_SUCCESS) {
		ovru_lambda* l = malloc(sizeof(ovru_lambda));
		l->ref_count = ATOMIC_VAR_INIT(1);
		l->param_count = param_count;
		l->params = params;
		l->capture_count = lambda_context.bindings.capture_count;
		l->captures = lambda_context.bindings.captures;
		l->body = body;
		*result = (ovru_term){ .type=OVRU_LAMBDA, .lambda=l };
	} else {
		for (int i = 0; i < param_count; i++) {
			ovs_free(OVS_SYMBOL, params[i]);
		}
		if (param_count > 0) {
			free(params);
		}
		if (lambda_context.bindings.capture_count > 0) {
			free(lambda_context.bindings.captures);
		}
	}

	bdtrie_clear(&lambda_context.bindings.variables);

	return success;
}

ovru_result compile_expression(ovru_term* result, ovs_expr e, compile_context* c) {
	if (ovs_is_symbol(e)) {
		ovru_variable v;
		ovru_result r = find_variable(&v, c, e.p);
		if (r == OVRU_SUCCESS) {
			*result = (ovru_term){ .type=OVRU_VARIABLE, .variable=v };
		}
		return r;
	}

	ovs_expr* parts;
	uint32_t count = ovs_delist(c->ovs_context->root_tables + OVS_UNQUALIFIED, e, &parts);
	if (count <= 0) {
		if (count == 0) {
			printf("\nEmpty Expression\n  ");
			ovs_dump_expr(e);
			return OVRU_EMPTY_EXPRESSION;
		} else {
			printf("\nInvalid Expression Terminator\n  ");
			ovs_dump_expr(e);
			return OVRU_INVALID_EXPRESSION_TERMINATOR;
		}
	}

	ovs_expr kind = parts[0];
	int32_t term_count = count - 1;
	ovs_expr* terms = parts + 1;

	ovru_result success;
	if (ovs_is_eq(kind, ovs_root_symbol(OVS_DATA_QUOTE)->expr)) {
		success = compile_quote(result, term_count, terms, c, e);

	} else if (ovs_is_eq(kind, ovs_root_symbol(OVS_DATA_LAMBDA)->expr)) {
		success = compile_lambda(result, term_count, terms, c, e);

	} else {
		printf("\nInvalid Expression Type\n  ");
		ovs_dump_expr(e);
		success = OVRU_INVALID_EXPRESSION_TYPE;
	}

	for (int i = 0; i < count; i++) {
		ovs_dealias(parts[i]);
	}
	free(parts);
	
	return success;
}

ovru_result compile_statement(ovru_statement* result, ovs_expr s, compile_context* c) {
	ovs_expr* expressions;
	int32_t count = ovs_delist(c->ovs_context->root_tables + OVS_UNQUALIFIED, s, &expressions);
	if (count <= 0) {
		if (count == 0) {
			printf("\nEmpty Statement\n  ");
			ovs_dump_expr(s);
			return OVRU_EMPTY_STATEMENT;
		} else {
			printf("\nInvalid Statement Terminator\n  ");
			ovs_dump_expr(s);
			return OVRU_INVALID_STATEMENT_TERMINATOR;
		}
	}

	ovru_term* terms = malloc(sizeof(ovru_term) * count);
	ovru_result success;
	for (int i = 0; i < count; i++) {
		success = compile_expression(&terms[i], expressions[i], c);
		ovs_dealias(expressions[i]);

		if (success != OVRU_SUCCESS) {
			for (i++; i < count; i++) {
				ovs_dealias(expressions[i]);
			}
			break;
		}
	}
	free(expressions);

	if (success != OVRU_SUCCESS) {
		free(terms);
	}

	*result = (ovru_statement){ count, terms };

	return success;
}

void compile_parameters(compile_context* result, ovs_context* c, const uint32_t param_count, const ovs_expr_ref** params) {
	*result = (compile_context){
		NULL,
		c,
		make_variable_bindings(param_count, params)
	};
}

ovru_result ovru_compile(ovru_statement* result, ovs_context* c, const ovs_expr e, const uint32_t param_count, const ovs_expr_ref** params) {
	compile_context context;
	compile_parameters(&context, c, param_count, params);

	ovru_result success = compile_statement(result, e, &context);

	if (context.bindings.capture_count > 0) {
		free(context.bindings.captures);
	}
	bdtrie_clear(&context.bindings.variables);

	return success;
}

/*
 * API Surface
 */

/*
 * Statement Function
 */

typedef enum statement_function_type {
	STATEMENT_WITH_LAMBDA,
	STATEMENT_WITH_VARIABLE,
	STATEMENT_WITH_QUOTE,
	STATEMENT_END
} statement_function;

typedef struct statement_data {
	statement_function_type type;
	compile_context* context;
	ovru_statement statement;
	ovs_expr_ref* parent;
} statement_data;

void statement_with_term(statement_data* s, statement_data* w, ovru_term t) {
	s->context = w->context;
	s->statement.term_count = w->statement.term_count + 1;
	s->statement.terms = malloc(sizeof(ovru_term) * s->statement.term_count);
	s->parent = w->parent;
	for (int i = 0; i < w->statement.term_count; i++) {
		s->statement.terms[i] = w->statement.terms[i];
	}
	s->statement.terms[w->statement.term_count] = t;
}

ovs_expr statement_represent(const ovs_function_data* d) {
	return ovs_symbol(d->context->root_tables + OVS_SYSTEM_BUILTIN, u_strlen(d->type->name), d->type->name);
}

void statement_free(const void* d) {
	statement_data* data = (statement_data*)d;

	ovs_free(OVS_FUNCTION, data->parent);
}

ovs_function_info statement_inspect(const void* d) {
	switch (((statement_data*)d)->type) {
		case STATEMENT_WITH_LAMBDA:
			return (ovs_function_info){ 3, 2 };
		case STATEMENT_WITH_VARIABLE:
			return (ovs_function_info){ 2, 2 };
		case STATEMENT_WITH_QUOTE:
			return (ovs_function_info){ 2, 2 };
		case STATEMENT_END:
			return (ovs_function_info){ 1, 2 };
	}
	assert(false);
}

int32_t statement_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* d);

static ovs_function_type statement_function = {
	u"statement",
	statement_represent,
	statement_inspect,
	statement_apply,
	statement_free
};

/*
 * Parameters Function
 */

typedef enum parameters_function_type {
	PARAMETERS_WITH,
	PARAMETERS_END
} parameters_function;

typedef struct parameters_data {
	parameters_function_type type;
	ovs_expr params;
	ovs_expr_ref* statement_cont;
	ovs_expr_ref* parent;
} parameters_data;

ovs_expr parameters_represent(const ovs_function_data* d) {
	return ovs_symbol(d->context->root_tables + OVS_SYSTEM_BUILTIN, u_strlen(d->type->name), d->type->name);
}

void parameters_free(const void* d) {
	parameters_data* data = (parameters_data*)d;

	ovs_free(OVS_FUNCTION, data->parent);
}

ovs_function_info parameters_inspect(const void* d) {
	switch (((parameters_data*)d)->type) {
		case PARAMETERS_WITH:
			return (ovs_function_info){ 2, 3 };
		case PARAMETERS_END:
			return (ovs_function_info){ 1, 3 };
	}
	assert(false);
}

int32_t parameters_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* d);

static ovs_function_type end_parameters_function = {
	u"parameters",
	parameters_represent,
	parameters_inspect,
	parameters_apply,
	parameters_free
};

void* ref_of(ovs_expr e) {
	return (void*)e.p;
}


/*
 * Compile
 */

ovs_expr compile_represent(const ovs_function_data* d) {
	return ovs_symbol(d->context->root_tables + OVS_SYSTEM_BUILTIN, u_strlen(d->type->name), d->type->name);
}

ovs_function_info compile_inspect(const void* d) {
	return (ovs_function_info){ 3, 3 };
}

void compile_free(const void* d) {}

int32_t compile_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* d);

static ovs_function_type compile_function = {
	u"compile",
	compile_represent,
	compile_inspect,
	compile_apply,
	compile_free
};

/*
 * Impl
 */

int32_t statement_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* d) {
	statement_data* data = (statement_data*)(d + 1);

	if (data->type == STATEMENT_END) {
		// TODO
		return 91546;
	}

	ovru_term t;
	ovru_result r;
	switch (data->type) {
		case STATEMENT_WITH_LAMBDA:
			break;

		case STATEMENT_WITH_VARIABLE:
			ovs_expr variable = args[0];
			ovs_expr cont = args[2];

			t.type = OVRU_VARIABLE;
			r = find_variable(&t.variable, data->context, variable.p);
			break;

		case STATEMENT_WITH_QUOTE:
			ovs_expr data = args[0];
			ovs_expr cont = args[1];

			ovru_term t = { .quote=data };
			r = OVRU_SUCCESS;
			break;

		default:
			assert(false);
	}

	if (r == OVRU_SUCCESS) {
		statement_data s;
		statement_with_term(&s, data, t);

		i->size = 3;
		i->values[0] = ovs_alias(cont);
		i->values[1] = ovs_function(d->context, &statement_function, sizeof(statement_data), &s);
		i->values[2] = ovs_function(d->context, &statement_function, sizeof(statement_data), &s);
		i->values[3] = ovs_function(d->context, &statement_function, sizeof(statement_data), &s);
		i->values[4] = ovs_function(d->context, &statement_function, sizeof(statement_data), &s);

	} else {
		// TODO fail
	}

	return r;
}
int32_t parameters_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* d) {
	parameters_data* data = ovs_function_extra_data(d);

	switch (data->type) {
		case PARAMETERS_WITH:
			ovs_expr param = args[0];
			ovs_expr cont = args[1];

			parameters_data p;
			p.params = ovs_cons(&d->context->root_tables[OVS_UNQUALIFIED], param, data->params);
			p.parent = data->parent;

			i->size = 3;
			i->values[0] = ovs_alias(cont);
			i->values[1] = ovs_function(d->context, &with_parameter_function, sizeof(parameters_data), &p);
			i->values[2] = ovs_function(d->context, &end_parameters_function, sizeof(parameters_data), &p);

			break;

		case PARAMETERS_END:
			ovs_expr cont = data->statement_cont;

			statement_data s;
			s.context = malloc(sizeof(compile_context));
			s.statement.term_count = 0;
			s.statement.terms = NULL;
			s.parent = data->parent;

			const ovs_expr_ref** param_list;
			int32_t param_count = ovs_delist_of(&d->context->root_tables[OVS_UNQUALIFIED], data->params, (void***)&param_list, ref_of);
			compile_parameters(s.context, d->context, param_count, param_list);

			i->size = 3;
			i->values[0] = ovs_alias(cont);
			i->values[1] = ovs_function(d->context, &with_lambda_function, sizeof(statement_data), &s);
			i->values[2] = ovs_function(d->context, &with_variable_function, sizeof(statement_data), &s);

			break;

		default:
			assert(false);
	}
	return OVRU_SUCCESS;
}

int32_t compile_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* d) {
	ovs_expr params = args[0];
	ovs_expr build = args[1];
	ovs_expr cont = args[2];

	i->size = 3;
	i->values[0] = ovs_alias(params);
	i->values[1] = ovs_function(d->context, &with_parameter_function, sizeof(parameters_data), &w);
	i->values[2] = ovs_function(d->context, &end_parameters_function, sizeof(parameters_data), &w);

	parameters_data* w1 = ovs_function_extra_data(i->values[1].p->function_data);
	w1->params = ovs_root_symbol(OVS_DATA_NIL)->expr;
	w1->statement_cont = build.p;
	w1->parent = cont.p;

	parameters_data* w2 = ovs_function_extra_data(i->values[2].p->function_data);
	w2->params = ovs_root_symbol(OVS_DATA_NIL)->expr;
	w2->statement_cont = build.p;
	w2->parent = cont.p;

	return OVRU_SUCCESS;
}

ovs_expr ovru_compiler(ovs_context* c) {
	return ovs_function(c, &compile_function, 0, NULL);
}

