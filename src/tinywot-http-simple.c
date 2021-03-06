/**
 * \internal
 * \file api.c
 * \brief Public API implementations.
 *
 * \copyright
 * SPDX-FileCopyrightText: 2021 Junde Yhi <junde@yhi.moe>
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinywot.h>

#include "tinywot-http-simple.h"

#if defined(__AVR_ARCH__) && defined(TINYWOT_HTTP_SIMPLE_USE_PROGMEM)
#include <avr/pgmspace.h>
#define _PROGMEM PROGMEM
#define _PSTR PSTR
#define _strlen strlen_P
#define _strncmp strncmp_P
#define _strspn strspn_P
#define _snprintf snprintf_P
#else
#define _PROGMEM
#define _PSTR
#define _strlen strlen
#define _strncmp strncmp
#define _strspn strspn
#define _snprintf snprintf
#endif

//////////////////// Private Data ////////////////////

#ifdef TINYWOT_HTTP_SIMPLE_USE_REASON_PHRASE
#define HTTP_REASON_PHRASE_OK "OK"
#define HTTP_REASON_PHRASE_NO_CONTENT "No Content"
#define HTTP_REASON_PHRASE_BAD_REQUEST "Bad Request"
#define HTTP_REASON_PHRASE_NOT_FOUND "Not Found"
#define HTTP_REASON_PHRASE_METHOD_NOT_ALLOWED "Method Not Allowed"
#define HTTP_REASON_PHRASE_INTERNAL_SERVER_ERROR "Internal Server Error"
#define HTTP_REASON_PHRASE_NOT_IMPLEMENTED "Not Implemented"
#else
#define HTTP_REASON_PHRASE_OK ""
#define HTTP_REASON_PHRASE_NO_CONTENT ""
#define HTTP_REASON_PHRASE_BAD_REQUEST ""
#define HTTP_REASON_PHRASE_NOT_FOUND ""
#define HTTP_REASON_PHRASE_METHOD_NOT_ALLOWED ""
#define HTTP_REASON_PHRASE_INTERNAL_SERVER_ERROR ""
#define HTTP_REASON_PHRASE_NOT_IMPLEMENTED ""
#endif

static const char str_crlf[] _PROGMEM = "\r\n";

static const char str_get[] _PROGMEM = "GET";
static const char str_put[] _PROGMEM = "PUT";
static const char str_post[] _PROGMEM = "POST";
static const char str_options[] _PROGMEM = "OPTIONS";

static const char str_ok[] _PROGMEM =
  "HTTP/1.1 200 " HTTP_REASON_PHRASE_OK "\r\n";
static const char str_no_content[] _PROGMEM =
  "HTTP/1.1 204 " HTTP_REASON_PHRASE_NO_CONTENT "\r\n";
static const char str_bad_request[] _PROGMEM =
  "HTTP/1.1 400 " HTTP_REASON_PHRASE_BAD_REQUEST "\r\n";
static const char str_not_found[] _PROGMEM =
  "HTTP/1.1 404 " HTTP_REASON_PHRASE_NOT_FOUND "\r\n";
static const char str_method_not_allowed[] _PROGMEM =
  "HTTP/1.1 405 " HTTP_REASON_PHRASE_METHOD_NOT_ALLOWED "\r\n";
static const char str_internal_server_error[] _PROGMEM =
  "HTTP/1.1 500 " HTTP_REASON_PHRASE_INTERNAL_SERVER_ERROR "\r\n";
static const char str_not_implemented[] _PROGMEM =
  "HTTP/1.1 501 " HTTP_REASON_PHRASE_NOT_IMPLEMENTED "\r\n";

static const char str_allow[] _PROGMEM = "Allow: ";
static const char str_content_type[] _PROGMEM = "Content-Type: ";
static const char str_content_length[] _PROGMEM = "Content-Length: ";
static const char str_allow_methods[] _PROGMEM =
  "Access-Control-Allow-Methods: ";
static const char str_allow_origin[] _PROGMEM =
  "Access-Control-Allow-Origin: *\r\n";
static const char str_allow_headers[] _PROGMEM =
  "Access-Control-Allow-Headers: Content-Type\r\n";
static const char str_conn_close[] _PROGMEM = "Connection: close\r\n";
static const char str_server[] _PROGMEM =
  "Server: TinyWoT-HTTP-Simple/" TINYWOT_HTTP_SIMPLE_VERSION
  " (TinyWoT/" TINYWOT_VERSION ")\r\n";

static const char str_text_plain[] _PROGMEM = "text/plain\r\n";
static const char str_application_octet_stream[] _PROGMEM =
  "application/octet-stream\r\n";
static const char str_application_json[] _PROGMEM = "application/json\r\n";
static const char str_application_td_json[] _PROGMEM =
  "application/td+json\r\n";

//////////////////// Private APIs ////////////////////

/**
 * \internal
 * \brief Return 0 if `stmt` is 0.
 */
