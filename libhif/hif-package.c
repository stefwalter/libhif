/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */
/**
 * SECTION:hif-goal
 * @short_description: Helper methods for dealing with hawkey packages.
 * @include: libhif.h
 * @stability: Unstable
 *
 * These methods make it easier to get and set extra data on a package.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <hawkey/reldep.h>
#include <hawkey/util.h>
#include <librepo/librepo.h>

#include "hif-cleanup.h"
#include "libhif.h"
#include "hif-utils.h"

typedef struct {
	char		*checksum_str;
	char		*nevra;
	gboolean	 user_action;
	gchar		*filename;
	gchar		*origin;
	gchar		*description;
	gchar		*package_id;
	HifPackageInfo	 info;
	HifStateAction	 action;
	HifSource	*src;
} HifPackagePrivate;

/**
 * hif_package_destroy_func:
 **/
static void
hif_package_destroy_func (void *userdata)
{
	HifPackagePrivate *priv = (HifPackagePrivate *) userdata;
	g_free (priv->filename);
	g_free (priv->origin);
	g_free (priv->package_id);
	g_free (priv->description);
	hy_free (priv->checksum_str);
	hy_free (priv->nevra);
	g_slice_free (HifPackagePrivate, priv);
}

/**
 * hif_package_get_filename:
 * @pkg: a #HyPackage instance.
 *
 * Gets the package filename.
 *
 * Returns: absolute filename, or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
hif_package_get_filename (HyPackage pkg)
{
	HifPackagePrivate *priv;

	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return NULL;
	if (hy_package_installed (pkg))
		return NULL;

	/* default cache filename location */
	if (priv->filename == NULL && priv->src != NULL) {
		priv->filename = g_build_filename (hif_source_get_location (priv->src),
						   hy_package_get_location (pkg),
						   NULL);
		/* set the filename to cachedir for non-local sources */
		if (!hif_source_is_local (priv->src) ||
		    !g_file_test (priv->filename, G_FILE_TEST_EXISTS)) {
			_cleanup_free_ gchar *basename = NULL;
			basename = g_path_get_basename (hy_package_get_location (pkg));
			g_free (priv->filename);
			priv->filename = g_build_filename (hif_source_get_packages (priv->src),
							   basename,
							   NULL);
		}
	}

	return priv->filename;
}

/**
 * hif_package_get_origin:
 * @pkg: a #HyPackage instance.
 *
 * Gets the package origin.
 *
 * Returns: the package origin, or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
hif_package_get_origin (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return NULL;
	if (!hy_package_installed (pkg))
		return NULL;
	return priv->origin;
}

/**
 * hif_package_get_priv:
 **/
static HifPackagePrivate *
hif_package_get_priv (HyPackage pkg)
{
	HifPackagePrivate *priv;

	/* create private area */
	priv = hy_package_get_userdata (pkg);
	if (priv != NULL)
		return priv;

	priv = g_slice_new0 (HifPackagePrivate);
	hy_package_set_userdata (pkg, priv, hif_package_destroy_func);
	return priv;
}

/**
 * hif_package_get_pkgid:
 * @pkg: a #HyPackage instance.
 *
 * Gets the pkgid, which is the SHA hash of the package header.
 *
 * Returns: pkgid string, or NULL
 *
 * Since: 0.1.0
 **/
const gchar *
hif_package_get_pkgid (HyPackage pkg)
{
	const unsigned char *checksum;
	HifPackagePrivate *priv;
	int checksum_type;

	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return NULL;
	if (priv->checksum_str != NULL)
		goto out;

	/* calculate and cache */
	checksum = hy_package_get_hdr_chksum (pkg, &checksum_type);
	if (checksum == NULL)
		goto out;
	priv->checksum_str = hy_chksum_str (checksum, checksum_type);
out:
	return priv->checksum_str;
}

/**
 * hif_package_set_pkgid:
 * @pkg: a #HyPackage instance.
 * @pkgid: pkgid, e.g. "e6e3b2b10c1ef1033769147dbd1bf851c7de7699"
 *
 * Sets the package pkgid, which is the SHA hash of the package header.
 *
 * Since: 0.1.8
 **/
void
hif_package_set_pkgid (HyPackage pkg, const gchar *pkgid)
{
	HifPackagePrivate *priv;
	g_return_if_fail (pkgid != NULL);
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	hy_free (priv->checksum_str);
	priv->checksum_str = strdup (pkgid);
}

/**
 * hif_package_id_build:
 **/
