/*
 * Copyright IBM Corp. 2015
 *
 * Author(s): Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *	      Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "s390-dev.h"


/* Return a path that is created by resolving the specified format string
 * @fmt and applying any base prefixes. */
char *path_get(const char *fmt, ...)
{
	va_list args;
	char *path;

	/* Get original path. */
	va_start(args, fmt);
	if (vasprintf(&path, fmt, args) == -1)
		return NULL;
	va_end(args);

	return path;
}

/* Return sysfs path to module directory. */
char *path_get_sys_module(const char *mod)
{
	return path_get("/sys/module/%s", mod);
}

/* Return sysfs path to module parameter file. */
char *path_get_sys_module_param(const char *mod, const char *name)
{
	if (name)
		return path_get("/sys/module/%s/parameters/%s", mod, name);

	return path_get("/sys/module/%s/parameters", mod);
}

/* Return sysfs path to /sys/dev/block/major:minor directory. */
char *path_get_sys_dev_block(unsigned int major, unsigned int minor)
{
	return path_get("/sys/dev/block/%d:%d", major, minor);
}

/* Return sysfs path to class directory. */
char *path_get_sys_class(const char *class, const char *name)
{
	if (name)
		return path_get("/sys/class/%s/%s", class, name);

	return path_get("/sys/class/%s", class);
}

/* Return sysfs path to CCW device. */
char *path_get_ccw_device(const char *drv, const char *id)
{
	if (drv)
		return path_get("%s/drivers/%s/%s", PATH_CCW_BUS, drv, id);

	return path_get("%s/devices/%s", PATH_CCW_BUS, id);
}

/* Return sysfs path to the specified attribute of a CCW device. */
char *path_get_ccw_device_attr(const char *drv, const char *id,
			       const char *attr) {
	if (drv)
		return path_get("%s/drivers/%s/%s/%s",
				PATH_CCW_BUS, drv, id, attr);

	return path_get("%s/devices/%s/%s", PATH_CCW_BUS, id, attr);
}

/* Return sysfs path to directory containing all CCW devices. */
char *path_get_ccw_devices(const char *drv)
{
	if (drv)
		return path_get("%s/drivers/%s/", PATH_CCW_BUS, drv);

	return path_get("%s/devices/", PATH_CCW_BUS);
}

/* Return sysfs path to CCWGROUP device. */
char *path_get_ccwgroup_device(const char *drv, const char *id)
{
	if (drv)
		return path_get("%s/drivers/%s/%s", PATH_CCWGROUP_BUS, drv, id);

	return path_get("%s/devices/%s", PATH_CCWGROUP_BUS, id);
}

/* Return sysfs path to directory containing all CCWGROUP devices. */
char *path_get_ccwgroup_devices(const char *drv)
{
	if (drv)
		return path_get("%s/drivers/%s/", PATH_CCWGROUP_BUS, drv);

	return path_get("%s/devices/", PATH_CCWGROUP_BUS);
}

/* Call a function for each entry in a directory:
 * exit_code_t callback(const char *abs_path, const char *rel_path, void *data)
 * Aborts when callback returns any value other than EXIT_OK.
 */
result_t path_for_each(const char *path,
			  result_t (*callback)(const char *, const char *,
						  void *), void *data)
{
	DIR *dir;
	struct dirent de, *dep;
	char *p;
	int err;
	result_t rc = RESULT_OK;

	dir = opendir(path);
	if (dir == NULL)
		return RESULT_RUNTIME_ERROR;

	do {
		err = readdir_r(dir, &de, &dep);
		if (err) {
			rc = RESULT_RUNTIME_ERROR;
		} else if (dep) {
			if (strcmp(de.d_name, ".") == 0 ||
			    strcmp(de.d_name, "..") == 0)
				continue;
			p = path_get("%s/%s", path, de.d_name);
			rc = callback(p, de.d_name, data);
			free(p);
		}
	} while (dep && rc == RESULT_OK);

	closedir(dir);

	return rc;
}

/* Return sysfs path to device or devices directory. */
char *path_get_sys_bus_dev(const char *bus, const char *id)
{
	if (!id)
		return path_get("/sys/bus/%s/devices", bus);

	return path_get("/sys/bus/%s/devices/%s", bus, id);
}

/* Return sysfs path to scsi drivers directory. */
char *path_get_sys_bus_drv(const char *bus, const char *drv)
{
	if (!drv)
		return path_get("/sys/bus/%s/drivers", bus);

	return path_get("/sys/bus/%s/drivers/%s", bus, drv);
}

/* Return sysfs path to zFCP LUN directory. */
char *path_get_zfcp_lun_dev(const struct zfcp_lun_devid *id)
{
	return path_get("%s/drivers/%s/%x.%x.%04x/0x%016" PRIx64
			"/0x%016" PRIx64, PATH_CCW_BUS,
			ZFCP_CCWDRV_NAME, id->fcp_dev.cssid,
			id->fcp_dev.ssid, id->fcp_dev.devno, id->wwpn, id->lun);
}

/* Return sysfs path to zFCP target port directory. */
char *path_get_zfcp_port_dev(const struct zfcp_lun_devid *id)
{
	return path_get("%s/drivers/%s/%x.%x.%04x/0x%016" PRIx64,
			PATH_CCW_BUS, ZFCP_CCWDRV_NAME,
			id->fcp_dev.cssid, id->fcp_dev.ssid,
			id->fcp_dev.devno, id->wwpn);
}

/* Return sysfs path to SCSI device directory. */
char *path_get_scsi_hctl_dev(const char *hctl)
{
	return path_get("/sys/bus/scsi/devices/%s", hctl);
}