#define RETURN_IF_FAIL(stmt) \
  { \
    int r = (stmt); \
    if (!r) \
      return 0; \
  }

/**
 * \internal
 * \brief Test up to `count` bytes if two strings are case-insensitively equal.
 *
 * Note that when `TINYWOT_HTTP_SIMPLE_USE_PROGMEM` is defined, this function
 * automatically uses AVR program space functions, in the case of which `s2`
 * must be a pointer to the program space.
 *
 * \param[in] s1 A string.
 * \param[in] s2 Another string. When `TINYWOT_HTTP_SIMPLE_USE_PROGMEM` is
 * defined, this must be a string pointing to the flash.
 * \param[in] count Up to how many bytes to compare.
 * \return non-zero if `s1[:count] == s2[:count]`, otherwise 0.
 */
static int _strinequ(const char *s1, const char *s2, size_t count) {
  for (;; ++s1, ++s2, --count) {
#if defined(__AVR_ARCH__) && defined(TINYWOT_HTTP_SIMPLE_USE_PROGMEM)
    char c2 = pgm_read_byte(s2);
#else
    char c2 = *s2;
#endif

    if (!count || (!*s1 && !c2)) {
      return 1;
    }

    if (tolower(*s1) != tolower(c2)) {
      return 0;
    }
  }

  return 0;
}

/**
 * \internal
 * \brief Call `config->write` to write `str` out, taking care of AVR program
 * space (flash memory) strings.
 *
 * \param[inout] config A TinyWoTHTTPSimpleConfig.
 * \param[in] str A string to write out. When `TINYWOT_HTTP_SIMPLE_USE_PROGMEM`
 * is defined, this must point to the flash memory.
 * \param[in] size The size of `str`. Note that this is not the length of `str`:
 * it includes the terminating NUL.
 * \return non-0 on success, 0 on failure.
 */
static int _write(TinyWoTHTTPSimpleConfig *config, const char *str,
                  size_t size) {
  int r = 0;

#if defined(__AVR_ARCH__) && defined(TINYWOT_HTTP_SIMPLE_USE_PROGMEM)
  size_t maxsize = config->linebuf_size < size ? config->linebuf_size : size;
  memcpy_P(config->linebuf, str, maxsize);
  r = config->write(config->linebuf, maxsize, config->ctx);
#else
  r = config->write(str, size, config->ctx);
#endif

  if (!r) {
    return 0;
  }

  return 1;
}

/**
 * \brief Send a list of allowed methods based on `response->allow`.
 *
 * \param[in] config Configuration.
 * \param[inout] response The TinyWoT response representation.
 * \return non-0 on success, 0 on failure.
 */
static int _send_allowed_method_list(TinyWoTHTTPSimpleConfig *config,
                                     TinyWoTResponse *response) {
  bool comma = false;

  if (response->allow & WOT_OPERATION_TYPE_READ_PROPERTY) {
    comma = true;
    RETURN_IF_FAIL(_write(config, str_get, _strlen(str_get)));
  }

  if (response->allow & WOT_OPERATION_TYPE_WRITE_PROPERTY) {
    if (comma) {
      RETURN_IF_FAIL(_write(config, _PSTR(", "), 2));
    } else {
      comma = true;
    }
    RETURN_IF_FAIL(_write(config, str_put, _strlen(str_put)));
  }

  if (response->allow & WOT_OPERATION_TYPE_INVOKE_ACTION) {
    if (comma) {
      RETURN_IF_FAIL(_write(config, _PSTR(", "), 2));
    } else {
      comma = true;
    }
    RETURN_IF_FAIL(_write(config, str_post, _strlen(str_post)));
  }

  return 1;
}

