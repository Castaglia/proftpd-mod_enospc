/*
 * ProFTPD: mod_enospc -- a module for simulating ENOSPC issues
 * Copyright (c) 2008-2022 TJ Saunders
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 *
 * As a special exemption, TJ Saunders and other respective copyright holders
 * give permission to link this program with OpenSSL, and distribute the
 * resulting executable, without including the source code for OpenSSL in the
 * source distribution.
 *
 * This is mod_enospc, contrib software for proftpd 1.3.x and above.
 * For more information contact TJ Saunders <tj@castaglia.org>.
 */

#include "conf.h"
#include "privs.h"

#define MOD_ENOSPC_VERSION		"mod_enospc/0.3"

/* Make sure the version of proftpd is as necessary. */
#if PROFTPD_VERSION_NUMBER < 0x0001030001
# error "ProFTPD 1.3.0rc1 or later required"
#endif

module enospc_module;

static off_t enospc_threshold = 0;
static off_t enospc_written = 0;

/* FSIO handlers
 */

static int enospc_close(pr_fh_t *fh, int fd) {
  if (enospc_written > enospc_threshold) {
    pr_log_debug(DEBUG0, MOD_ENOSPC_VERSION ": closing '%s' at size %" PR_LU
      ", returning ENOSPC", fh->fh_path, (pr_off_t) enospc_written);
    errno = ENOSPC;
    return -1;
  }

  enospc_written = 0;
  return close(fd);
}

static int enospc_write(pr_fh_t *fh, int fd, const char *buf, size_t size) {
  enospc_written += size;

  if (enospc_threshold > 0 &&
      enospc_written > enospc_threshold) {
    pr_log_debug(DEBUG0, MOD_ENOSPC_VERSION ": writing '%s' at size %" PR_LU
      ", returning ENOSPC", fh->fh_path, (pr_off_t) enospc_written);
    errno = ENOSPC;
    return -1;
  }

  return size;
}

/* Configuration handlers
 */

/* usage: NoSpaceEngine on|off */
MODRET set_enospcengine(cmd_rec *cmd) {
  int engine = -1;
  config_rec *c;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT);

  engine = get_boolean(cmd, 1);
  if (engine == -1) {
    CONF_ERROR(cmd, "expected Boolean parameter");
  }

  c = add_config_param(cmd->argv[0], 1, NULL);
  c->argv[0] = pcalloc(c->pool, sizeof(int));
  *((int *) c->argv[0]) = engine;

  return PR_HANDLED(cmd);
}

/* usage: NoSpaceThreshold size */
MODRET set_enospcthreshold(cmd_rec *cmd) {
  char *tmp = NULL;
  off_t threshold;
  config_rec *c;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT);

#ifdef HAVE_STRTOULL
  threshold = strtoull(cmd->argv[1], &tmp, 10);
#else
  threshold = strtoul(cmd->argv[1], &tmp, 10);
#endif /* HAVE_STRTOULL */

  if (tmp &&
      *tmp) {
    CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "'", cmd->argv[1],
      "' is not a valid threshold", NULL));
  }

  c = add_config_param(cmd->argv[0], 1, NULL);
  c->argv[0] = pcalloc(c->pool, sizeof(off_t));
  *((off_t *) c->argv[0]) = threshold;

  return PR_HANDLED(cmd);
}

/* Event handlers
 */

#if defined(PR_SHARED_MODULE)
static void enospc_mod_unload_ev(const void *event_data, void *user_data) {
  if (strcmp("mod_enospc.c", (const char *) event_data) != 0) {
    return;
  }

  (void) pr_unmount_fs("/", "enospc");
  pr_event_unregister(&enospc_module, NULL, NULL);
}
#endif /* PR_SHARED_MODULE */

static void enospc_postparse_ev(const void *event_data, void *user_data) {
  config_rec *c;
  pr_fs_t *fs;
  int engine = FALSE;

  c = find_config(main_server->conf, CONF_PARAM, "NoSpaceEngine", FALSE);
  if (c != NULL) {
    engine = *((int *) c->argv[0]);
  }

  if (engine == FALSE) {
    return;
  }

  c = find_config(main_server->conf, CONF_PARAM, "NoSpaceThreshold", FALSE);
  if (c != NULL) {
    enospc_threshold = *((off_t *) c->argv[0]);
  }

  /* Register our custom filesystem. */
  fs = pr_register_fs(permanent_pool, "enospc", "/");
  if (fs == NULL) {
    return;
  }

  /* Add our custom FSIO handlers. */
  fs->close = enospc_close;
  fs->write = enospc_write;

  return;
}

/* Initialization functions
 */

static int enospc_init(void) {
#if defined(PR_SHARED_MODULE)
  pr_event_register(&enospc_module, "core.module-unload", enospc_mod_unload_ev,
    NULL);
#endif /* PR_SHARED_MODULE */

  pr_event_register(&enospc_module, "core.postparse", enospc_postparse_ev,
    NULL);

  return 0;
}

/* Module API tables
 */

static conftable enospc_conftab[] = {
  { "NoSpaceEngine",		set_enospcengine,	NULL },
  { "NoSpaceThreshold",		set_enospcthreshold,	NULL },
  { NULL }
};

module enospc_module = {
  NULL, NULL,

  /* Module API version 2.0 */
  0x20,

  /* Module name */
  "enospc",

  /* Module configuration handler table */
  enospc_conftab,

  /* Module command handler table */
  NULL,

  /* Module authentication handler table */
  NULL,

  /* Module initialization function */
  enospc_init,

  /* Session initialization function */
  NULL,

  /* Module version */
  MOD_ENOSPC_VERSION
};
