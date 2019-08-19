/**
 * Copyright (C) 2019 kuroneko
 */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static char *ngx_http_homework_set_key_value(
    ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_homework_get_variable_handler(
    ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_homework_pre_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_homework_post_init(ngx_conf_t *cf);
static void *ngx_http_homework_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_homework_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_homework_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_homework_merge_loc_conf(
    ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_homework_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_homework_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_homework_body_filter(
    ngx_http_request_t *r, ngx_chain_t *in);

static const char kVariableName[] = "homework_host";
static const char kTapHeader[] = "tap";
static const char kSignaturePrefix[] = "\n<!-- Origin server is changed from ";
static const char kSignatureJointer[] = " to ";
static const char kSignatureSuffix[] = " by homework module -->\n";

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

typedef struct ngx_http_homework_key_value_s {
  ngx_str_t key;
  ngx_str_t value;
} ngx_http_homework_key_value_t;

typedef struct ngx_http_homework_main_conf_s {
  ngx_int_t variable_index;
} ngx_http_homework_main_conf_t;

typedef struct ngx_http_homework_loc_conf_s {
  ngx_http_homework_key_value_t target;
} ngx_http_homework_loc_conf_t;

typedef struct ngx_http_homework_ctx_s {
  ngx_flag_t is_changed;
  ngx_flag_t has_signature;
  ngx_str_t from_host;
  ngx_str_t to_host;
  ngx_uint_t signature_length;
} ngx_http_homework_ctx_t;

static ngx_command_t ngx_http_homework_commands[] = {
  {ngx_string("homework_target"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
    ngx_http_homework_set_key_value,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_homework_loc_conf_t, target),
    NULL},
  ngx_null_command};

/* The module context. */
static ngx_http_module_t ngx_http_homework_module_ctx = {
    ngx_http_homework_pre_init, /* preconfiguration */
    ngx_http_homework_post_init, /* postconfiguration */

    ngx_http_homework_create_main_conf, /* create main configuration */
    ngx_http_homework_init_main_conf, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    ngx_http_homework_create_loc_conf, /* create location configuration */
    ngx_http_homework_merge_loc_conf /* merge location configuration */
};

/* Module definition. */
ngx_module_t ngx_http_homework_module = {
    NGX_MODULE_V1,
    &ngx_http_homework_module_ctx, /* module context */
    ngx_http_homework_commands, /* module directives */
    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    NULL, /* init module */
    NULL, /* init process */
    NULL, /* init thread */
    NULL, /* exit thread */
    NULL, /* exit process */
    NULL, /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_http_homework_handler(ngx_http_request_t *r)
{
  ngx_http_homework_loc_conf_t *hlcf
    = (ngx_http_homework_loc_conf_t *) ngx_http_get_module_loc_conf(
        r, ngx_http_homework_module);
  if (!hlcf->target.key.data || !hlcf->target.value.data) {
    return NGX_DECLINED;
  }

  ngx_list_part_t *part = &r->headers_in.headers.part;
  ngx_str_t target_name = ngx_string(kTapHeader);
  ngx_table_elt_t *target_header = NULL;
  for (; part; part = part->next) {
    ngx_table_elt_t *headers = (ngx_table_elt_t *) part->elts;
    for (ngx_uint_t i = 0; i < part->nelts; ++i) {
      if (target_name.len == headers[i].key.len
          && !ngx_strcasecmp(target_name.data, headers[i].key.data)) {
        target_header = &headers[i];
        goto END_OF_FIND_HEADER;
      }
    }
  }
END_OF_FIND_HEADER:
  if (target_header == NULL) {
    return NGX_DECLINED;
  }
  if (hlcf->target.key.len != target_header->value.len
      || ngx_strcmp(hlcf->target.key.data, target_header->value.data)) {
    return NGX_DECLINED;
  }
  ngx_http_homework_main_conf_t *hmcf
    = (ngx_http_homework_main_conf_t *) ngx_http_get_module_main_conf(
        r, ngx_http_homework_module);
  ngx_http_variable_value_t *value
    = ngx_http_get_indexed_variable(r, hmcf->variable_index);

  ngx_http_homework_ctx_t *ctx
    = (ngx_http_homework_ctx_t *)ngx_http_get_module_ctx(
        r, ngx_http_homework_module);
  if (ctx == NULL) {
    ctx = (ngx_http_homework_ctx_t *)ngx_pcalloc(
        r->pool, sizeof(ngx_http_homework_ctx_t));
    ngx_http_set_ctx(r, ctx, ngx_http_homework_module);
  }
  ctx->is_changed = true;
  ctx->has_signature = false;
  ctx->from_host = (ngx_str_t){value->len, value->data};
  ctx->to_host = hlcf->target.value;

  value->len = hlcf->target.value.len;
  value->valid = 1;
  value->no_cacheable = 0;
  value->not_found = 0;
  value->data = hlcf->target.value.data;

  return NGX_DECLINED;
}

static char *ngx_http_homework_set_key_value(
    ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

  char *pointer = conf;
  ngx_http_homework_key_value_t *field
    = (ngx_http_homework_key_value_t *)(pointer + cmd->offset);

  if (field->key.data || field->value.data) {
    return NGX_CONF_ERROR;
  }
  ngx_str_t *values = cf->args->elts;
  field->key = values[1];
  field->value = values[2];
  return NGX_CONF_OK;
}

static ngx_int_t ngx_http_homework_get_variable_handler(
    ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data) {
  if (data == 0) {
    v->not_found = 1;
  }
  return NGX_OK;
}

static ngx_int_t ngx_http_homework_pre_init(ngx_conf_t *cf) {
  ngx_str_t name = ngx_string(kVariableName);
  ngx_http_variable_t *variable
    = ngx_http_add_variable(cf, &name, NGX_HTTP_VAR_CHANGEABLE);
  if (variable == NULL) {
    return NGX_ERROR;
  }
  variable->data = 0;
  variable->get_handler = ngx_http_homework_get_variable_handler;
  return NGX_OK;
}

static ngx_int_t ngx_http_homework_post_init(ngx_conf_t *cf) {
  ngx_http_core_main_conf_t *cmcf
    = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

  ngx_http_handler_pt *handler
    = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
  if (handler == NULL) {
      return NGX_ERROR;
  }
  *handler = ngx_http_homework_handler;

  ngx_http_next_header_filter = ngx_http_top_header_filter;
  ngx_http_top_header_filter = ngx_http_homework_header_filter;
  ngx_http_next_body_filter = ngx_http_top_body_filter;
  ngx_http_top_body_filter = ngx_http_homework_body_filter;
  return NGX_OK;
}

static void *ngx_http_homework_create_main_conf(ngx_conf_t *cf) {
  ngx_http_homework_main_conf_t *conf
    = (ngx_http_homework_main_conf_t *) ngx_pcalloc(
        cf->pool, sizeof(ngx_http_homework_main_conf_t));

  return conf;
}

static char *ngx_http_homework_init_main_conf(ngx_conf_t *cf, void *conf) {
  ngx_http_homework_main_conf_t *hmcf = (ngx_http_homework_main_conf_t *) conf;
  
  ngx_str_t name = ngx_string(kVariableName);
  hmcf->variable_index = ngx_http_get_variable_index(cf, &name);
  if (hmcf->variable_index == NGX_ERROR) {
    return NGX_CONF_ERROR;
  }

  return NGX_CONF_OK;
}

static void *ngx_http_homework_create_loc_conf(ngx_conf_t *cf) {
  ngx_http_homework_loc_conf_t *conf
    = (ngx_http_homework_loc_conf_t *) ngx_pcalloc(
        cf->pool, sizeof(ngx_http_homework_loc_conf_t));

  conf->target
    = (ngx_http_homework_key_value_t){ngx_null_string, ngx_null_string};
  return conf;
}

static char *ngx_http_homework_merge_loc_conf(
    ngx_conf_t *cf, void *parent, void *child) {
  ngx_http_homework_loc_conf_t *conf
    = (ngx_http_homework_loc_conf_t *) child;

  if (!conf->target.key.data || !conf->target.value.data) {
    conf->target = ((ngx_http_homework_loc_conf_t *) parent)->target;
  }

  return NGX_CONF_OK;
}

static ngx_int_t ngx_http_homework_header_filter(ngx_http_request_t *r) {
  ngx_http_homework_ctx_t *ctx
    = (ngx_http_homework_ctx_t *)ngx_http_get_module_ctx(
        r, ngx_http_homework_module);
  if (ctx == NULL || !ctx->is_changed) {
    return ngx_http_next_header_filter(r);
  }
  if (ctx->signature_length == 0) {
    ctx->signature_length = sizeof(kSignaturePrefix)
      + ctx->from_host.len + sizeof(kSignatureJointer)
      +ctx->to_host.len + sizeof(kSignatureSuffix) - 3;
  }
  if (r->headers_out.content_length_n != -1) {
    r->headers_out.content_length_n += ctx->signature_length;
  }
  return ngx_http_next_header_filter(r);
}

static ngx_int_t ngx_http_homework_body_filter(
    ngx_http_request_t *r, ngx_chain_t *in) {
  ngx_http_homework_ctx_t *ctx
    = (ngx_http_homework_ctx_t *)ngx_http_get_module_ctx(
        r, ngx_http_homework_module);

  if (ctx == NULL || !ctx->is_changed || ctx->has_signature) {
    return ngx_http_next_body_filter(r, in);
  }

  ngx_chain_t *chain = in;
  for (; chain->next; chain = chain->next) {}
  if (!chain->buf->last_buf) {
    return ngx_http_next_body_filter(r, in);
  }

  u_char *pos = ngx_palloc(r->pool, ctx->signature_length + 1);
  u_char *last = pos;
  last = ngx_copy(last, kSignaturePrefix, sizeof(kSignaturePrefix) - 1);
  last = ngx_copy(last, ctx->from_host.data, ctx->from_host.len);
  last = ngx_copy(last, kSignatureJointer, sizeof(kSignatureJointer) - 1);
  last = ngx_copy(last, ctx->to_host.data, ctx->to_host.len);
  last = ngx_copy(last, kSignatureSuffix, sizeof(kSignatureSuffix) - 1);
  *last = '\0';

  ngx_buf_t *buffer = (ngx_buf_t *)ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
  buffer->memory = 1;
  buffer->last_buf = 0;
  buffer->pos = pos;
  buffer->last = last;

  ngx_chain_t last_chain;
  last_chain.next = NULL;
  last_chain.buf = buffer;

  ngx_int_t result = ngx_http_next_body_filter(r, &last_chain);
  if (result != NGX_OK) {
    return result;
  }
  ctx->has_signature = true;

  return ngx_http_next_body_filter(r, in);
}
