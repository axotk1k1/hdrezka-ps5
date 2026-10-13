/*
 * PS5 Vulkan Template - the host reference build's side of console_curl.h.
 *
 * On the PC the system's libcurl needs none of what console_curl.c gives
 * PacBrew's on the console; the two calls the title makes do nothing here.
 *
 * SPDX-License-Identifier: MIT
 */
#include <curl/curl.h>

void console_curl_setup(CURL *easy)
{
    (void)easy;
}

const char *console_curl_ca_file(void)
{
    return "";
}
