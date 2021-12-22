/*
 * zfcp helper functions for the s390-zfcp Debian-Installation
 * configuration module.
 *
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>

#include <debian-installer.h>

#include "s390-dev.h"

int zfcp_check_allow_lun_scan(int *enabled)
{
	int rc;
	char *path, *value;

	rc = -1;
	path = path_get_sys_module_param(ZFCP_MOD_NAME, "allow_lun_scan");
	if (path == NULL)
		goto out;

	value = misc_read_text_file(path, 1);
	if (value == NULL)
		goto out_free_path;

	rc = 0;
	*enabled = !strncmp(value, "Y", 1);

	free(value);
out_free_path:
	free(path);
out:
	return rc;
}

static char *get_port_type_path(const char *id)
{
	int rc;
	int err;
	DIR *dir;
	char *devpath, *path;
	struct dirent de, *dep;

	devpath = path_get_ccw_device(ZFCP_CCWDRV_NAME, id);
	dir = opendir(devpath);
	if (dir == NULL)
		goto out;

	path = NULL;
	dir_for_each_entry_safe(dir, &de, dep, err) {
		if (!starts_with(de.d_name, "host"))
			continue;
		rc = asprintf(&path, "%s/%s/fc_host/%s/port_type",
			      devpath, de.d_name, de.d_name);
		if (rc == -1)
			path = NULL;
		break;
	}

	closedir(dir);
out:
	free(devpath);

	return path;
}

result_t zfcp_dev_check_npiv(const char *ccwid, int *enabled)
{
	result_t rc;
	char *path, *type;

	rc = RESULT_RUNTIME_ERROR;
	path = get_port_type_path(ccwid);
	if (path == NULL)
		goto out;

	type = misc_read_text_file(path, 1);
	if (type == NULL)
		goto out;

	/* Check FCP port type. */
	*enabled = !strcmp(type, "NPIV VPORT");

	rc = RESULT_OK;
	free(type);
out:
	free(path);

	return rc;
}

result_t zfcp_lun_op(const struct zfcp_lun_devid *devid, const char *op)
{
	int rc;
	result_t result;
	char *portpath, *path = NULL, *lun = NULL;

	result = RESULT_OUT_OF_MEMORY;

	portpath = path_get_zfcp_port_dev(devid);
	if (portpath == NULL)
		goto out;

	if (!dir_exists(portpath)) {
		result = RESULT_ZFCP_WWPN_NOT_FOUND;
		goto out;
	}

	rc = asprintf(&path, "%s/%s", portpath, op);
	if (rc == -1)
		goto out;
	di_debug("ZFCP_LUN_OP: path=%s\n", path);

	rc = asprintf(&lun, "0x%016" PRIx64, devid->lun);
	if (rc == -1)
		goto out;

	if (misc_write_text_file(path, lun) != RESULT_OK) {
		result = RESULT_ZFCP_INVALID_LUN;
		goto out;
	}
	di_debug("ZFCP_LUN_OP: modified LUN: %s\n", lun);

	result = RESULT_OK;
out:
	free(lun);
	free(path);
	free(portpath);

	return result;
}

result_t zfcp_lun_add(const struct zfcp_lun_devid *devid)
{
	result_t rc;
	char *lunpath;

	lunpath = path_get_zfcp_lun_dev(devid);
	if (lunpath == NULL)
		return RESULT_OUT_OF_MEMORY;

	if (dir_exists(lunpath)) {
		rc = RESULT_OK;
		goto out;
	}

	rc = zfcp_lun_op(devid, "unit_add");
out:
	free(lunpath);
	return rc;
}

result_t scsi_device_remove(const char *hctl)
{
	int rc;
	result_t result;
	char *path, *delete = NULL;

	path = path_get_sys_bus_dev("scsi", hctl);
	if (path == NULL)
		return RESULT_OUT_OF_MEMORY;

	if (!dir_exists(path)) {
		result = RESULT_ZFCP_SCSI_NOT_FOUND;
		goto out;
	}

	rc = asprintf(&delete, "%s/delete", path);
	if (rc == -1) {
		result = RESULT_OUT_OF_MEMORY;
		goto out;
	}

	result = misc_write_text_file(delete, "\n");
out:
	free(delete);
	free(path);

	return result;
}