/**
 * \internal
 * \brief Extract useful information from a HTTP request line.
 *
 * `linebuf` should be a string of HTTP request line. For example:
 *
 * ```
 * GET /test HTTP/1.1\r\n\0
 * ```
 *
 * This function matches on the three components and store them in `request`.
 * `pathbuf` is used to copy out the path component from `linebuf`.
 *
 * \param[in] linebuf Buffer storing the current HTTP line.
 * \param[out] pathbuf Buffer storing the path component.
 * \param[in] pathbuf_size Size of `pathbuf`.
 * \param[out] request TinyWoT request representation.
 * \return non-0 if all information has been successfully matched and stored
 * into `request`, otherwise 0.
 */
static int tinywot_http_simple_extract_request_line(const char *linebuf,
                                                    char *pathbuf,
                                                    size_t pathbuf_size,
                                                    TinyWoTRequest *request) {
  const char *cursor_start = linebuf;
  const char *cursor_end = NULL;
  size_t cursor_range = 0;

  // Method

  cursor_end = strchr(cursor_start, ' ');
  if (!cursor_end) {
    return 0;
  }
  cursor_range = cursor_end - cursor_start;

  if (_strncmp(cursor_start, str_get, cursor_range) == 0) {
    request->op = WOT_OPERATION_TYPE_READ_PROPERTY;
  } else if (_strncmp(cursor_start, str_put, cursor_range) == 0) {
    request->op = WOT_OPERATION_TYPE_WRITE_PROPERTY;
  } else if (_strncmp(cursor_start, str_post, cursor_range) == 0) {
    request->op = WOT_OPERATION_TYPE_INVOKE_ACTION;
  } else if (_strncmp(cursor_start, str_options, cursor_range) == 0) {
    request->op = TINYWOT_OPERATION_TYPE_OPTIONS;
  } else {
    return 0;
  }

  cursor_start = cursor_end + 1;

  // Path

  cursor_end = strchr(cursor_start, ' ');
  if (!cursor_end) {
    return 0;
  }
  cursor_range = cursor_end - cursor_start;

  if (pathbuf_size < cursor_range + 1) {
    return 0;
  }
  strncpy(pathbuf, cursor_start, cursor_range);

  cursor_start = cursor_end + 1;

  // Version (but only an assertion on the format)

  if (_strncmp(cursor_start, _PSTR("HTTP/1.1"), 8) != 0)
    return 0;
  cursor_end = strchr(cursor_start, '\r');
  if (!cursor_end)
    return 0;
  if (*(cursor_end + 1) != '\n')
    return 0;

  return 1;
}

/**
 * \internal
 * \brief Extract useful information from a line of HTTP header field.
 *
 * `linebuf` should be a string of HTTP line of header. For example:
 *
 * ```
 * Content-Type: application/json\r\n\0
 * ```
 *
 * Header key is matched case-insensitively. On supported header key, this
 * function writes information to `request`. Currently supported header fields
 * include:
 *
 * - `content-type` => `request->content_type`
 * - `content-length` => `request->content_length`
 *
 * \param[in] linebuf Line buffer storing the current HTTP header field.
 * \param[out] request A TinyWoT request representation.
 * \return
 * - 1 if we don't care about the current header. The caller should re-fill
 *   lienbuf with the next HTTP header field and call this function again.
 * - 0 if `linebuf` only contains CR LF (`\r\n`), indicating the end of HTTP
 *   header field. The caller should stop re-filling `linebuf`.
 * - -1 if `linebuf` is a malformed HTTP header field, which is an error.
 */
