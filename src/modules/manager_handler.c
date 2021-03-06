/*
 * manager_handler.c
 *
 *  Created on: Apr 20, 2018
 *      Author: pchero
 */

#define _GNU_SOURCE

#include <evhtp.h>
#include <string.h>

#include "slog.h"
#include "common.h"
#include "utils.h"

#include "http_handler.h"
#include "user_handler.h"
#include "pjsip_handler.h"
#include "chat_handler.h"
#include "publication_handler.h"
#include "dialplan_handler.h"

#include "manager_handler.h"

#define DEF_MANAGER_AUTHTOKEN_TYPE          "manager"

#define DEF_PUBLISH_TOPIC_PREFIX_MANAGER     "/manager"   // topic: DEF_PUBLISH_TOPIC_PREFIX_MANAGER_INFO

#define DEF_PUB_EVENT_PREFIX_MANAGER_NOTICE   "manager.notice"
#define DEF_PUB_EVENT_PREFIX_MANAGER_USER     "manager.user"
#define DEF_PUB_EVENT_PREFIX_MANAGER_TRUNK    "manager.trunk"

// info
static json_t* get_manager_info(const json_t* j_user);
static bool update_manager_info(const json_t* j_user, const json_t* j_data);

// user
static json_t* get_users_all(void);
static json_t* get_user_info(const char* uuid_user);
static bool create_user_info(const json_t* j_data);
static bool update_user_info(const char* uuid_user, const json_t* j_data);
static bool delete_user_info(const char* uuid);

static char* get_user_pjsip_account(const char* uuid_user);
static bool create_user_pjsip_account(const char* target, const json_t* j_data);
static bool update_user_pjsip_account(const char* uuid_user, const json_t* j_data);

static char* get_user_context(const char* uuid_user);
static bool create_user_contact(const char* uuid_user, const char* target);
static bool create_user_permission(const char* uuid_user, const json_t* j_data);
static bool create_user_userinfo(const char* uuid_user, const json_t* j_data);

static bool update_user_permission(const char* uuid_user, const json_t* j_data);
static bool update_user_user(const char* uuid_user, const json_t* j_data);

// trunk
static json_t* get_trunks_all(void);
static json_t* get_trunk_info(const char* name);
static bool create_trunk_info(const json_t* j_data);
static bool update_trunk_info(const char* name, const json_t* j_data);
static bool delete_trunk_info(const char* name);

static bool is_exist_trunk(const char* name);


// sdialplan
static json_t* get_sdialplans_all(void);
static json_t* get_sdialplan_info(const char* name);
static bool create_sdialplan_info(const json_t* j_data);
static bool update_sdialplan_info(const char* name, const json_t* j_data);
static bool delete_sdialplan_info(const char* name);


// callback
static bool cb_resource_handler_user_db_userinfo(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data);
static bool cb_resource_handler_user_db_permission(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data);

static bool cb_resource_handler_pjsip_module(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data);
static bool cb_resource_handler_pjsip_db_registration_outbound(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data);


bool manager_init_handler(void)
{

  // register callback
  user_register_callback_db_userinfo(&cb_resource_handler_user_db_userinfo);
  user_register_callback_db_permission(&cb_resource_handler_user_db_permission);

  pjsip_register_callback_module(&cb_resource_handler_pjsip_module);
  pjsip_register_callback_db_registration_outbound(&cb_resource_handler_pjsip_db_registration_outbound);

  return true;
}

bool manager_term_handler(void)
{
  return true;
}

bool manager_reload_handler(void)
{
  int ret;

  ret = manager_term_handler();
  if(ret == false) {
    return false;
  }

  ret = manager_init_handler();
  if(ret == false) {
    return false;
  }

  return true;
}

/**
 * POST ^/manager/login request handler.
 * @param req
 * @param data
 */
