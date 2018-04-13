/*
 * dialplan_handler.c
 *
 *  Created on: Jan 21, 2018
 *      Author: pchero
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <jansson.h>
#include <string.h>

#include "slog.h"
#include "utils.h"
#include "conf_handler.h"
#include "http_handler.h"
#include "resource_handler.h"
#include "publication_handler.h"
#include "ami_action_handler.h"


#define DEF_DIALPLAN_CONFNAME   "extensions.conf"

#define DEF_DB_TABLE_DP_DIALPLANMASTER    "dp_dpma"
#define DEF_DB_TABLE_DP_DIALPLAN          "dp_dialplan"

static bool init_databases(void);
static bool init_database_dp_dialplan(void);
static bool init_database_dp_dialplanmaster(void);

static bool create_dpma_info(const json_t* j_data);
static bool delete_dpma_info(const char* uuid);
static bool update_dpma_info(const char* uuid, const json_t* j_data);
static bool is_exist_dpma_info(const char* uuid);


static bool create_dialplan_info(const json_t* j_data);
static bool update_dialplan_info(const char* uuid, const json_t* j_data);
static bool delete_dialplan_info(const char* uuid);
static bool is_exist_dialplan_info(const char* dpma_uuid, int seq);
static bool is_exist_dialplan_info_uuid(const char* uuid);

bool dialplan_init_handler(void)
{
  int ret;

  ret = init_databases();
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database.");
    return false;
  }

  return true;
}

bool dialplan_term_handler(void)
{
  return true;
}

bool dialplan_reload_handler(void)
{
  int ret;

  ret = dialplan_term_handler();
  if(ret == false) {
    return false;
  }

  ret = dialplan_init_handler();
  if(ret == false) {
    return false;
  }

  return true;
}

static bool init_databases(void)
{
  int ret;

  slog(LOG_DEBUG, "Fired init_databases.");

  ret = init_database_dp_dialplanmaster();
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate dialplanmaster database.");
    return false;
  }

  ret = init_database_dp_dialplan();
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate dialplan database.");
    return false;
  }

  return true;
}

static bool init_database_dp_dialplanmaster(void)
{
  int ret;
  const char* create_table;

  create_table =
      "create table if not exists " DEF_DB_TABLE_DP_DIALPLANMASTER " ("

      // identify
      "   uuid    varchar(255),"

      // info
      "   name        varchar(255),"
      "   detail      varchar(1023),"

      // timestamp. UTC."
      "   tm_create         datetime(6),"   // create time
      "   tm_update         datetime(6),"   // update time.

      "   primary key(uuid)"
      ");";

  ret = resource_exec_mem_sql(create_table);
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database. database[%s]", DEF_DB_TABLE_DP_DIALPLANMASTER);
    return false;
  }

  return true;
}

static bool init_database_dp_dialplan(void)
{
  int ret;
  const char* create_table;

  create_table =
      "create table if not exists " DEF_DB_TABLE_DP_DIALPLAN " ("

      // identify
      "   uuid        varchar(255),"  // identity.
      "   dpma_uuid   varchar(255),"
      "   sequence    int,"

      // info
      "   name        varchar(255),"
      "   detail      varchar(1023),"
      "   command     varchar(1023),"

      // timestamp. UTC."
      "   tm_create         datetime(6),"   // create time
      "   tm_update         datetime(6),"   // update time.

      "   primary key(dpma_uuid, sequence)"   ///
      ");";


  ret = resource_exec_mem_sql(create_table);
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database. database[%s]", DEF_DB_TABLE_DP_DIALPLAN);
    return false;
  }

  return true;
}

/**
 * GET ^/dp/config request handler.
 * @param req
 * @param data
 */
