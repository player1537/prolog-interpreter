#include "prolog.h"
#include "mpc/mpc.h"
#include <assert.h>

#define NEW(type, num) ((type*)malloc(sizeof(type) * (num)))
#define RENEW(orig, type, num) ((type*)realloc((orig), sizeof(type) * (num)))
#define MAX_PARAMS 10
#define ENLARGE_FACTOR 2

/*****************************************
 * AST Functions
 *****************************************/
int parse_file(mpc_result_t *r, const char *filename) {
  mpc_parser_t	*Constant  = mpc_new("constant");
  mpc_parser_t	*Variable  = mpc_new("variable");
  mpc_parser_t	*Ident	   = mpc_new("ident");
  mpc_parser_t	*Params	   = mpc_new("params");
  mpc_parser_t	*Predicate = mpc_new("predicate");
  mpc_parser_t  *Union     = mpc_new("union");
  mpc_parser_t	*Fact	   = mpc_new("fact");
  mpc_parser_t  *Query     = mpc_new("query");
  mpc_parser_t	*Lang	   = mpc_new("lang");

  int return_value;

  mpca_lang(MPCA_LANG_DEFAULT,
	    " constant  : /[a-z0-9_]+/;                            "
	    " variable  : /[A-Z][a-z0-9_]*/;                       "
	    " ident     : <constant> | <variable>;                 "
	    " params    : <ident> (',' <ident>)*;                  "
	    " predicate : <ident> '(' <params> ')';                "
	    " union     : <predicate> (',' <predicate>)*;          "
	    " fact      : <union> '.';                             "
	    " query     : \"?-\" <union> '.';                      "
	    " lang      : /^/ (<fact> | <query>)+ /$/;             ",
	    Constant, Variable, Ident, Params, Predicate, Union, Fact, Query,
	    Lang, NULL);

  printf("Constant:  "); mpc_print(Constant);
  printf("Variable:  "); mpc_print(Variable);
  printf("Ident:     "); mpc_print(Ident);
  printf("Params:    "); mpc_print(Params);
  printf("Predicate: "); mpc_print(Predicate);
  printf("Union:     "); mpc_print(Union);
  printf("Fact:      "); mpc_print(Fact);
  printf("Query:     "); mpc_print(Query);
  printf("Lang:      "); mpc_print(Lang);

  if (filename != NULL) {
    return_value = mpc_parse_contents(filename, Lang, r);
  } else {
    return_value = mpc_parse_pipe("<stdin>", stdin, Lang, r);
  }

  if (!return_value) {
    mpc_err_print(r->error);
    mpc_err_delete(r->error);

    goto cleanup;
  }

 cleanup:
  mpc_cleanup(9,
	      Constant, Variable, Ident, Params, Predicate,
	      Union,    Fact,     Query, Lang
	      );

  return return_value;
}

void print_tags(const mpc_ast_t *ast, const int depth) {
  int i;

  for (i=0; i<depth; ++i) {
    printf("  ");
  }
  printf("%s: '%s'\n", ast->tag, ast->contents);

  for (i=0; i<ast->children_num; ++i) {
    print_tags(ast->children[i], depth + 1);
  }
}

void initialize_tag_state(find_tag_state_t *state, const mpc_ast_t *ast) {
  state->ast = ast;
  state->child = 0;
}

const mpc_ast_t *find_tag(const mpc_ast_t *ast, const char *tag) {
  find_tag_state_t state;

  initialize_tag_state(&state, ast);
  return find_tag_next(&state, tag);
}

const mpc_ast_t *find_tag_next(find_tag_state_t *state, const char *tag) {
  int i,
      j,
      children_num;
  const mpc_ast_t *child_ast,
                  *parent = state->ast,
                  *return_value;

  if (parent == NULL) {
    return NULL;
  }

  children_num = parent->children_num;

  for (i=state->child; i<children_num; ++i) {
    child_ast = parent->children[i];
    if (strstr(child_ast->tag, tag) != NULL) {
      state->child = i + 1;
      return child_ast;
    }

    for (j=0; j<child_ast->children_num; ++j) {
      state->ast = child_ast;
      state->child = 0;

      return_value = find_tag_next(state, tag);
      if (return_value != NULL) {
	return return_value;
      }
    }

    state->ast = parent;
  }

  state->ast = NULL;
  return NULL;
}

/*****************************************
 * Symbol Table Functions
 *****************************************/
void initialize_symbol_table(symbol_table_t *table) {
  table->num_symbols = 0;
  table->num_allocated = 1;
  table->symbols = NEW(symbol_table_node_t *, table->num_allocated);
}