static int tinywot_http_simple_extract_header_field(const char *linebuf,
                                                    TinyWoTRequest *request) {
  const char *key_start = NULL;
  const char *key_end = NULL;
  const char *value_start = NULL;
  const char *value_end = NULL;
  size_t key_length = 0;
  size_t value_length = 0;

  // If the current line consists (starts with, even) only CR and LF then we
  // indicate the over of HTTP header fields.
  if (_strncmp(linebuf, str_crlf, _strlen(str_crlf)) == 0) {
    return 0;
  }

  // Locate key
  key_start = linebuf;
  key_end = strchr(key_start, ':');
  if (!key_end) {
    return -1;
  }
  key_length = key_end - key_start;

  // Locate value
  value_start = key_end + 1;
  value_end = strchr(value_start, '\r');
  if (!value_end || *(value_end + 1) != '\n') {
    return -1;
  }

  // Trim any optional whitespace around the value
  value_start += _strspn(value_start, _PSTR(" \t"));
  while (*(value_end - 1) == ' ' || *(value_end - 1) == '\t') {
    value_end -= 1;
  }
  value_length = value_end - value_start;

  if (_strinequ(key_start, str_content_type, key_length)) {
    if (_strinequ(value_start, str_text_plain, value_length)) {
      request->content_type = TINYWOT_CONTENT_TYPE_TEXT_PLAIN;
    } else if (_strinequ(value_start, str_application_octet_stream,
                         value_length)) {
      request->content_type = TINYWOT_CONTENT_TYPE_OCTET_STREAM;
    } else if (_strinequ(value_start, str_application_json, value_length)) {
      request->content_type = TINYWOT_CONTENT_TYPE_JSON;
    } else if (_strinequ(value_start, str_application_td_json, value_length)) {
      request->content_type = TINYWOT_CONTENT_TYPE_TD_JSON;
    } else {
      request->content_type = TINYWOT_CONTENT_TYPE_UNKNOWN;
    }
  } else if (_strinequ(key_start, str_content_length, key_length)) {
    unsigned long val = strtoul(value_start, NULL, 10);
    if (errno == ERANGE) {
      return -1;
    }
    request->content_length = (size_t)val;
  }

  // Now we don't care about this header field -- indicate a continuance
  return 1;
}

//////////////////// Public APIs ////////////////////

int tinywot_http_simple_recv(TinyWoTHTTPSimpleConfig *config,
                             TinyWoTRequest *request) {
  int r = 0;

  // Process HTTP request line
  r = config->readln(config->linebuf, config->linebuf_size, config->ctx);
  if (r != 1) {
    return 0;
  }
  r = tinywot_http_simple_extract_request_line(config->linebuf, config->pathbuf,
                                               config->pathbuf_size, request);
  if (!r) {
    return 0;
  }
  request->path = config->pathbuf;

  // Process HTTP header fields
  for (;;) {
    r = config->readln(config->linebuf, config->linebuf_size, config->ctx);
    if (r != 1) {
      return 0;
    }
    r = tinywot_http_simple_extract_header_field(config->linebuf, request);
    if (r < 0) {
      return 0;
    }
    if (r == 0) {
      break;
    }
    // Implied
    // if (r > 0) {
    //   continue;
    // }
  }

  // Additionally load up to linebuf_size bytes of content
  r = config->readln(config->linebuf, config->linebuf_size, config->ctx);
  if (r == -2) {
    return 0;
  }
  request->content = config->linebuf;

  return 1;
}