void dialplan_htp_get_dp_config(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  char* tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_get_dp_config.");

  // get info
  tmp = conf_get_ast_current_config_info_text(DEF_DIALPLAN_CONFNAME);
  if(tmp == NULL) {
    slog(LOG_ERR, "Could not get dialplan conf.");
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", json_string(tmp));
  sfree(tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * PUT ^/dp/config request handler.
 * @param req
 * @param data
 */
void dialplan_htp_put_dp_config(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  char* tmp;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_put_dp_config.");

  tmp = http_get_text_from_request_data(req);
  if(tmp == NULL) {
    slog(LOG_ERR, "Could not get data.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // update config
  ret = conf_update_ast_current_config_info_text(DEF_DIALPLAN_CONFNAME, tmp);
  sfree(tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not update dialplan config info.");
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * GET ^/dp/dpmas request handler.
 * @param req
 * @param data
 */
void dialplan_htp_get_dp_dpmas(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_INFO, "Fired htp_get_dp_dpmas.");

  j_tmp = dialplan_get_dpmas_all();
  if(j_tmp == NULL) {
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", json_object());
  json_object_set_new(json_object_get(j_res, "result"), "list", j_tmp);

  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * htp request handler.
 * request: POST ^/dp/dpmas$
 * @param req
 * @param data
 */
void dialplan_htp_post_dp_dpmas(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_data;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_post_dp_dpmas.");

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    // no request data
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create dpma info
  ret = create_dpma_info(j_data);
  json_decref(j_data);
  if(ret == false) {
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * GET ^/dp/dpmas/<detail> request handler.
 * @param req
 * @param data
 */
void dialplan_htp_get_dp_dpmas_detail(evhtp_request_t *req, void *data)
{
  json_t* j_tmp;
  json_t* j_res;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_get_dp_dpmas_detail.");

  // key parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get key info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get info
  j_tmp = dialplan_get_dpma_info(detail);
  sfree(detail);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dpma info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", j_tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * htp request handler.
 * PUT ^/dp/dpmas/<detail> request handler.
 * @param req
 * @param data
 */
void dialplan_htp_put_dp_dpmas_detail(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_data;
  char* detail;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_put_dp_dpmas_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    slog(LOG_ERR, "Could not get correct data from request.");
    sfree(detail);
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // update info
  ret = update_dpma_info(detail, j_data);
  sfree(detail);
  json_decref(j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not update dp_dpma info.");
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * DELETE ^/dp/dpmas/<detail> request handler.
 * @param req
 * @param data
 */
void dialplan_htp_delete_dp_dpmas_detail(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  char* detail;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_delete_dp_dpmas_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get name info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // delete dpma
  ret = delete_dpma_info(detail);
  sfree(detail);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete detail info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * GET ^/dp/dialplans request handler.
 * @param req
 * @param data
 */
void dialplan_htp_get_dp_dialplans(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_INFO, "Fired htp_get_dp_dialplans.");

  j_tmp = dialplan_get_dialplans_all();
  if(j_tmp == NULL) {
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", json_object());
  json_object_set_new(json_object_get(j_res, "result"), "list", j_tmp);

  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * htp request handler.
 * request: POST ^/dp/dialplans$
 * @param req
 * @param data
 */
void dialplan_htp_post_dp_dialplans(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_data;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_post_dp_dialplans.");

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    // no request data
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create dialplan info
  ret = create_dialplan_info(j_data);
  json_decref(j_data);
  if(ret == false) {
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * htp request handler.
 * request: GET ^/dp/dialplans/detail
 * @param req
 * @param data
 */
void dialplan_htp_get_dp_dialplans_detail(evhtp_request_t *req, void *data)
{
  json_t* j_tmp;
  json_t* j_res;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_get_dp_dialplans_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get key info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get info
  j_tmp = dialplan_get_dialplan_info(detail);
  sfree(detail);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dialplan info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", j_tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * htp request handler.
 * PUT ^/dp/dialplans/<detail> request handler.
 * @param req
 * @param data
 */
void dialplan_htp_put_dp_dialplans_detail(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_data;
  char* detail;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_put_dp_dialplans_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    slog(LOG_ERR, "Could not get correct data from request.");
    sfree(detail);
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // update info
  ret = update_dialplan_info(detail, j_data);
  sfree(detail);
  json_decref(j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not update dp_dpma info.");
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * DELETE ^/dp/dialplans/<detail> request handler.
 * @param req
 * @param data
 */
void dialplan_htp_delete_dp_dialplans_detail(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  char* detail;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired htp_delete_dp_dialplans_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get name info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // delete dialplan
  ret = delete_dialplan_info(detail);
  sfree(detail);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete detail info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * Update dpma.
 * @return
 */
static bool update_dpma_info(const char* uuid, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  char* timestamp;

  if((uuid == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired update_dpma_info.");

  ret = is_exist_dpma_info(uuid);
  if(ret == false) {
    slog(LOG_ERR, "The given dpma info is not exist. dpma_uuid[%s]", uuid);
    return false;
  }

  j_tmp = json_deep_copy(j_data);
  json_object_del(j_tmp, "uuid");
  json_object_del(j_tmp, "tm_create");
  json_object_del(j_tmp, "tm_update");

  json_object_set_new(j_tmp, "uuid", json_string(uuid));

  timestamp = utils_get_utc_timestamp();
  json_object_set_new(j_tmp, "tm_update", json_string(timestamp));
  sfree(timestamp);

  ret = dialplan_update_dpma_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not update dpma info.");
    return false;
  }

  return true;
}

/**
 * Delete dpma.
 * @return
 */
static bool delete_dpma_info(const char* uuid)
{
  int ret;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_dpma_info. uuid[%s]", uuid);

  ret = dialplan_delete_dpma_info(uuid);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete dpma info.");
    return false;
  }

  return true;
}

/**
 * Create dpma.
 * @return
 */
static bool create_dpma_info(const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  char* uuid;
  char* timestamp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired create_dp_dpma_info.");

  j_tmp = json_deep_copy(j_data);
  json_object_del(j_tmp, "tm_create");
  json_object_del(j_tmp, "tm_update");
  json_object_del(j_tmp, "tm_uuid");

  timestamp = utils_get_utc_timestamp();
  uuid = utils_gen_uuid();

  json_object_set_new(j_tmp, "uuid", json_string(uuid));
  sfree(uuid);
  json_object_set_new(j_tmp, "tm_create", json_string(timestamp));
  sfree(timestamp);

  // insert info
  ret = dialplan_create_dpma_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not insert dp_dpma contact.");
    return false;
  }

  return true;
}

/**
 * Create dialplan.
 * @return
 */
static bool create_dialplan_info(const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  char* uuid;
  char* timestamp;
  const char* dpma_uuid;
  int seq;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired create_dialplan_info.");

  // get dpma_uuid
  dpma_uuid = json_string_value(json_object_get(j_data, "dpma_uuid"));
  if(dpma_uuid == NULL) {
    slog(LOG_ERR, "Could not get dpma_uuid info.");
    return false;
  }

  // get sequence info
  seq = json_integer_value(json_object_get(j_data, "sequence"));
  if(seq <= 0) {
    slog(LOG_ERR, "Could not get correct sequence info.");
    return false;
  }

  // check existence
  ret = is_exist_dialplan_info(dpma_uuid, seq);
  if(ret == true) {
    slog(LOG_ERR, "The given dialplan info is already  exist. dpma_uuid[%s], seq[%d]", dpma_uuid, seq);
    return false;
  }

  j_tmp = json_deep_copy(j_data);
  json_object_del(j_tmp, "uuid");
  json_object_del(j_tmp, "tm_create");
  json_object_del(j_tmp, "tm_update");
  json_object_del(j_tmp, "tm_uuid");

  timestamp = utils_get_utc_timestamp();
  uuid = utils_gen_uuid();

  json_object_set_new(j_tmp, "uuid", json_string(uuid));
  sfree(uuid);
  json_object_set_new(j_tmp, "tm_create", json_string(timestamp));
  sfree(timestamp);

  // insert info
  ret = dialplan_create_dialplan_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not insert dp_dialplan contact.");
    return false;
  }

  return true;
}

/**
 * Update dialplan.
 * @return
 */
static bool update_dialplan_info(const char* uuid, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  char* timestamp;
  const char* dpma_uuid;

  if((uuid == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired update_dialplan_info.");

  ret = is_exist_dialplan_info_uuid(uuid);
  if(ret == false) {
    slog(LOG_ERR, "The given dialplan is not exist. uuid[%s]", uuid);
    return false;
  }

  dpma_uuid = json_string_value(json_object_get(j_data, "dpma_uuid"));
  if(dpma_uuid == NULL) {
    slog(LOG_ERR, "Could not get dpma_uuid info.");
    return false;
  }

  // check dpma existence
  ret = is_exist_dpma_info(dpma_uuid);
  if(ret == false) {
    slog(LOG_ERR, "The given dpma is not exist. dpma_uuid[%s]", dpma_uuid);
    return false;
  }

  j_tmp = json_deep_copy(j_data);
  json_object_del(j_tmp, "uuid");
  json_object_del(j_tmp, "tm_create");
  json_object_del(j_tmp, "tm_update");

  json_object_set_new(j_tmp, "uuid", json_string(uuid));

  timestamp = utils_get_utc_timestamp();
  json_object_set_new(j_tmp, "tm_update", json_string(timestamp));
  sfree(timestamp);

  ret = dialplan_update_dialplan_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not update dpma info.");
    return false;
  }

  return true;
}

/**
 * Delete dialplan.
 * @return
 */
static bool delete_dialplan_info(const char* uuid)
{
  int ret;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_dialplan_info. uuid[%s]", uuid);

  ret = dialplan_delete_dialplan_info(uuid);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete dialplan info.");
    return false;
  }

  return true;
}


static bool is_exist_dpma_info(const char* uuid)
{
  json_t* j_tmp;

  j_tmp = dialplan_get_dpma_info(uuid);
  if(j_tmp == NULL) {
    return false;
  }

  json_decref(j_tmp);
  return true;
}

static bool is_exist_dialplan_info_uuid(const char* uuid)
{
  json_t* j_tmp;

  j_tmp = dialplan_get_dialplan_info(uuid);
  if(j_tmp == NULL) {
    return false;
  }
  json_decref(j_tmp);

  return true;

}

static bool is_exist_dialplan_info(const char* dpma_uuid, int seq)
{
  json_t* j_tmp;

  j_tmp = dialplan_get_dialplan_info_by_dpma_seq(dpma_uuid, seq);
  if(j_tmp == NULL) {
    return false;
  }
  json_decref(j_tmp);

  return true;
}

/**
 * Add the commands to the agi channel.
 * @param unique_id
 * @return
 */
bool dialplan_add_cmds(const char* agi_uuid)
{
  json_t* j_agi;
  json_t* j_dps;
  json_t* j_dp;
  int ret;
  int idx;
  char* uuid;
  const char* jade_dialplan;
  const char* dpma_uuid;
  const char* channel;
  const char* dp_uuid;
  const char* command;

  if(agi_uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired add_dialplan_cmds. agi_uuid[%s]", agi_uuid);

  // get agi info
  j_agi = get_core_agi_info(agi_uuid);
  if(j_agi == NULL) {
    slog(LOG_ERR, "Could not get agi info.");
    return false;
  }

  // check the given agi is for jade_dialplan or not
  jade_dialplan = json_string_value(json_object_get(json_object_get(j_agi, "env"), "agi_arg_1"));
  if(jade_dialplan == NULL) {
    slog(LOG_NOTICE, "The given agi is not for the jade_dialplan.");
    json_decref(j_agi);
    return false;
  }
  if(strcasecmp(jade_dialplan, "jade_dialplan") != 0) {
    slog(LOG_NOTICE, "Unmatched jade_dialplan. The given agi is not for the jade_dialplan.");
    json_decref(j_agi);
    return false;
  }

  // get dpma_uuid
  // consider agi_arg_1 as a dpma_uuid
  dpma_uuid = json_string_value(json_object_get(json_object_get(j_agi, "env"), "agi_arg_2"));
  if(dpma_uuid == NULL) {
    slog(LOG_NOTICE, "Could not get dpma_uuid info.");
    json_decref(j_agi);
    return false;
  }

  ret = is_exist_dpma_info(dpma_uuid);
  if(ret == false) {
    slog(LOG_ERR, "The given dpma_uuid is not exist.");
    json_decref(j_agi);
    return false;
  }

  // get dialplans
  j_dps = dialplan_get_dialplans_by_dpma_uuid_order_sequence(dpma_uuid);
  if(j_dps == NULL) {
    slog(LOG_ERR, "Could not get dialplan info");
    json_decref(j_agi);
    return false;
  }

  // send agi request
  json_array_foreach(j_dps, idx, j_dp) {
    uuid = utils_gen_uuid();

    channel = json_string_value(json_object_get(j_agi, "channel"));
    command = json_string_value(json_object_get(j_dp, "command"));
    dp_uuid = json_string_value(json_object_get(j_dp, "uuid"));

    // send ami action
    ret = ami_action_agi(channel, command, uuid);
    if(ret == false) {
      sfree(uuid);
      continue;
    }

    // add cmd
    ret = add_core_agi_info_cmd(agi_uuid, uuid, command, dp_uuid);
    sfree(uuid);
    if(ret == false) {
      slog(LOG_ERR, "Could not add cmd info.");
      continue;
    }
  }
  json_decref(j_dps);
  json_decref(j_agi);

  return true;
}

/**
 * Get all dp_dpma array
 * @return
 */
json_t* dialplan_get_dpmas_all(void)
{
  json_t* j_res;

  j_res = resource_get_file_items("dp_dpma", "*");
  return j_res;
}

/**
 * Get corresponding dp_dpma detail info.
 * @return
 */
json_t* dialplan_get_dpma_info(const char* key)
{
  json_t* j_res;

  if(key == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired get_dp_dpma_info. key[%s]", key);

  j_res = resource_get_file_detail_item_key_string("dp_dpma", "uuid", key);

  return j_res;
}


/**
 * Create dp dpma info.
 * @param j_data
 * @return
 */
bool dialplan_create_dpma_info(const json_t* j_data)
{
  int ret;
  const char* tmp_const;
  json_t* j_tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired create_dp_dpma_info.");

  // insert info
  ret = resource_insert_file_item("dp_dpma", j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not insert dp_dpma info.");
    return false;
  }

  // publish
  // get info
  tmp_const = json_string_value(json_object_get(j_data, "uuid"));
  j_tmp = dialplan_get_dpma_info(tmp_const);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dpma info. uuid[%s]", tmp_const);
    return false;
  }

  // publish event
  ret = publication_publish_event_dp_dpma(DEF_PUB_TYPE_CREATE, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Update dp_dpma info.
 * @param j_data
 * @return
 */
bool dialplan_update_dpma_info(const json_t* j_data)
{
  int ret;
  const char* tmp_const;
  json_t* j_tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired update_dp_dpma_info.");

  // update
  ret = resource_update_file_item("dp_dpma", "uuid", j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not update dp_dpma info.");
    return false;
  }

  // publish
  // get info
  tmp_const = json_string_value(json_object_get(j_data, "uuid"));
  j_tmp = dialplan_get_dpma_info(tmp_const);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dpma info. uuid[%s]", tmp_const);
    return false;
  }

  // publish event
  ret = publication_publish_event_dp_dpma(DEF_PUB_TYPE_UPDATE, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Delete dp_dpma info.
 * @param j_data
 * @return
 */
bool dialplan_delete_dpma_info(const char* key)
{
  int ret;
  json_t* j_tmp;

  if(key == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_dp_dpma_info. key[%s]", key);

  // get info
  j_tmp = dialplan_get_dpma_info(key);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dpma info. uuid[%s]", key);
    return false;
  }

  ret = resource_delete_file_items_string("dp_dpma", "uuid", key);
  if(ret == false) {
    slog(LOG_WARNING, "Could not delete dp_dpma info. key[%s]", key);
    json_decref(j_tmp);
    return false;
  }

  // publish
  // publish event
  ret = publication_publish_event_dp_dpma(DEF_PUB_TYPE_DELETE, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Get all dp_dialplan array
 * @return
 */
json_t* dialplan_get_dialplans_all(void)
{
  json_t* j_res;

  j_res = resource_get_file_items("dp_dialplan", "*");
  return j_res;
}

/**
 * Get all dp_dialplans by dpma_uuid order by sequence
 * @return
 */
json_t* dialplan_get_dialplans_by_dpma_uuid_order_sequence(const char* dpma_uuid)
{
  json_t* j_res;
  json_t* j_obj;

  if(dpma_uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_obj = json_pack("{s:s}", "dpma_uuid", dpma_uuid);

  j_res = resource_get_file_detail_items_by_obj_order("dp_dialplan", j_obj, "sequence");
  json_decref(j_obj);

  return j_res;
}

/**
 * Get corresponding dp_dialplan detail info.
 * @return
 */
json_t* dialplan_get_dialplan_info(const char* key)
{
  json_t* j_res;

  if(key == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired get_dp_dialplan_info. key[%s]", key);

  j_res = resource_get_file_detail_item_key_string("dp_dialplan", "uuid", key);

  return j_res;
}

/**
 * Get corresponding dp_dialplan detail info.
 * @return
 */
json_t* dialplan_get_dialplan_info_by_dpma_seq(const char* dpma_uuid, int seq)
{
  json_t* j_res;
  json_t* j_obj;

  if(dpma_uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired get_dp_dialplan_info_by_dpma_seq. dpma_uuid[%s], sequence[%d]", dpma_uuid, seq);

  j_obj = json_pack("{s:s, s:i}",
      "dpma_uuid",  dpma_uuid,
      "sequence",   seq
      );

  j_res = resource_get_file_detail_item_by_obj("dp_dialplan", j_obj);
  json_decref(j_obj);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}


/**
 * Create dp_dialplan info.
 * @param j_data
 * @return
 */
bool dialplan_create_dialplan_info(const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  const char* tmp_const;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired create_dp_dialplan_info.");

  // insert info
  ret = resource_insert_file_item("dp_dialplan", j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not insert dp_dialplan contact.");
    return false;
  }

  // publish
  // get info
  tmp_const = json_string_value(json_object_get(j_data, "uuid"));
  j_tmp = dialplan_get_dialplan_info(tmp_const);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dialplan info. uuid[%s]", tmp_const);
    return false;
  }

  // publish event
  ret = publication_publish_event_dp_dialplan(DEF_PUB_TYPE_CREATE, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Update dp_dialplan info.
 * @param j_data
 * @return
 */
bool dialplan_update_dialplan_info(const json_t* j_data)
{
  int ret;
  const char* tmp_const;
  json_t* j_tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired update_dp_dialplan_info.");

  // update
  ret = resource_update_file_item("dp_dialplan", "uuid", j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not update dp_dialplan info.");
    return false;
  }

  // publish
  // get info
  tmp_const = json_string_value(json_object_get(j_data, "uuid"));
  j_tmp = dialplan_get_dialplan_info(tmp_const);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dialplan info. uuid[%s]", tmp_const);
    return false;
  }

  // publish event
  ret = publication_publish_event_dp_dialplan(DEF_PUB_TYPE_UPDATE, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Delete dp_dialplan info.
 * @param j_data
 * @return
 */
bool dialplan_delete_dialplan_info(const char* key)
{
  int ret;
  json_t* j_tmp;

  if(key == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_dp_dialplan_info. key[%s]", key);

  // get info
  j_tmp = dialplan_get_dialplan_info(key);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get dp_dialplan info. uuid[%s]", key);
    return false;
  }

  ret = resource_delete_file_items_string("dp_dialplan", "uuid", key);
  if(ret == false) {
    slog(LOG_WARNING, "Could not delete dp_dialplan info. key[%s]", key);
    json_decref(j_tmp);
    return false;
  }

  // publish
  // publish event
  ret = publication_publish_event_dp_dialplan(DEF_PUB_TYPE_DELETE, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