static gchar *
hif_package_id_build (const gchar *name,
		      const gchar *version,
		      const gchar *arch,
		      const gchar *data)
{
	return g_strjoin (";", name,
			  version != NULL ? version : "",
			  arch != NULL ? arch : "",
			  data != NULL ? data : "",
			  NULL);
}

/**
 * hif_package_get_id:
 * @pkg: a #HyPackage instance.
 *
 * Gets the package-id as used by PackageKit.
 *
 * Returns: the package_id string, or %NULL, e.g. "hal;2:0.3.4;i386;installed:fedora"
 *
 * Since: 0.1.0
 **/
const gchar *
hif_package_get_id (HyPackage pkg)
{
	HifPackagePrivate *priv;
	const gchar *reponame;
	_cleanup_free_ gchar *reponame_tmp = NULL;

	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return NULL;
	if (priv->package_id != NULL)
		goto out;

	/* calculate and cache */
	reponame = hy_package_get_reponame (pkg);
	if (g_strcmp0 (reponame, HY_SYSTEM_REPO_NAME) == 0) {
		/* origin data to munge into the package_id data field */
		if (priv->origin != NULL) {
			reponame_tmp = g_strdup_printf ("installed:%s", priv->origin);
			reponame = reponame_tmp;
		} else {
			reponame = "installed";
		}
	} else if (g_strcmp0 (reponame, HY_CMDLINE_REPO_NAME) == 0) {
		reponame = "local";
	}
	priv->package_id = hif_package_id_build (hy_package_get_name (pkg),
						hy_package_get_evr (pkg),
						hy_package_get_arch (pkg),
						reponame);
out:
	return priv->package_id;
}

/**
 * hif_package_get_nevra:
 * @pkg: a #HyPackage instance.
 *
 * Gets the package NEVRA.
 *
 * Returns: the package NEVRA, e.g. "2:hal-0.3.4-5.i386"
 *
 * Since: 0.1.0
 **/
const gchar *
hif_package_get_nevra (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv->nevra == NULL)
		priv->nevra = hy_package_get_nevra (pkg);
	return priv->nevra;
}

/**
 * hif_package_get_description:
 * @pkg: a #HyPackage instance.
 *
 * Get the package description, with minor tweaks to remove extra whitespace.
 *
 * Returns: the package description
 *
 * Since: 0.1.0
 **/
const gchar *
hif_package_get_description (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv->description == NULL) {
		priv->description = g_strdup (hy_package_get_description (pkg));
		g_strdelimit (priv->description, "\n\t", ' ');
	}
	return priv->description;
}

/**
 * hif_package_get_cost:
 * @pkg: a #HyPackage instance.
 *
 * Returns the cost of the source that provided the package.
 *
 * Returns: the cost, where higher is more expensive, default 1000
 *
 * Since: 0.1.0
 **/
guint
hif_package_get_cost (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv->src == NULL) {
		g_warning ("no src for %s", hif_package_get_id (pkg));
		return G_MAXUINT;
	}
	return hif_source_get_cost (priv->src);
}

/**
 * hif_package_set_filename:
 * @pkg: a #HyPackage instance.
 * @filename: absolute filename.
 *
 * Sets the file on disk that matches the package source.
 *
 * Since: 0.1.0
 **/
void
hif_package_set_filename (HyPackage pkg, const gchar *filename)
{
	HifPackagePrivate *priv;

	/* replace contents */
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	g_free (priv->filename);
	priv->filename = g_strdup (filename);
}

/**
 * hif_package_set_origin:
 * @pkg: a #HyPackage instance.
 * @origin: origin, e.g. "fedora"
 *
 * Sets the package origin source.
 *
 * Since: 0.1.0
 **/
void
hif_package_set_origin (HyPackage pkg, const gchar *origin)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	g_free (priv->origin);
	priv->origin = g_strdup (origin);
}

/**
 * hif_package_set_source:
 * @pkg: a #HyPackage instance.
 * @src: a #HifSource.
 *
 * Sets the source the package was created from.
 *
 * Since: 0.1.0
 **/
void
hif_package_set_source (HyPackage pkg, HifSource *src)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	priv->src = src;
}

/**
 * hif_package_get_source:
 * @pkg: a #HyPackage instance.
 *
 * Gets the source the package was created from.
 *
 * Returns: a #HifSource or %NULL
 *
 * Since: 0.1.0
 **/
HifSource *
hif_package_get_source (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return NULL;
	return priv->src;
}