result_t zfcp_lun_remove(const struct zfcp_lun_devid *devid)
{
	result_t rc;
	char *lunpath;

	lunpath = path_get_zfcp_lun_dev(devid);
	if (lunpath == NULL)
		return RESULT_OUT_OF_MEMORY;

	if (!dir_exists(lunpath)) {
		rc = RESULT_ZFCP_INVALID_LUN;
		goto out;
	}

	rc = zfcp_lun_op(devid, "unit_remove");
out:
	free(lunpath);
	return rc;
}

/* Retrieve CCW device ID of HBA for specified SCSI device path. */
static char *devpath_to_hba_id(const char *path)
{
	char *copy, *start, *end, *hba_id = NULL;

	copy = strdup(path);
	if (copy == NULL)
		return NULL;

	/* copy=/devices/css0/0.0.001c/0.0.1940/host0/... */
	end = strstr(copy, "/host");
	if (end == NULL)
		goto out;
	*end = 0;
	/* copy=/devices/css0/0.0.001c/0.0.1940 */
	start = strrchr(copy, '/');
	if (start == NULL)
		goto out;
	start++;
	hba_id = strdup(start);
out:
	free(copy);

	return hba_id;
}

/* Retrieve WWPN from specified SCSI device path. */
static char *devpath_to_wwpn(const char *devpath)
{
	char *copy, *rport, *end, *path = NULL, *wwpn = NULL;

	/* devpath=/devices/css0/0.0.001c/0.0.1940/host0/rport-0:0-16/
	 *          target0:0:16/0:0:16:1085030433/ */
	copy = strdup(devpath);
	rport = strstr(copy, "/rport-");
	end = skip_comp(rport);
	if (end == NULL)
		goto out;
	*end = 0;

	/* devpath=/devices/css0/0.0.001c/0.0.1940/host0/rport-0:0-16
	 * rport=rport-0:0-16 */
	path = path_get("/sys%s/fc_remote_ports%s/port_name", copy, rport);
	if (path != NULL)
		wwpn = misc_read_text_file(path, 1);
out:
	free(path);
	free(copy);

	return wwpn;
}

static char *lun_to_str(uint64_t lun)
{
	int rc;
	char *fcplun = NULL;

	rc = asprintf(&fcplun, "0x%16" PRIx64, lun);
	if (rc == -1)
		return NULL;
	return fcplun;
}

static char *scsi_htcl_to_zfcp_lun_devpath(const char *hctl)
{
	char *buspath, *link = NULL;

	buspath = path_get_sys_bus_dev("scsi", hctl);
	if (buspath == NULL)
		return NULL;
	link = misc_readlink(buspath);
	free(buspath);

	return link;
}

int zfcp_for_each_scsi_dev(int (*cb)(const char *, const char *, const char *,
				     const char*, void *data), void *data)
{
	int rc, err;
	DIR *devices;
	struct dirent scsi, *dep;
	struct scsi_hctl_devid hctl;
	char *path, *devpath, *hba_id, *wwpn, *lun;

	path = path_get_sys_bus_dev("scsi", NULL);
	if (path == NULL)
		return 0;
	rc = 0;
	devices = opendir(path);
	if (devices == NULL) {
		di_info("Could not open directory: %s: %s\n", path,
			strerror(errno));
		free(path);
		/* There are likely no SCSI devices */
		return 0;
	}

	/*
	 * Scan all devices on the SCSI bus to spot SCSI devices managed by
	 * the zfcp driver.
	 */
	dir_for_each_entry_safe(devices, &scsi, dep, err) {
		if (starts_with(scsi.d_name, "host") ||
		    starts_with(scsi.d_name, "target"))
			continue;
		if (scsi_hctl_parse_devid(&hctl, scsi.d_name))
			continue;

		devpath = scsi_htcl_to_zfcp_lun_devpath(scsi.d_name);
		if (devpath == NULL)
			continue;
		path = strstr(devpath, "/devices/");
		if (path == NULL) {
			free(devpath);
			continue;
		}
		hba_id = devpath_to_hba_id(path);
		wwpn = devpath_to_wwpn(path);
		lun = lun_to_str(scsi_lun_to_fcp_lun(hctl.lun));
		if (hba_id != NULL && wwpn != NULL && lun != NULL)
			rc = cb(scsi.d_name, hba_id, wwpn, lun, data);
		free(lun);
		free(wwpn);
		free(hba_id);
		free(devpath);
		if (rc)
			break;
	}
	closedir(devices);

	return rc;
}
