/**
 * #FujiNet ISS tracker for C64
 *
 * @author  Thomas Cherryhomes
 * @email   thom dot cherryhomes at gmail dot com
 * @license gpl v. 3
 *
 * @brief main()
 */

#include <c64.h>
#include <tgi.h>
#include <stdbool.h>
#include <stdio.h>
#include "fetch.h"

char prompt[255], response[1024];

void main(void)
{
  tgi_install(c64_hi_tgi);
  tgi_init();
  tgi_clear();
  
  fetch(&prompt, &response);
}