void manager_htp_post_manager_login(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;
  char* username;
  char* password;
  char* authtoken;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_post_manager_login.");

  // get username/pass
  ret = http_get_htp_id_pass(req, &username, &password);
  if(ret == false) {
    sfree(username);
    sfree(password);
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create authtoken
  authtoken = user_create_authtoken(username, password, DEF_MANAGER_AUTHTOKEN_TYPE);
  sfree(username);
  sfree(password);
  if(authtoken == NULL) {
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  j_tmp = json_pack("{s:s}",
      "authtoken",  authtoken
      );
  sfree(authtoken);

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
 * request: DELETE ^/manager/login$
 * @param req
 * @param data
 */
void manager_htp_delete_manager_login(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  const char* authtoken;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_delete_manager_login.");

  // get authtoken
  authtoken = evhtp_kv_find(req->uri->query, "authtoken");
  if(authtoken == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // delete authtoken
  ret = user_delete_authtoken_info(authtoken);
  if(ret == false) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  j_res = http_create_default_result(EVHTP_RES_OK);
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}


/**
 * GET ^/manager/info request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_info(evhtp_request_t *req, void *data)
{
  json_t* j_user;
  json_t* j_res;
  json_t* j_tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_info.");

  // get userinfo
  j_user = http_get_userinfo(req);
  if(j_user == NULL) {
    http_simple_response_error(req, EVHTP_RES_FORBIDDEN, 0, NULL);
    return;
  }

  // get info
  j_tmp = get_manager_info(j_user);
  json_decref(j_user);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get manager info.");
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
 * PUT ^/manager/info request handler.
 * @param req
 * @param data
 */
void manager_htp_put_manager_info(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  json_t* j_data;
  json_t* j_user;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_put_manager_users.");

  // get userinfo
  j_user = http_get_userinfo(req);
  if(j_user == NULL) {
    http_simple_response_error(req, EVHTP_RES_FORBIDDEN, 0, NULL);
    return;
  }

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    json_decref(j_user);
    return;
  }

  // update info
  ret = update_manager_info(j_user, j_data);
  json_decref(j_data);
  json_decref(j_user);
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
 * GET ^/manager/users request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_users(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_users.");

  // get info
  j_tmp = get_users_all();
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get users info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", json_object());
  json_object_set_new(json_object_get(j_res, "result"), "list", j_tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * POST ^/manager/users request handler.
 * @param req
 * @param data
 */
void manager_htp_post_manager_users(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  json_t* j_data;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_post_manager_users.");

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create info
  ret = create_user_info(j_data);
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
 * GET ^/manager/users/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_users_detail(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_users_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get user info
  j_tmp = get_user_info(detail);
  sfree(detail);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get manager user info.");
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
 * DELETE ^/manager/users/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_delete_manager_users_detail(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_delete_manager_users_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // delete user info
  ret = delete_user_info(detail);
  sfree(detail);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete manager user info.");
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
 * PUT ^/manager/users/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_put_manager_users_detail(evhtp_request_t *req, void *data)
{
  int ret;
  char* detail;
  json_t* j_res;
  json_t* j_data;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_put_manager_users.");

  // get detail
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    sfree(detail);
    return;
  }

  // update info
  ret = update_user_info(detail, j_data);
  json_decref(j_data);
  sfree(detail);
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
 * POST ^/manager/trunks request handler.
 * @param req
 * @param data
 */
void manager_htp_post_manager_trunks(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  json_t* j_data;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_post_manager_trunks.");

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create info
  ret = create_trunk_info(j_data);
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
 * GET ^/manager/trunks request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_trunks(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_trunks.");

  // get info
  j_tmp = get_trunks_all();
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get users info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", json_object());
  json_object_set_new(json_object_get(j_res, "result"), "list", j_tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * GET ^/manager/trunks/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_trunks_detail(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_trunks_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get user info
  j_tmp = get_trunk_info(detail);
  sfree(detail);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get manager trunk info.");
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
 * DELETE ^/manager/trunks/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_delete_manager_trunks_detail(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_delete_manager_trunks_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // delete user info
  ret = delete_trunk_info(detail);
  sfree(detail);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete manager trunk info.");
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
 * PUT ^/manager/trunks/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_put_manager_trunks_detail(evhtp_request_t *req, void *data)
{
  int ret;
  char* detail;
  json_t* j_res;
  json_t* j_data;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_put_manager_trunks_detail.");

  // get detail
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    sfree(detail);
    return;
  }

  // update info
  ret = update_trunk_info(detail, j_data);
  json_decref(j_data);
  sfree(detail);
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
 * GET ^/manager/sdialplans request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_sdialplans(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_sdialplans.");

  // get info
  j_tmp = get_sdialplans_all();
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get sdialplan info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", json_object());
  json_object_set_new(json_object_get(j_res, "result"), "list", j_tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * POST ^/manager/sdialplans request handler.
 * @param req
 * @param data
 */
void manager_htp_post_manager_sdialplans(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  json_t* j_data;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_post_manager_sdialplans.");

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create info
  ret = create_sdialplan_info(j_data);
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
 * GET ^/manager/sdialplans/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_sdialplans_detail(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_sdialplans_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get info
  j_tmp = get_sdialplan_info(detail);
  sfree(detail);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get manager sdialplan info.");
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
 * DELETE ^/manager/sdialplans/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_delete_manager_sdialplans_detail(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  char* detail;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_delete_manager_sdialplans_detail.");

  // detail parse
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // delete info
  ret = delete_sdialplan_info(detail);
  sfree(detail);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete manager sdialplan info.");
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
 * PUT ^/manager/sdialplans/<detail> request handler.
 * @param req
 * @param data
 */
void manager_htp_put_manager_sdialplans_detail(evhtp_request_t *req, void *data)
{
  int ret;
  char* detail;
  json_t* j_res;
  json_t* j_data;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_put_manager_sdialplans_detail.");

  // get detail
  detail = http_get_parsed_detail(req);
  if(detail == NULL) {
    slog(LOG_ERR, "Could not get detail info.");
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    sfree(detail);
    return;
  }

  // update info
  ret = update_sdialplan_info(detail, j_data);
  json_decref(j_data);
  sfree(detail);
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

static json_t* get_user_info(const char* uuid_user)
{
  json_t* j_res;
  json_t* j_perms;
  json_t* j_perm;
  int idx;
  char* context;

  if(uuid_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get user info
  j_res = user_get_userinfo_info(uuid_user);
  if(j_res == NULL) {
    slog(LOG_NOTICE, "Could not get user info.");
    return NULL;
  }

  // get permissions info
  j_perms = user_get_permissions_by_useruuid(uuid_user);
  if(j_perms != NULL) {

    json_array_foreach(j_perms, idx, j_perm) {
      json_object_del(j_perm, "user_uuid");
      json_object_del(j_perm, "uuid");
    }

    json_object_set_new(j_res, "permissions", j_perms);
  }

  // get context info
  context = get_user_context(uuid_user);
  json_object_set_new(j_res, "context", context? json_string(context) : json_string(""));
  sfree(context);

  return j_res;
}

/**
 * Get given manager info.
 * @param authtoken
 * @return
 */
static json_t* get_manager_info(const json_t* j_user)
{
  json_t* j_res;
  const char* uuid_user;

  if(j_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired get_info.");

  uuid_user = json_string_value(json_object_get(j_user, "uuid"));
  if(uuid_user == NULL) {
    slog(LOG_NOTICE, "Could not get user uuid info.");
    return NULL;
  }

  // get user info
  j_res = user_get_userinfo_info(uuid_user);
  if(j_res == NULL) {
    slog(LOG_NOTICE, "Could not get user info.");
    return NULL;
  }

  // remove password object
  json_object_del(j_res, "password");

  return j_res;
}

/**
 * Update given manager info.
 * @param j_user
 * @param j_data
 * @return
 */
static bool update_manager_info(const json_t* j_user, const json_t* j_data)
{
  int ret;
  const char* uuid;

  if((j_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired update_manager_info.");

  uuid = json_string_value(json_object_get(j_user, "uuid"));
  if(uuid == NULL) {
    slog(LOG_NOTICE, "Could not get user uuid info.");
    return false;
  }

  // update
  ret = user_update_userinfo_info(uuid, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update userinfo.");
    return false;
  }

  return true;
}

static json_t* get_users_all(void)
{
  json_t* j_res;
  json_t* j_tmp;
  json_t* j_users;
  json_t* j_user;
  int idx;
  const char* uuid;

  j_users = user_get_userinfos_all();
  if(j_users == NULL) {
    return NULL;
  }

  j_res = json_array();
  json_array_foreach(j_users, idx, j_user) {
    uuid = json_string_value(json_object_get(j_user, "uuid"));

    j_tmp = get_user_info(uuid);
    if(j_tmp == NULL) {
      continue;
    }

    json_array_append_new(j_res, j_tmp);
  }
  json_decref(j_users);

  return j_res;
}

static bool create_user_pjsip_account(const char* target, const json_t* j_data)
{
  int ret;
  const char* context;

  if((target == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  context = json_string_value(json_object_get(j_data, "context"));
  if(context == NULL) {
    slog(LOG_NOTICE, "Could not get context info.");
    return false;
  }

  ret = pjsip_create_account_with_default_setting(target, context);
  if(ret == false) {
    slog(LOG_WARNING, "Could not create pjsip target info.");
    return false;
  }

  // reload pjsip config
  ret = pjsip_reload_config();
  if(ret == false) {
    slog(LOG_WARNING, "Could not reload pjsip_handler.");
    return false;
  }

  return true;
}

/**
 * Update user's account info.
 * @param uuid_user
 * @param j_data
 * @return
 */
static bool update_user_pjsip_account(const char* uuid_user, const json_t* j_data)
{
  int ret;
  const char* context;
  char* account;
  json_t* j_endpoint;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  context = json_string_value(json_object_get(j_data, "context"));
  if(context == NULL) {
    slog(LOG_NOTICE, "Could not get context info.");
    return false;
  }

  // get account
  account = get_user_pjsip_account(uuid_user);
  if(account == NULL) {
    slog(LOG_NOTICE, "Could not get account info. user_uuid[%s]", uuid_user);
    return false;
  }

  // to change user's context,
  // we need to change endpoint configurations
  j_endpoint = pjsip_cfg_get_endpoint_info_data(account);
  if(j_endpoint == NULL) {
    slog(LOG_NOTICE, "Could not get endpoint info.");
    sfree(account);
    return false;
  }

  json_object_set_new(j_endpoint, "context", json_string(context));
  ret = pjsip_cfg_update_endpoint_info_data(account, j_endpoint);
  sfree(account);
  json_decref(j_endpoint);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update endpoint info.");
    return false;
  }

  // reload pjsip config
  ret = pjsip_reload_config();
  if(ret == false) {
    slog(LOG_WARNING, "Could not reload pjsip_handler.");
    return false;
  }

  return true;
}

static bool delete_user_pjsip_account(const char* uuid_user)
{
  int ret;
  char* account;

  if(uuid_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get account
  account = get_user_pjsip_account(uuid_user);
  if(account == NULL) {
    slog(LOG_NOTICE, "Could not get account info.");
    return false;
  }

  ret = pjsip_delete_account(account);
  sfree(account);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete account info.");
    return false;
  }

  // reload pjsip config
  ret = pjsip_reload_config();
  if(ret == false) {
    slog(LOG_WARNING, "Could not reload pjsip_handler.");
    return false;
  }

  return true;
}

static bool create_user_userinfo(const char* uuid_user, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  const char* username;
  const char* password;
  const char* name;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get mandatory items
  username = json_string_value(json_object_get(j_data, "username"));
  password = json_string_value(json_object_get(j_data, "password"));
  name = json_string_value(json_object_get(j_data, "name"));
  if((username == NULL) || (password == NULL) || (name == NULL)) {
    slog(LOG_NOTICE, "Could not get mandatory items.");
    return false;
  }

  j_tmp = json_pack("{s:s, s:s, s:s}",
      "username",   username,
      "password",   password,
      "name",       name
      );
  ret = user_create_userinfo(uuid_user, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_WARNING, "Could not create userinfo.");
    return false;
  }

  return true;
}

static bool create_user_permission(const char* uuid_user, const json_t* j_data)
{
  int ret;
  int idx;
  json_t* j_tmp;
  json_t* j_permissions;
  json_t* j_permission;
  const char* permission;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_permissions = json_object_get(j_data, "permissions");
  if(j_permissions == NULL) {
    slog(LOG_NOTICE, "Could not get permissions info.");
    return false;
  }

  // create each permissions
  json_array_foreach(j_permissions, idx, j_permission) {
    permission = json_string_value(json_object_get(j_permission, "permission"));
    if(permission == NULL) {
      continue;
    }

    j_tmp = json_pack("{s:s, s:s}",
        "user_uuid",    uuid_user,
        "permission",   permission
        );
    ret = user_create_permission_info(j_tmp);
    json_decref(j_tmp);
    if(ret == false) {
      slog(LOG_WARNING, "Could not create permission info.");
    }
  }

  return true;
}

static bool create_user_contact(const char* uuid_user, const char* target)
{
  int ret;
  json_t* j_tmp;

  if((uuid_user == NULL) || (target == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_tmp = json_pack("{s:s, s:s}",
      "user_uuid",    uuid_user,
      "target",       target
      );

  ret = user_create_contact_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    return false;
  }

  return true;
}

static char* get_user_pjsip_account(const char* uuid_user)
{
  char* res;
  const char* tmp_const;
  json_t* j_contacts;
  json_t* j_contact;

  if(uuid_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_contacts = user_get_contacts_by_user_uuid(uuid_user);
  if(j_contacts == NULL) {
    slog(LOG_NOTICE, "Could not get contacts info. user_uuid[%s]", uuid_user);
    return NULL;
  }

  j_contact = json_array_get(j_contacts, 0);
  if(j_contact == NULL) {
    slog(LOG_INFO, "The given user has no contact. user_uuid[%s]", uuid_user);
    json_decref(j_contacts);
    return NULL;
  }

  tmp_const = json_string_value(json_object_get(j_contact, "target"));
  if(tmp_const == NULL) {
    slog(LOG_ERR, "Could not get target info.");
    json_decref(j_contacts);
    return NULL;
  }

  res = strdup(tmp_const);
  json_decref(j_contacts);

  return res;
}

static char* get_user_context(const char* uuid_user)
{
  char* res;
  const char* tmp_const;
  char* account;
  json_t* j_endpoint;


  if(uuid_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get pjsip account
  account = get_user_pjsip_account(uuid_user);
  if(account == NULL) {
    slog(LOG_NOTICE, "Could not get target info.");
    return NULL;
  }

  // get endpoint info
  j_endpoint = pjsip_get_endpoint_info(account);
  sfree(account)
  if(j_endpoint == NULL) {
    slog(LOG_NOTICE, "Could not get endpoint info.");
    return NULL;
  }

  // get context
  tmp_const = json_string_value(json_object_get(j_endpoint, "context"));
  if(tmp_const == NULL) {
    slog(LOG_NOTICE, "Could not get context info.");
    json_decref(j_endpoint);
    return NULL;
  }

  res = strdup(tmp_const);
  json_decref(j_endpoint);

  return res;
}

static bool create_user_info(const json_t* j_data)
{
  int ret;
  char* target;
  char* uuid_user;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // generate info
  target = utils_gen_uuid();
  uuid_user = utils_gen_uuid();

  // create pjsip_account
  ret = create_user_pjsip_account(target, j_data);
  if(ret == false) {
    slog(LOG_WARNING, "Could not create user target info.");
    delete_user_pjsip_account(target);
    sfree(target);
    sfree(uuid_user);
    return false;
  }

  // create user
  ret = create_user_userinfo(uuid_user, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create user user info.");
    delete_user_pjsip_account(target);
    sfree(uuid_user);
    sfree(target);
    return false;
  }

  // create permission
  ret = create_user_permission(uuid_user, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create user permission info.");
    delete_user_pjsip_account(target);
    user_delete_userinfo_info(uuid_user);
    sfree(uuid_user);
    sfree(target);
    return false;
  }

  // create contact
  ret = create_user_contact(uuid_user, target);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create user contact info.");
    delete_user_pjsip_account(target);
    user_delete_userinfo_info(uuid_user);
    sfree(uuid_user);
    sfree(target);
    return false;
  }

  sfree(target);
  sfree(uuid_user);

  return true;
}

static bool delete_user_info(const char* uuid)
{
  int ret;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // delete account
  ret = delete_user_pjsip_account(uuid);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete user pjsip account info. uuid[%s]", uuid);
    return false;
  }

  // delete userinfo
  ret = user_delete_related_info_by_useruuid(uuid);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete user info. uuid[%s]", uuid);
    return false;
  }

  // delete chatinfo
  ret = chat_delete_info_by_useruuid(uuid);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete chat info.");
    return false;
  }

  return true;
}

static bool update_user_user(const char* uuid_user, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_tmp = json_pack("{s:s, s:s}",
      "name",       json_string_value(json_object_get(j_data, "name"))? : "",
      "password",   json_string_value(json_object_get(j_data, "password"))? : ""
      );

  ret = user_update_userinfo_info(uuid_user, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    return false;
  }

  return true;
}

static bool update_user_permission(const char* uuid_user, const json_t* j_data)
{
  int ret;
  const char* tmp_const;
  json_t* j_perms;
  json_t* j_perm;
  json_t* j_tmp;
  int idx;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_perms = json_object_get(j_data, "permissions");
  if(j_perms == NULL) {
    slog(LOG_NOTICE, "Could not get permissions info.");
    return false;
  }

  // delete all previous permissions
  ret = user_delete_permissions_by_useruuid(uuid_user);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete permission info.");
    return false;
  }

  // create new permissions
  json_array_foreach(j_perms, idx, j_perm) {
    tmp_const = json_string_value(json_object_get(j_perm, "permission"));

    j_tmp = json_pack("{s:s, s:s}",
        "user_uuid",    uuid_user,
        "permission",   tmp_const
        );

    ret = user_create_permission_info(j_tmp);
    json_decref(j_tmp);
    if(ret == false) {
      slog(LOG_NOTICE, "Could not create permission info.");
    }
  }

  return true;
}

static bool update_user_info(const char* uuid_user, const json_t* j_data)
{
  int ret;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // update userinfo
  ret = update_user_user(uuid_user, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update user info.");
    return false;
  }

  // update permission
  ret = update_user_permission(uuid_user, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update permission info.");
    return false;
  }

  // upate pjsip account info
  ret = update_user_pjsip_account(uuid_user, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update pjsip account info.");
    return false;
  }

  return true;
}

/**
 * Returns all subscribable topics of manager module.
 * @param j_user
 * @return
 */
json_t* manager_get_subscribable_topics_all(const json_t* j_user)
{
  char* topic;
  json_t* j_res;

  if(j_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_res = json_array();

  // set topics
  asprintf(&topic, "%s", DEF_PUBLISH_TOPIC_PREFIX_MANAGER);
  json_array_append_new(j_res, json_string(topic));
  sfree(topic);

  return j_res;
}

/**
 * Callback handler for user_userinfo.
 * @param type
 * @param j_data
 * @return
 */
static bool cb_resource_handler_user_db_userinfo(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data)
{
  char* topic;
  int ret;
  const char* tmp_const;
  json_t* j_event;
  enum EN_PUBLISH_TYPES event_type;

  if((j_data == NULL)){
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired cb_handler_user_userinfo.");

  // create event depends on type
  if(type == EN_RESOURCE_CREATE) {
    // set event type
    event_type = EN_PUBLISH_CREATE;

    // get userinfo
    tmp_const = json_string_value(json_object_get(j_data, "uuid"));
    if(tmp_const == NULL) {
      slog(LOG_NOTICE, "Could not get user uuid.");
      return false;
    }

    // create event
    j_event = get_user_info(tmp_const);
    if(j_event == NULL) {
      slog(LOG_NOTICE, "Could not create event user info.");
      return false;
    }
  }
  else if(type == EN_RESOURCE_UPDATE) {
    // set event type
    event_type = EN_PUBLISH_UPDATE;

    // get userinfo
    tmp_const = json_string_value(json_object_get(j_data, "uuid"));
    if(tmp_const == NULL) {
      return false;
    }

    // create event
    j_event = get_user_info(tmp_const);
    if(j_event == NULL) {
      slog(LOG_NOTICE, "Could not create event user info.");
      return false;
    }
  }
  else if(type == EN_RESOURCE_DELETE) {
    // set event type
    event_type = EN_PUBLISH_DELETE;

    // create event
    j_event = json_deep_copy(j_data);
  }
  else {
    // something was wrong
    slog(LOG_ERR, "Unsupported resource update type. type[%d]", type);
    return false;
  }

  // create topic
  asprintf(&topic, "%s", DEF_PUBLISH_TOPIC_PREFIX_MANAGER);

  // publish event
  ret = publication_publish_event(topic, DEF_PUB_EVENT_PREFIX_MANAGER_USER, event_type, j_event);
  sfree(topic);
  json_decref(j_event);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Callback handler.
 * publish event.
 * manager.user.<type>
 * @param type
 * @param j_data
 * @return
 */
static bool cb_resource_handler_user_db_permission(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data)
{
  char* topic;
  int ret;
  const char* uuid;
  json_t* j_tmp;

  if((j_data == NULL)){
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired cb_resource_handler_user_permission.");

  // get userinfo
  uuid = json_string_value(json_object_get(j_data, "user_uuid"));
  if(uuid == NULL) {
    slog(LOG_NOTICE, "Could not get user_uuid info.");
    return false;
  }

  j_tmp = get_user_info(uuid);
  if(j_tmp == NULL) {
    // may already user removed.
    // skip the print log
    return false;
  }

  // create topic
  asprintf(&topic, "%s", DEF_PUBLISH_TOPIC_PREFIX_MANAGER);

  // publish event
  ret = publication_publish_event(topic, DEF_PUB_EVENT_PREFIX_MANAGER_USER, EN_PUBLISH_UPDATE, j_tmp);
  sfree(topic);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Callback handler for pjsip_registration_outbound.
 * @param type
 * @param j_data
 * @return
 */
static bool cb_resource_handler_pjsip_db_registration_outbound(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data)
{
  char* topic;
  int ret;
  const char* tmp_const;
  json_t* j_event;
  enum EN_PUBLISH_TYPES event_type;

  if((j_data == NULL)){
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired cb_resource_handler_pjsip_registration_outbound.");

  // create event depends on type
  if(type == EN_RESOURCE_CREATE) {
    // set event type

    // not create. it's update
    // because of, when the pjsip_registration_outbound has been updated, it's already
    // after the configuration item created.
    event_type = EN_PUBLISH_UPDATE;

    // get info
    tmp_const = json_string_value(json_object_get(j_data, "object_name"));
    if(tmp_const == NULL) {
      slog(LOG_NOTICE, "Could not get registration outbound object_name.");
      return false;
    }

    // create event
    j_event = get_trunk_info(tmp_const);
    if(j_event == NULL) {
      slog(LOG_NOTICE, "Could not create event trunk info.");
      return false;
    }
  }
  else if(type == EN_RESOURCE_UPDATE) {
    // set event type
    event_type = EN_PUBLISH_UPDATE;

    // get info
    tmp_const = json_string_value(json_object_get(j_data, "object_name"));
    if(tmp_const == NULL) {
      slog(LOG_NOTICE, "Could not get registration outbound object_name.");
      return false;
    }

    // create event
    j_event = get_trunk_info(tmp_const);
    if(j_event == NULL) {
      slog(LOG_NOTICE, "Could not create event trunk info.");
      return false;
    }
  }
  else {
    slog(LOG_ERR, "Unsupported resource update type. type[%d]", type);
    return false;
  }

  // create topic
  asprintf(&topic, "%s", DEF_PUBLISH_TOPIC_PREFIX_MANAGER);

  // publish event
  ret = publication_publish_event(topic, DEF_PUB_EVENT_PREFIX_MANAGER_TRUNK, event_type, j_event);
  sfree(topic);
  json_decref(j_event);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

/**
 * Callback handler for pjsip module
 * @param type
 * @param j_data
 * @return
 */
static bool cb_resource_handler_pjsip_module(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data)
{
  char* topic;
  int ret;
  json_t* j_event;
  enum EN_PUBLISH_TYPES event_type;

  if((j_data == NULL)){
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired cb_resource_handler_pjsip_mod.");

  if(type == EN_RESOURCE_RELOAD) {
    event_type = EN_PUBLISH_CREATE;

    j_event = json_pack("{s:s, s[{s:s}, {s:s}]}",
        "type",       "reload",
        "modules",
          "name", "trunk",
          "name", "user"
        );
  }
  else {
    // something was wrong
    slog(LOG_ERR, "Unsupported resource update type. type[%d]", type);
    return false;
  }

  // create topic
  asprintf(&topic, "%s", DEF_PUBLISH_TOPIC_PREFIX_MANAGER);

  // publish event
  ret = publication_publish_event(topic, DEF_PUB_EVENT_PREFIX_MANAGER_NOTICE, event_type, j_event);
  sfree(topic);
  json_decref(j_event);
  if(ret == false) {
    slog(LOG_ERR, "Could not publish event.");
    return false;
  }

  return true;
}

static json_t* get_trunk_info(const char* name)
{
  json_t* j_tmp;
  json_t* j_res;

  if(name == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_res = json_object();
  json_object_set_new(j_res, "name", json_string(name));

  // get cfg registration
  j_tmp = pjsip_cfg_get_registration_info_data(name);
  if(j_tmp  == NULL) {
    slog(LOG_NOTICE, "No registration info. name[%s]", name);
    json_decref(j_res);
    return NULL;
  }
  json_object_set(j_res, "server_uri", json_object_get(j_tmp, "server_uri"));
  json_object_set(j_res, "client_uri", json_object_get(j_tmp, "client_uri"));
  json_decref(j_tmp);

  // get cfg auth
  j_tmp = pjsip_cfg_get_auth_info_data(name);
  json_object_set(j_res, "username", json_object_get(j_tmp, "username"));
  json_object_set(j_res, "password", json_object_get(j_tmp, "password"));
  json_decref(j_tmp);

  // get cfg aor
  j_tmp = pjsip_cfg_get_aor_info_data(name);
  json_object_set(j_res, "contact", json_object_get(j_tmp, "contact"));
  json_decref(j_tmp);

  // get cfg endpoint
  j_tmp = pjsip_cfg_get_endpoint_info_data(name);
  json_object_set(j_res, "context", json_object_get(j_tmp, "context"));
  json_decref(j_tmp);

  // get cfg identify
  j_tmp = pjsip_cfg_get_identify_info_data(name);
  json_object_set_new(j_res, "hostname", json_object_get(j_tmp, "match")? json_incref(json_object_get(j_tmp, "match")) : json_string(""));
  json_decref(j_tmp);

  // get registration_outbound
  j_tmp = pjsip_get_registration_outbound_info(name);
  if(j_tmp != NULL) {
    json_object_set(j_res, "status", json_object_get(j_tmp, "status"));
  }
  else {
    json_object_set_new(j_res, "status", json_string("Unregistered"));
  }
  json_decref(j_tmp);

  return j_res;
}

static json_t* get_trunks_all(void)
{
  json_t* j_res;
  json_t* j_tmp;
  json_t* j_regs;
  json_t* j_reg;
  const char* name;
  int idx;

  j_regs = pjsip_cfg_get_registrations_all();
  if(j_regs == NULL) {
    slog(LOG_ERR, "Could not get pjsip cfg registrations info.");
    return NULL;
  }

  j_res = json_array();
  json_array_foreach(j_regs, idx, j_reg) {
    name = json_string_value(json_object_get(j_reg, "name"));
    if(name == NULL) {
      continue;
    }

    j_tmp = get_trunk_info(name);
    if(j_tmp == NULL) {
      continue;
    }

    json_array_append_new(j_res, j_tmp);
  }
  json_decref(j_regs);

  return j_res;
}

/**
 * Create trunk info with default setting.
 * @param j_data
 * @return
 */
static bool create_trunk_info(const json_t* j_data)
{
  int ret;
  const char* name;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get name
  name = json_string_value(json_object_get(j_data, "name"));
  if(name == NULL) {
    slog(LOG_NOTICE, "Could not get name info.");
    return false;
  }

  // check exist
  ret = is_exist_trunk(name);
  if(ret == true) {
    slog(LOG_NOTICE, "The given trunk name is already exist. name[%s]", name);
    return false;
  }

  // create registration
  ret = pjsip_cfg_create_registration_with_default_info(
      name,
      json_string_value(json_object_get(j_data, "server_uri"))? : "",
      json_string_value(json_object_get(j_data, "client_uri"))? : ""
      );
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create registration info.");
    delete_trunk_info(name);
    return false;
  }

  // create auth
  ret = pjsip_cfg_create_auth_info(
      name,
      json_string_value(json_object_get(j_data, "username"))? : "",
      json_string_value(json_object_get(j_data, "password"))? : ""
      );
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create auth info.");
    delete_trunk_info(name);
    return false;
  }

  // create aor
  ret = pjsip_cfg_create_aor_info(
      name,
      json_string_value(json_object_get(j_data, "contact"))? : ""
      );
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create aor info.");
    delete_trunk_info(name);
    return false;
  }

  // create endpoint
  ret = pjsip_cfg_create_endpoint_with_default_info(
      name,
      json_string_value(json_object_get(j_data, "context"))? : ""
      );
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create endpoint info.");
    delete_trunk_info(name);
    return false;
  }

  // create identify
  ret = pjsip_cfg_create_identify_info(
      name,
      json_string_value(json_object_get(j_data, "hostname"))? : ""
      );
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create identify info.");
    delete_trunk_info(name);
    return false;
  }

  // reload pjsip config
  ret = pjsip_reload_config();
  if(ret == false) {
    slog(LOG_WARNING, "Could not reload pjsip_handler.");
    return false;
  }

  return true;
}

static bool delete_trunk_info(const char* name)
{
  int ret;

  if(name == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_trunk_info");

  pjsip_cfg_delete_aor_info(name);
  pjsip_cfg_delete_auth_info(name);
  pjsip_cfg_delete_contact_info(name);
  pjsip_cfg_delete_endpoint_info(name);
  pjsip_cfg_delete_identify_info(name);
  pjsip_cfg_delete_registration_info(name);

  ret = pjsip_reload_config();
  if(ret == false) {
    slog(LOG_NOTICE, "Could not reload pjsip config.");
    return false;
  }

  return true;
}

static bool update_trunk_info(const char* name, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  json_t* j_cfg;

  if((name == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired update_trunk_info.");


  // check exist
  ret = is_exist_trunk(name);
  if(ret == false) {
    slog(LOG_NOTICE, "The given trunk name is not exist. name[%s]", name);
    return false;
  }

  // update registration
  j_cfg = pjsip_cfg_get_registration_info(name);
  j_tmp = json_object_get(j_cfg, "data");
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get cfg registration info. name[%s]", name);
    return false;
  }
  json_object_set_new(j_tmp, "server_uri",
      json_object_get(j_data, "server_uri")? json_incref(json_object_get(j_data, "server_uri")) : json_string("")
      );
  json_object_set_new(j_tmp, "client_uri",
      json_object_get(j_data, "client_uri")? json_incref(json_object_get(j_data, "client_uri")) : json_string("")
      );
  ret = pjsip_cfg_update_registration_info(j_cfg);
  json_decref(j_cfg);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update registration info.");
    return false;
  }

  // update auth
  j_tmp = pjsip_cfg_get_auth_info_data(name);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get cfg auth info. name[%s]", name);
    return false;
  }
  json_object_set_new(j_tmp, "username",
      json_object_get(j_data, "username")? json_incref(json_object_get(j_data, "username")) : json_string("")
      );
  json_object_set_new(j_tmp, "password",
      json_object_get(j_data, "password")? json_incref(json_object_get(j_data, "password")) : json_string("")
      );
  ret = pjsip_cfg_update_auth_info(name, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update registration info.");
    return false;
  }

  // update aor
  j_tmp = pjsip_cfg_get_aor_info_data(name);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get cfg aor info. name[%s]", name);
    return false;
  }
  json_object_set_new(j_tmp, "contact",
      json_object_get(j_data, "contact")? json_incref(json_object_get(j_data, "contact")) : json_string("")
      );
  ret = pjsip_cfg_update_aor_info_data(name, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update aor info.");
    return false;
  }

  // update endpoint
  j_tmp = pjsip_cfg_get_endpoint_info_data(name);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get cfg endpoint info. name[%s]", name);
    return false;
  }
  json_object_set_new(j_tmp, "context",
      json_object_get(j_data, "context")? json_incref(json_object_get(j_data, "context")) : json_string("")
      );
  ret = pjsip_cfg_update_endpoint_info_data(name, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update endpoint info.");
    return false;
  }

  // update identify
  j_tmp = pjsip_cfg_get_identify_info_data(name);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get cfg identify info. name[%s]", name);
    return false;
  }
  json_object_set_new(j_tmp, "match",
      json_object_get(j_data, "hostname")? json_incref(json_object_get(j_data, "hostname")) : json_string("")
      );
  ret = pjsip_cfg_update_identify_info_data(name, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update identify info.");
    return false;
  }

  // reload pjsip config
  ret = pjsip_reload_config();
  if(ret == false) {
    slog(LOG_WARNING, "Could not reload pjsip_handler.");
    return false;
  }

  return true;
}

/**
 * Return true, if the given name is exist in any configuration files.
 * @param name
 * @return
 */
static bool is_exist_trunk(const char* name)
{
  json_t* j_tmp;

  j_tmp = pjsip_cfg_get_aor_info_data(name);
  if(j_tmp != NULL) {
    json_decref(j_tmp);
    return true;
  }

  j_tmp = pjsip_cfg_get_auth_info_data(name);
  if(j_tmp != NULL) {
    json_decref(j_tmp);
    return true;
  }

  j_tmp = pjsip_cfg_get_contact_info_data(name);
  if(j_tmp != NULL) {
    json_decref(j_tmp);
    return true;
  }

  j_tmp = pjsip_cfg_get_endpoint_info_data(name);
  if(j_tmp != NULL) {
    json_decref(j_tmp);
    return true;
  }

  j_tmp = pjsip_cfg_get_identify_info_data(name);
  if(j_tmp != NULL) {
    json_decref(j_tmp);
    return true;
  }

  j_tmp = pjsip_cfg_get_registration_info_data(name);
  if(j_tmp != NULL) {
    json_decref(j_tmp);
    return true;
  }

  j_tmp = pjsip_cfg_get_transport_info(name);
  if(j_tmp != NULL) {
    json_decref(j_tmp);
    return true;
  }

  return false;
}

static json_t* get_sdialplans_all(void)
{
  json_t* j_res;

  j_res = dialplan_get_sdialplans_all();
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

static json_t* get_sdialplan_info(const char* name)
{
  json_t* j_res;

  if(name == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired get_sdialplan_info. name[%s]", name);

  j_res = dialplan_get_sdialplan_info(name);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

static bool create_sdialplan_info(const json_t* j_data)
{
  int ret;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  ret = dialplan_create_sdialplan_info(j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create sdialplan info.");
    return false;
  }

  ret = dialplan_reload_asterisk();
  if(ret == false) {
    slog(LOG_NOTICE, "Could not reload dialplan asterisk module.");
    return false;
  }

  return true;
}

/**
 * Update the given sdialplan info.
 * @param name
 * @param j_data
 * @return
 */
static bool update_sdialplan_info(const char* name, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;

  if((name == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired update_sdialplan_info. name[%s]", name);

  j_tmp = json_deep_copy(j_data);
  json_object_set_new(j_tmp, "name", json_string(name));

  ret = dialplan_update_sdialplan_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not update sdialplan info. name[%s]", name);
    return false;
  }

  ret = dialplan_reload_asterisk();
  if(ret == false) {
    slog(LOG_NOTICE, "Could not reload dialplan asterisk module.");
    return false;
  }

  return true;
}

/**
 * Delete the given sdialplan info.
 * @param name
 * @param j_data
 * @return
 */
static bool delete_sdialplan_info(const char* name)
{
  int ret;

  if(name == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_sdialplan_info. name[%s]", name);

  ret = dialplan_delete_sdialplan_info(name);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not delete sdialplan info. name[%s]", name);
    return false;
  }

  ret = dialplan_reload_asterisk();
  if(ret == false) {
    slog(LOG_NOTICE, "Could not reload dialplan asterisk module.");
    return false;
  }

  return true;
}