int tinywot_http_simple_send(TinyWoTHTTPSimpleConfig *config,
                             TinyWoTResponse *response) {
  // HTTP status line
  switch (response->status) {
    case TINYWOT_RESPONSE_STATUS_OK:
      if (response->content) {
        RETURN_IF_FAIL(_write(config, str_ok, _strlen(str_ok)));
      } else {
        RETURN_IF_FAIL(_write(config, str_no_content, _strlen(str_no_content)));
      }
      break;
    case TINYWOT_RESPONSE_STATUS_BAD_REQUEST:
      RETURN_IF_FAIL(_write(config, str_bad_request, _strlen(str_bad_request)));
      break;
    case TINYWOT_RESPONSE_STATUS_UNSUPPORTED:
      RETURN_IF_FAIL(_write(config, str_not_found, _strlen(str_not_found)));
      break;
    case TINYWOT_RESPONSE_STATUS_METHOD_NOT_ALLOWED:
      RETURN_IF_FAIL(_write(config, str_method_not_allowed,
                            _strlen(str_method_not_allowed)));
      break;
    case TINYWOT_RESPONSE_STATUS_NOT_IMPLEMENTED:
      RETURN_IF_FAIL(
        _write(config, str_not_implemented, _strlen(str_not_implemented)));
      break;
    case TINYWOT_RESPONSE_STATUS_ERROR:   // fall through
    case TINYWOT_RESPONSE_STATUS_UNKNOWN: // fall through
    default:
      RETURN_IF_FAIL(_write(config, str_internal_server_error,
                            _strlen(str_internal_server_error)));
      break;
  }

  // CORS
  RETURN_IF_FAIL(_write(config, str_allow_origin, _strlen(str_allow_origin)));
  RETURN_IF_FAIL(_write(config, str_allow_headers, _strlen(str_allow_headers)));
  if (response->allow) {
    RETURN_IF_FAIL(
      _write(config, str_allow_methods, _strlen(str_allow_methods)));
    RETURN_IF_FAIL(_send_allowed_method_list(config, response));
    RETURN_IF_FAIL(_write(config, str_crlf, _strlen(str_crlf)));
  }

  // Allow (on 405)
  if (response->status == TINYWOT_RESPONSE_STATUS_METHOD_NOT_ALLOWED) {
    RETURN_IF_FAIL(_write(config, str_allow, _strlen(str_allow)));
    RETURN_IF_FAIL(_send_allowed_method_list(config, response));
    RETURN_IF_FAIL(_write(config, str_crlf, _strlen(str_crlf)));
  }

  // Connection: close (can keep-alive be implemented?)
  RETURN_IF_FAIL(_write(config, str_conn_close, _strlen(str_conn_close)));

  // Server versioning info
  RETURN_IF_FAIL(_write(config, str_server, _strlen(str_server)));

  // If there is actually no content payload, then we stop here
  if (!response->content) {
    RETURN_IF_FAIL(_write(config, str_crlf, _strlen(str_crlf)));
    return 1;
  }

  // Content-Type
  RETURN_IF_FAIL(_write(config, str_content_type, _strlen(str_content_type)));

  switch (response->content_type) {
    case TINYWOT_CONTENT_TYPE_OCTET_STREAM:
      RETURN_IF_FAIL(_write(config, str_application_octet_stream,
                            _strlen(str_application_octet_stream)));
      break;
    case TINYWOT_CONTENT_TYPE_JSON:
      RETURN_IF_FAIL(
        _write(config, str_application_json, _strlen(str_application_json)));
      break;
    case TINYWOT_CONTENT_TYPE_TD_JSON:
      RETURN_IF_FAIL(_write(config, str_application_td_json,
                            _strlen(str_application_td_json)));
      break;
    case TINYWOT_CONTENT_TYPE_TEXT_PLAIN: // fall through
    case TINYWOT_CONTENT_TYPE_UNKNOWN:    // fall through
    default:
      RETURN_IF_FAIL(_write(config, str_text_plain, _strlen(str_text_plain)));
      break;
  }

  // Content-Length
  int nbytes = _snprintf(config->linebuf, config->linebuf_size, _PSTR("%u"),
                         response->content_length);
  RETURN_IF_FAIL(
    _write(config, str_content_length, _strlen(str_content_length)));
  RETURN_IF_FAIL(config->write(config->linebuf, (size_t)nbytes, config->ctx));
  RETURN_IF_FAIL(_write(config, str_crlf, _strlen(str_crlf)));

  // End of header
  RETURN_IF_FAIL(_write(config, str_crlf, _strlen(str_crlf)));

  // Content payload
  RETURN_IF_FAIL(_write(config, response->content, response->content_length));

  return 1;
}