void initialize_symbol_table_node(symbol_table_node_t *node, const char *name) {
  node->num_link = 0;
  node->num_allocated = 1;
  node->links = NEW(symbol_table_to_predicate_t *, node->num_allocated);
  node->name = name;
}

void initialize_symbol_table_to_predicate(symbol_table_to_predicate_t *link,
					  int pos,
					  predicate_table_node_t *predicate,
					  predicate_table_to_symbol_t *ref) {
  link->position = pos;
  link->predicate = predicate;
  link->link = ref;
}

symbol_table_node_t *symbol_table_add(symbol_table_t *table, const char *name) {
  symbol_table_node_t *node = NEW(symbol_table_node_t, 1);

  initialize_symbol_table_node(node, name);

  if (table->num_symbols >= table->num_allocated) {
    symbol_table_enlarge(table);
  }

  table->symbols[table->num_symbols++] = node;
  return node;
}

void symbol_table_link_add(symbol_table_node_t *node,
			   int pos,
			   predicate_table_node_t *predicate,
			   predicate_table_to_symbol_t *ref) {
  symbol_table_to_predicate_t *link = NEW(symbol_table_to_predicate_t, 1);

  initialize_symbol_table_to_predicate(link, pos, predicate, ref);

  if (node->num_link >= node->num_allocated) {
    symbol_table_node_enlarge(node);
  }

  node->links[node->num_link++] = link;
}

void symbol_table_enlarge(symbol_table_t *table) {
  table->num_allocated *= ENLARGE_FACTOR;
  table->symbols = RENEW(table->symbols,
			 symbol_table_node_t *,
			 table->num_allocated);
}

void symbol_table_node_enlarge(symbol_table_node_t *node) {
  node->num_allocated *= ENLARGE_FACTOR;
  node->links = RENEW(node->links,
		      symbol_table_to_predicate_t *,
		      node->num_allocated);
}

symbol_table_node_t *symbol_table_find(symbol_table_t *table,
				       const char *name) {
  int i;
  symbol_table_node_t *node;

  for (i=0; i<table->num_symbols; ++i) {
    node = table->symbols[i];
    if (strcmp(name, node->name) == 0) {
      return node;
    }
  }

  return NULL;
}

/*****************************************
 * Predicate Table Functions
 *****************************************/
void initialize_predicate_table(predicate_table_t *table) {
  table->num_predicates = 0;
  table->num_allocated = 1;
  table->predicates = NEW(predicate_table_node_t *, table->num_allocated);
}

void initialize_predicate_table_node(predicate_table_node_t *node,
				     const char *name) {
  node->num_link = 0;
  node->num_allocated = 1;
  node->name = name;
  node->links = NEW(predicate_table_to_symbol_t *, node->num_allocated);
}

void initialize_predicate_table_to_symbol(predicate_table_to_symbol_t *link,
					  int arity) {
  link->arity = arity;
  link->nodes = NEW(symbol_table_node_t *, arity);
}

predicate_table_node_t *predicate_table_add(predicate_table_t *table,
					    const char *name) {
  predicate_table_node_t *node = NEW(predicate_table_node_t, 1);

  initialize_predicate_table_node(node, name);

  if (table->num_predicates >= table->num_allocated) {
    predicate_table_enlarge(table);
  }

  table->predicates[table->num_predicates++] = node;
  return node;
}

predicate_table_to_symbol_t *predicate_table_link_add(
    predicate_table_node_t *node,
    int arity,
    symbol_table_node_t **nodes) {
  predicate_table_to_symbol_t *link = NEW(predicate_table_to_symbol_t, 1);
  int i;

  link->arity = arity;
  link->nodes = NEW(symbol_table_node_t *, arity);

  for (i=0; i<arity; ++i) {
    link->nodes[i] = nodes[i];
  }

  if (node->num_link >= node->num_allocated) {
    predicate_table_node_enlarge(node);
  }

  node->links[node->num_link++] = link;
  return link;
}

void predicate_table_enlarge(predicate_table_t *table) {
  table->num_allocated *= ENLARGE_FACTOR;
  table->predicates = RENEW(table->predicates,
			    predicate_table_node_t *,
			    table->num_allocated);
}

void predicate_table_node_enlarge(predicate_table_node_t *node) {
  node->num_allocated *= ENLARGE_FACTOR;
  node->links = RENEW(node->links,
		      predicate_table_to_symbol_t *,
		      node->num_allocated);
}