/**
 * hif_package_get_info:
 * @pkg: a #HyPackage instance.
 *
 * Gets the info enum assigned to the package.
 *
 * Returns: #HifPackageInfo value
 *
 * Since: 0.1.0
 **/
HifPackageInfo
hif_package_get_info (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return HIF_PACKAGE_INFO_UNKNOWN;
	return priv->info;
}

/**
 * hif_package_get_action:
 * @pkg: a #HyPackage instance.
 *
 * Gets the action assigned to the package, i.e. what is going to be performed.
 *
 * Returns: a #HifStateAction
 *
 * Since: 0.1.0
 */
HifStateAction
hif_package_get_action (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return HIF_STATE_ACTION_UNKNOWN;
	return priv->action;
}

/**
 * hif_package_set_info:
 * @pkg: a #HyPackage instance.
 * @info: the info flags.
 *
 * Sets the info flags for the package.
 *
 * Since: 0.1.0
 **/
void
hif_package_set_info (HyPackage pkg, HifPackageInfo info)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	priv->info = info;
}

/**
 * hif_package_set_action:
 * @pkg: a #HyPackage instance.
 * @action: the #HifStateAction for the package.
 *
 * Sets the action for the package, i.e. what is going to be performed.
 *
 * Since: 0.1.0
 */
void
hif_package_set_action (HyPackage pkg, HifStateAction action)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	priv->action = action;
}

/**
 * hif_package_get_user_action:
 * @pkg: a #HyPackage instance.
 *
 * Gets if the package was installed or removed as the user action.
 *
 * Returns: %TRUE if the package was explicitly requested
 *
 * Since: 0.1.0
 **/
gboolean
hif_package_get_user_action (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return FALSE;
	return priv->user_action;
}

/**
 * hif_package_set_user_action:
 * @pkg: a #HyPackage instance.
 * @user_action: %TRUE if the package was explicitly requested.
 *
 * Sets if the package was installed or removed as the user action.
 *
 * Since: 0.1.0
 **/
void
hif_package_set_user_action (HyPackage pkg, gboolean user_action)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	priv->user_action = user_action;
}

/**
 * hif_package_is_gui:
 * @pkg: a #HyPackage instance.
 *
 * Returns: %TRUE if the package is a GUI package
 *
 * Since: 0.1.0
 **/
gboolean
hif_package_is_gui (HyPackage pkg)
{
	gboolean ret = FALSE;
	gchar *tmp;
	gint idx;
	HyReldepList reldeplist;
	HyReldep reldep;
	int size;

	/* find if the package depends on GTK or KDE */
	reldeplist = hy_package_get_requires (pkg);
	size = hy_reldeplist_count (reldeplist);
	for (idx = 0; idx < size && !ret; idx++) {
		reldep = hy_reldeplist_get_clone (reldeplist, idx);
		tmp = hy_reldep_str (reldep);
		if (g_strstr_len (tmp, -1, "libgtk") != NULL ||
		    g_strstr_len (tmp, -1, "libQt5Gui.so") != NULL ||
		    g_strstr_len (tmp, -1, "libQtGui.so") != NULL ||
		    g_strstr_len (tmp, -1, "libqt-mt.so") != NULL) {
			ret = TRUE;
		}
		free (tmp);
		hy_reldep_free (reldep);
	}

	hy_reldeplist_free (reldeplist);
	return ret;
}

/**
 * hif_package_is_devel:
 * @pkg: a #HyPackage instance.
 *
 * Returns: %TRUE if the package is a development package
 *
 * Since: 0.1.0
 **/
