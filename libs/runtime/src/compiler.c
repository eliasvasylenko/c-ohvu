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
#include "compilerapi.h"

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
 * API
 */

void* ref_of(ovs_expr e) {
	return (void*)e.p;
}

ovs_expr statement_function(statement_data* d, ovs_function_type* t) {
	statement_data** s;
	ovs_expr e = ovs_function(d->context->ovs_context, t, sizeof(statement_data*), (void**)&s);
	*s = d;
	return e;
}

void statement_with(ovs_instruction* i, statement_data* e, ovs_expr cont, ovru_term t) {
	statement_data* d = malloc(sizeof(statement_data));

	d->context = e->context;
	d->statement.term_count = e->statement.term_count + 1;
	d->statement.terms = malloc(sizeof(ovru_term) * d->statement.term_count);
	d->parent = e->parent;
	for (int i = 0; i < e->statement.term_count; i++) {
		d->statement.terms[i] = e->statement.terms[i];
	}
	d->statement.terms[e->statement.term_count] = t;

	i->size = 5;
	i->values[0] = ovs_alias(cont);
	i->values[1] = statement_function(d, &statement_with_lambda_function);
	i->values[2] = statement_function(d, &statement_with_variable_function);
	i->values[3] = statement_function(d, &statement_with_quote_function);
	i->values[4] = statement_function(d, &statement_end_function);
}

int32_t statement_end_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* f) {
	statement_data* data = ovs_function_extra_data(f);

	// TODO
	return 91546;
}

int32_t statement_with_lambda_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* f) {
	statement_data* data = ovs_function_extra_data(f);

	ovs_expr params = args[0];
	ovs_expr body = args[1];
	ovs_expr cont = args[2];

	i->size = 3;
	i->values[0] = ovs_alias(params);
	i->values[1] = parameters_function(d, f->context, &parameters_with_function);
	i->values[2] = parameters_function(d, f->context, &parameters_end_function);

	// adfter params ->

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
	// TODO
}

int32_t statement_with_variable_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* f) {
	statement_data* e = ovs_function_extra_data(f);

	ovs_expr variable = args[0];
	ovs_expr cont = args[1];

	ovru_term t;
	t.type = OVRU_VARIABLE;
	ovru_result r = find_variable(&t.variable, e->context, variable.p);

	if (r == OVRU_SUCCESS) {
		statement_with(i, e, cont, t);
	}
	return r;
}

int32_t statement_with_quote_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* f) {
	statement_data* e = ovs_function_extra_data(f);

	ovs_expr data = args[0];
	ovs_expr cont = args[1];

	ovru_term t = { .quote=data };

	statement_with(i, e, cont, t);
	return OVRU_SUCCESS;
}

ovs_expr parameters_function(parameters_data* d, ovs_context* c, ovs_function_type* t) {
	parameters_data** p;
	ovs_expr e = ovs_function(c, t, sizeof(parameters_data*), (void**)&p);
	*p = d;
	return e;
}

int32_t parameters_with_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* f) {
	parameters_data* e = ovs_function_extra_data(f);

	ovs_expr param = args[0];
	ovs_expr cont = args[1];

	parameters_data* d = malloc(sizeof(parameters_data));
	d->params = ovs_cons(&f->context->root_tables[OVS_UNQUALIFIED], param, e->params);
	d->parent = e->parent;

	i->size = 3;
	i->values[0] = ovs_alias(cont);
	i->values[1] = parameters_function(d, f->context, &parameters_with_function);
	i->values[2] = parameters_function(d, f->context, &parameters_end_function);

	return OVRU_SUCCESS;
}

int32_t parameters_end_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* f) {
	parameters_data* e = ovs_function_extra_data(f);

	ovs_expr cont = (ovs_expr){ OVS_FUNCTION, .p=e->statement_cont };

	statement_data* d = malloc(sizeof(statement_data));
	d->context = malloc(sizeof(compile_context));
	d->statement.term_count = 0;
	d->statement.terms = NULL;
	d->cont = e->cont;

	const ovs_expr_ref** param_list;
	int32_t param_count = ovs_delist_of(&f->context->root_tables[OVS_UNQUALIFIED], e->params, (void***)&param_list, ref_of);
	compile_parameters(d->context, f->context, param_count, param_list);

	i->size = 3;
	i->values[0] = ovs_alias(cont);
	i->values[1] = statement_function(d, &statement_with_lambda_function);
	i->values[2] = statement_function(d, &statement_with_variable_function);

	return OVRU_SUCCESS;
}

int32_t compile_apply(ovs_instruction* i, ovs_expr* args, const ovs_function_data* f) {
	ovs_expr params = args[0];
	ovs_expr body = args[1];
	ovs_expr cont = args[2];

	parameters_data* d = malloc(sizeof(parameters_data));
	d->params = ovs_root_symbol(OVS_DATA_NIL)->expr;
	d->statement_cont = body.p;
	d->cont = cont.p;

	i->size = 3;
	i->values[0] = ovs_alias(params);
	i->values[1] = parameters_function(d, f->context, &parameters_with_function);
	i->values[2] = parameters_function(d, f->context, &parameters_end_function);

	return OVRU_SUCCESS;
}

ovs_expr ovru_compiler(ovs_context* c) {
	return ovs_function(c, &compile_function, 0, NULL);
}