predicate_table_node_t *predicate_table_find(predicate_table_t *table,
					     const char *name) {
  int i;
  predicate_table_node_t *node;

  for (i=0; i<table->num_predicates; ++i) {
    node = table->predicates[i];
    if (strcmp(name, node->name) == 0) {
      return node;
    }
  }

  return NULL;
}

/*****************************************
 * Rule Functions
 *****************************************/
void define_facts(const mpc_ast_t *ast,
		  symbol_table_t *symbol_table,
		  predicate_table_t *predicate_table) {
  int ident_number,
      i;
  const char *params[MAX_PARAMS];
  find_tag_state_t fact_state,
                   predicate_state,
                   ident_state;
  const mpc_ast_t *fact,
                  *predicate,
                  *ident;

  initialize_tag_state(&fact_state, ast);
  while ((fact = find_tag_next(&fact_state, "fact")) != NULL) {
    initialize_tag_state(&predicate_state, fact);
    while ((predicate = find_tag_next(&predicate_state, "predicate")) != NULL) {
      ident_number = 0;

      for (i=0; i<MAX_PARAMS; ++i) {
	params[i] = NULL;
      }

      initialize_tag_state(&ident_state, predicate);
      while ((ident = find_tag_next(&ident_state, "ident")) != NULL) {
	params[ident_number++] = ident->contents;
      }

      rule_add(symbol_table,
	       predicate_table,
	       params[0],
	       ident_number - 1,
	       &params[1]);
    }
  }
}

void rule_add(symbol_table_t *symbol_table,
	      predicate_table_t *predicate_table,
	      const char *pred_name,
	      const int arity,
	      const char **strings) {
  int i;
  predicate_table_node_t *predicate;
  predicate_table_to_symbol_t *link;
  symbol_table_node_t *symbols[MAX_PARAMS];
  symbol_table_node_t *symbol;

  predicate = predicate_table_find(predicate_table, pred_name);
  if (!predicate) {
    predicate = predicate_table_add(predicate_table, pred_name);
  }

  for (i=0; i<arity; ++i) {
    symbol = symbol_table_find(symbol_table, strings[i]);
    if (!symbol) {
      symbol = symbol_table_add(symbol_table, strings[i]);
    }

    symbols[i] = symbol;
  }

  link = predicate_table_link_add(predicate, arity, symbols);

  for (i=0; i<arity; ++i) {
    symbol_table_link_add(symbols[i], i, predicate, link);
  }
}

void print_symbols(symbol_table_t *table) {
  int i,
      j;
  symbol_table_node_t *node;
  symbol_table_to_predicate_t *link;
  predicate_table_node_t *predicate;

  printf("Symbol Table:\n");
  for (i=0; i<table->num_symbols; ++i) {
    node = table->symbols[i];
    printf("'%s':\n", node->name);
    for (j=0; j<node->num_link; ++j) {
      link = node->links[j];
      predicate = link->predicate;
      printf("\t'%s': %d\n", predicate->name, link->position);
    }
  }
}

void print_predicates(predicate_table_t *table) {
  int i,
      j,
      k;
  predicate_table_node_t *node;
  predicate_table_to_symbol_t *link;
  symbol_table_node_t *symbol;

  printf("Predicate Table:\n");
  for (i=0; i<table->num_predicates; ++i) {
    node = table->predicates[i];
    for (j=0; j<node->num_link; ++j) {
      link = node->links[j];
      printf("%s(", node->name);
      for (k=0; k<link->arity; ++k) {
	symbol = link->nodes[k];
	if (k != 0) {
	  printf(",");
	}
	printf("%s", symbol->name);
      }
      printf(").\n");
    }
  }
}

void print_rules(symbol_table_t *symbol_table,
		 predicate_table_t *predicate_table) {
  print_symbols(symbol_table);
  print_predicates(predicate_table);
}

/*****************************************
 * Main function
 *****************************************/
int main(int argc, char **argv) {
  mpc_result_t r;
  symbol_table_t symbol_table;
  predicate_table_t predicate_table;
  int return_value;
  const char *filename = argc > 1 ? argv[1] : NULL;

  return_value = parse_file(&r, filename);
  if (!return_value) {
    return 1;
  }

  initialize_symbol_table(&symbol_table);
  initialize_predicate_table(&predicate_table);

  print_tags(r.output, 0);
  define_facts(r.output, &symbol_table, &predicate_table);

  print_rules(&symbol_table, &predicate_table);

  mpc_ast_delete(r.output);

  return 0;
}