gboolean
hif_package_is_devel (HyPackage pkg)
{
	const gchar *name;
	name = hy_package_get_name (pkg);
	if (g_str_has_suffix (name, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (name, "-devel"))
		return TRUE;
	if (g_str_has_suffix (name, "-static"))
		return TRUE;
	if (g_str_has_suffix (name, "-libs"))
		return TRUE;
	return FALSE;
}

/**
 * hif_package_is_downloaded:
 * @pkg: a #HyPackage instance.
 *
 * Returns: %TRUE if the package is already downloaded
 *
 * Since: 0.1.0
 **/
gboolean
hif_package_is_downloaded (HyPackage pkg)
{
	const gchar *filename;

	if (hy_package_installed (pkg))
		return FALSE;
	filename = hif_package_get_filename (pkg);
	if (filename == NULL) {
		g_warning ("Failed to get cache filename for %s",
			   hy_package_get_name (pkg));
		return FALSE;
	}
	return g_file_test (filename, G_FILE_TEST_EXISTS);
}

/**
 * hif_package_is_installonly:
 * @pkg: a #HyPackage instance.
 *
 * Returns: %TRUE if the package can be installed more than once
 *
 * Since: 0.1.0
 */
gboolean
hif_package_is_installonly (HyPackage pkg)
{
	const gchar **installonly_pkgs;
	const gchar *pkg_name;
	guint i;

	installonly_pkgs = hif_context_get_installonly_pkgs (NULL);
	pkg_name = hy_package_get_name (pkg);
	for (i = 0; installonly_pkgs[i] != NULL; i++) {
		if (g_strcmp0 (pkg_name, installonly_pkgs[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * hif_source_checksum_hy_to_lr:
 **/
static GChecksumType
hif_source_checksum_hy_to_lr (int checksum_hy)
{
	if (checksum_hy == HY_CHKSUM_MD5)
		return LR_CHECKSUM_MD5;
	if (checksum_hy == HY_CHKSUM_SHA1)
		return LR_CHECKSUM_SHA1;
	if (checksum_hy == HY_CHKSUM_SHA256)
		return LR_CHECKSUM_SHA256;
	return G_CHECKSUM_SHA512;
}

/**
 * hif_package_check_filename:
 * @pkg: a #HyPackage instance.
 * @valid: Set to %TRUE if the package is valid.
 * @error: a #GError or %NULL..
 *
 * Checks the package is already downloaded and valid.
 *
 * Returns: %TRUE if the package was checked successfully
 *
 * Since: 0.1.0
 **/
gboolean
hif_package_check_filename (HyPackage pkg, gboolean *valid, GError **error)
{
	LrChecksumType checksum_type_lr;
	char *checksum_valid = NULL;
	const gchar *path;
	const unsigned char *checksum;
	gboolean ret = TRUE;
	int checksum_type_hy;
	int fd;

	/* check if the file does not exist */
	path = hif_package_get_filename (pkg);
	g_debug ("checking if %s already exists...", path);
	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		*valid = FALSE;
		goto out;
	}

	/* check the checksum */
	checksum = hy_package_get_chksum (pkg, &checksum_type_hy);
	checksum_valid = hy_chksum_str (checksum, checksum_type_hy);
	checksum_type_lr = hif_source_checksum_hy_to_lr (checksum_type_hy);
	fd = g_open (path, O_RDONLY, 0);
	if (fd < 0) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     HIF_ERROR_INTERNAL_ERROR,
			     "Failed to open %s", path);
		goto out;
	}
	ret = lr_checksum_fd_cmp(checksum_type_lr,
				 fd,
				 checksum_valid,
				 TRUE, /* use xattr value */
				 valid,
				 error);
	if (!ret) {
		g_close (fd, NULL);
		goto out;
	}
	ret = g_close (fd, error);
	if (!ret)
		goto out;
out:
	hy_free (checksum_valid);
	return ret;
}

/**
 * hif_package_download:
 * @pkg: a #HyPackage instance.
 * @directory: destination directory, or %NULL for the cachedir.
 * @state: the #HifState.
 * @error: a #GError or %NULL..
 *
 * Downloads the package.
 *
 * Returns: the complete filename of the downloaded file
 *
 * Since: 0.1.0
 **/
gchar *
hif_package_download (HyPackage pkg,
		      const gchar *directory,
		      HifState *state,
		      GError **error)
{
	HifSource *src;
	src = hif_package_get_source (pkg);
	return hif_source_download_package (src, pkg, directory, state, error);
}

/**
 * hif_package_array_download:
 * @packages: an array of packages.
 * @directory: destination directory, or %NULL for the cachedir.
 * @state: the #HifState.
 * @error: a #GError or %NULL..
 *
 * Downloads an array of packages.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.1.0
 */
gboolean
hif_package_array_download (GPtrArray *packages,
			    const gchar *directory,
			    HifState *state,
			    GError **error)
{
	HifState *state_local;
	HyPackage pkg;
	guint i;

	/* download any package that is not currently installed */
	hif_state_set_number_steps (state, packages->len);
	for (i = 0; i < packages->len; i++) {
		_cleanup_free_ gchar *tmp = NULL;
		pkg = g_ptr_array_index (packages, i);
		state_local = hif_state_get_child (state);
		tmp = hif_package_download (pkg, NULL, state_local, error);
		if (tmp == NULL)
			return FALSE;

		/* done */
		if (!hif_state_done (state, error))
			return FALSE;
	}
	return TRUE;
}
