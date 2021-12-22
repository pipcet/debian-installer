/*
 * Debian Installer Utility to configure FC-attched SCSI devices
 * for Linux on System z and Linux on z Systems (s390).
 *
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <cdebconf/debconfclient.h>
#include <debian-installer.h>

#include "s390-dev.h"

#ifndef __unused
#	define  __unused __attribute__ ((unused))
#endif

/* Directory where the zfcp configuration files are written */
#define SYSCONFIG_DIR "/etc/sysconfig/hardware/"
#define CONFIG_PREFIX SYSCONFIG_DIR "config-ccw-"

/* External commands */
#define UDEVADM "udevadm"

/* Debconf related definitions */
#define TEMPLATE_PREFIX	"s390-zfcp/"
#define DEBCONF_CAPS	"backup"
#define PRESEED_DELIM	","
#define SCSI_ASYNC_TRIES	15
#define SCSI_ASYNC_TIMEOUT	250000	  /* useconds */
#define ZFCP_ENTRY_SIZE		(9 + 20 + 1)
#define MAX_LUN_LIST_SIZE	2048

/* Memory buffer descriptor for an arbitrary number of
 * detected FCP devices and their device information.
 */
struct dev_list
{
	char	*buf;
	size_t	size;
};

/* Definition of a zFCP host adapter */
struct zfcp_host {
	struct ccw_devid devid;	      /* CCW device ID */
	char	name[PATH_MAX];	      /* Device and display name */
	struct di_tree *luns;	      /* List of WWPN:LUN pairs */
	int	online;		      /* Adapter is enabled */
	int	npiv;		      /* Adapter uses N_PORT virtualization */
	int	write_luns;	      /* Write automatically added LUNs */
	int	configured;	      /* Configuration file written */
};

/* Definition of state and state transitions */
enum state {
	BACKUP,			/* Backup state */
	DETECT_ZFCP_HOSTS,	/* Detect available zfcp host adapter */
	PRESEED,		/* Read and parse preseeding data */
	SELECT_ZFCP_HOST,	/* Select adapter for configuration */
	REMOVE_CONFIG,		/* Remove zfcp configuration file */
	ENABLE,			/* Activate an zfcp host adapter */
	ADD_ZFCP_LUNS,		/* Attach SCSI disks */
	WRITE,			/* Write zfcp configuration files */
	ERROR,			/* Indicate an error condition */
	FINISH,			/* Completed zfcp configuration */
};

enum state_wanted
{
	WANT_BACKUP,
	WANT_NEXT,
	WANT_FINISH,
	WANT_ERROR
};

/* Exit codes */
#define EXIT_BACKUP 10

/* List of zfcp host adapters available for configuration */
static di_tree *zfcp_hosts;

/* Indicator that the zfcp device driver performs automatic LUN scanning */
static int zfcp_allow_lun_scan;

/* Indicator that the module runs unattended through preseeding */
static int preseeded;

/* List of preseeded zfcp lun identifiers */
static di_tree *preseed_data;


static int di_compare_ccw_devids(const void *key1, const void *key2)
{
	return ccw_cmp_devids(key1, key2);
}

static int di_compare_zfcp_lun_devids(const void *key1, const void *key2)
{
	return zfcp_lun_cmp_devids(key1, key2);
}

static void di_free_zfcp_host(void *host)
{
	struct zfcp_host *zfcp_host = host;

	if (zfcp_host->luns != NULL)
		di_tree_destroy(zfcp_host->luns);
	di_free(zfcp_host);
}

static int add_zfcp_host(struct di_tree *tree, const char *id)
{
	int online;
	char *path, *value;
	char config[PATH_MAX];
	struct zfcp_host *zfcp_host;

	path = path_get_ccw_device_attr(ZFCP_CCWDRV_NAME, id, "online");
	if (path == NULL)
		return -1;

	if (access(path, F_OK)) {
		free(path);
		/* Ignore this host adapter */
		return 0;
	}

	value = misc_read_text_file(path, 1);
	online = strtol(value, NULL, 10);
	free(value);
	free(path);

	zfcp_host = di_malloc0(sizeof(*zfcp_host));
	if (zfcp_host == NULL)
		return -1;

	strncpy(zfcp_host->name, id, sizeof(zfcp_host->name));
	if (ccw_parse_devid(&zfcp_host->devid, zfcp_host->name) != RESULT_OK) {
		di_free(zfcp_host);
		return -1;
	}

	if (online > 0) {
		zfcp_host->online = 1;
		zfcp_dev_check_npiv(zfcp_host->name, &zfcp_host->npiv);
	}

	snprintf(config, sizeof(config), CONFIG_PREFIX "%s", zfcp_host->name);
	zfcp_host->configured = !access(config, F_OK);

	zfcp_host->luns = NULL;
	di_tree_insert(tree, &zfcp_host->devid, zfcp_host);
	di_debug("DETECT: Added FCP device: %s: online=%d npiv=%d\n",
		 zfcp_host->name, zfcp_host->online, zfcp_host->npiv);

	return 0;
}

static enum state_wanted detect_zfcp_hosts(struct debconfclient *client)
{
	int err;
	char *path;
	DIR *devices;
	enum state_wanted rc;
	struct dirent dev, *dep;

	/*
	 * Detect and collect FCP adapters.  FCP adapters might not be
	 * available, because
	 *
	 *  a) FCP adapters are not configured or attached to the Linux
	 *     instance, or
	 *  b) the CCW device IDs for the FCP adapters are black-listed
	 *     through cio_ignore.
	 *
	 * CIO_IGNORE:
	 *
	 * Handling cio_ignore is currently not supported by this module.
	 * To support it, users have to provide the CCW device IDs
	 * manually.  The module has then to renove the device IDs from
	 * the black-list.  This can be automatically handled by using
	 * preseed data or by introducing a [WANT_]UNIGNORE_ZFCP_HOSTS
	 * state to ask the user for FCP devices.
	 *
	 * For now, the module reports that there are no FCP devices
	 * if the zfcp device driver module is not loaded or if it is
	 * loaded but no FCP devices were found.
	 */
	path = path_get_ccw_devices(ZFCP_CCWDRV_NAME);
	if (path == NULL) {
		di_error("Could not allocate memory\n");
		return WANT_ERROR;
	}

	rc = WANT_ERROR;
	devices = opendir(path);
	if (devices == NULL) {
		if (errno == ENOENT) {
			di_debug("DETECT: The zfcp device driver is not loaded\n");
			debconf_input(client, "low", TEMPLATE_PREFIX "no_zfcp_hosts");
			debconf_capb(client);
			debconf_go(client);
			rc = WANT_FINISH;
		}
		di_warning("Could not open directory: %s: %s\n", path,
			   strerror(errno));
		goto out;
	}

	dir_for_each_entry_safe(devices, &dev, dep, err) {
		if (ccw_parse_devid(NULL, dev.d_name) != RESULT_OK)
			continue;
		if (add_zfcp_host(zfcp_hosts, dev.d_name))
			goto out;
	}
	closedir(devices);

	zfcp_check_allow_lun_scan(&zfcp_allow_lun_scan);
	if (zfcp_allow_lun_scan)
		di_debug("DETECT: Automatic LUN scanning is enabled\n");

	rc = WANT_NEXT;
	if (!di_tree_size(zfcp_hosts)) {
		di_debug("DETECT: No FCP devices are available\n");
		debconf_input(client, "low", TEMPLATE_PREFIX "no_zfcp_hosts");
		debconf_capb(client);
		debconf_go(client);
		rc = WANT_FINISH;
	}
out:
	free(path);
	return rc;
}

static result_t preseed_zfcp_lun_id(struct zfcp_lun_devid **new, const char *spec)
{
	result_t rc;
	struct zfcp_lun_devid *devid;

	devid = di_malloc0(sizeof(*devid));
	if (devid == NULL)
		return -1;

	if (strchr(spec, ':') == NULL)
		/* Only a FCP CCW device ID specified */
		rc = ccw_parse_devid(&devid->fcp_dev, spec);
	else
		rc = zfcp_lun_parse_devid(devid, spec);

	if (rc == RESULT_OK)
		*new = devid;
	else
		di_free(devid);

	return rc;
}

static enum state_wanted preseed_zfcp(struct debconfclient *c)
{
	int *seen;
	enum state_wanted rc;
	char *ent, *spec = NULL, *token_state;
	struct zfcp_lun_devid *devid;

	debconf_get(c, TEMPLATE_PREFIX "zfcp");
	if (c->value == NULL || !strlen(c->value)) {
		di_debug("PRESEED: No preseed data available\n");
		return WANT_NEXT;
	}
	di_info("PRESEED: '%s'", c->value);

	rc = WANT_ERROR;
	spec = strdup(c->value);
	if (spec == NULL) {
		di_error("Could not allocate memory for preseed data\n");
		goto out;
	}

	preseed_data = di_tree_new_full(di_compare_zfcp_lun_devids, di_free, di_free);
	if (preseed_data == NULL) {
		di_error("Could not allocate memory for preseed data tree\n");
		goto out;
	}

	ent = strtok_r(spec, PRESEED_DELIM, &token_state);
	while (ent != NULL) {
		ent = strtrim(ent);
		di_debug("PRESEED DATA ENTRY: '%s'\n", ent);
		if (preseed_zfcp_lun_id(&devid, ent) != RESULT_OK)
			goto out_free_tree;
		seen = di_malloc0(sizeof(*seen));
		if (seen == NULL)
			goto out_free_tree;
		di_tree_insert(preseed_data, devid, seen);
		ent = strtok_r(NULL, PRESEED_DELIM, &token_state);
	}

	rc = WANT_NEXT;
	preseeded = di_tree_size(preseed_data);

out_free_tree:
	if (!preseeded)
		di_tree_destroy(preseed_data);
out:
	free(spec);

	return rc;
}

/*
 * Request input from debconf for the specified template.  The function
 * returns the return code of the debconf_go() function and sets the
 * **result pointer to the result.
 */
static cmdstatus_t debconf_result(struct debconfclient *client,
				  const char *priority, const char *template,
				  char **result)
{
	cmdstatus_t ret;

	debconf_input(client, priority, template);
	ret = debconf_go(client);
	debconf_get(client, template);
	*result = client->value;

	return ret;
}

static cmdstatus_t debconf_show_error(struct debconfclient *client,
				      const char *template)
{
	cmdstatus_t ret;

	debconf_input(client, "critical", template);
	debconf_capb(client);
	ret = debconf_go(client);
	debconf_capb(client, DEBCONF_CAPS);

	return ret;
}

static void build_zfcp_host_list(void *key __unused, void *value, void *data)
{
	struct zfcp_host *host = value;
	struct dev_list *devlist = data;

	if (devlist->buf[0])
		di_snprintfcat (devlist->buf, devlist->size, ", ");
	di_snprintfcat(devlist->buf, devlist->size, host->name);

	if (host->configured)
		di_snprintfcat (devlist->buf, devlist->size, " (configured)");
	else if (host->online)
		di_snprintfcat (devlist->buf, devlist->size, " (not configured)");
}

static void preseed_select_one(void *key, void *value, void *data)
{
	char *fcpdev;
	int *seen = value;
	struct zfcp_host *host;
	struct zfcp_lun_devid *devid = key;
	struct zfcp_host **selected = data;

	/* There is already a zfcp host adapter selected */
	if (*selected != NULL)
		return;

	/* The current entry has been already processed. */
	if (*seen)
		return;

	/* Try to find the next unconfigured zfcp host adapter */
	host = di_tree_lookup(zfcp_hosts, &devid->fcp_dev);
	if (host == NULL) {
		/*
		 * CIO_IGNORE:
		 * Remove the FCP device from the blacklist and retry.
		 */
		ccw_devid_to_str(&fcpdev, &devid->fcp_dev);
		di_error("Preseeded FCP device was not detected: %s\n", fcpdev);
		free(fcpdev);
		return;
	}

	if (host->configured)
		return;

	*selected = host;
	*seen = 1;
}

static enum state_wanted select_preseeded(struct zfcp_host **zfcp_host)
{
	if (!di_tree_size(preseed_data)) {
		di_error("No preseeded FCP device data available\n");
		return WANT_ERROR;
	}

	/*
	 * Iterate through the list of preseed data and select each
	 * zfcp host adapter which is not configured yet.
	 */
	*zfcp_host = NULL;
	di_tree_foreach(preseed_data, preseed_select_one, zfcp_host);
	if (*zfcp_host == NULL)
		return WANT_FINISH;

	return WANT_NEXT;
}

static enum state_wanted select_zfcp_host(struct debconfclient *client,
					  struct zfcp_host **zfcp_host)
{
	cmdstatus_t rc;
	char *val, *delim;
	struct ccw_devid devid;
	struct dev_list hostlist;

	/*
	 * Allocate memory to store the detected FCP devices along
	 * with their state information.
	 */
	hostlist.size = ZFCP_ENTRY_SIZE * di_tree_size (zfcp_hosts);
	hostlist.buf = di_malloc0 (hostlist.size);
	if (hostlist.buf == NULL)
	{
		di_warning("Could not allocate memory for FCP device list\n");
		return WANT_ERROR;
	}
	di_tree_foreach(zfcp_hosts, build_zfcp_host_list, &hostlist);

	debconf_subst(client, TEMPLATE_PREFIX "select_zfcp_host", "choices", hostlist.buf);
	rc = debconf_result(client, "critical", TEMPLATE_PREFIX "select_zfcp_host", &val);
	if (rc == CMD_GOBACK) {
		di_free (hostlist.buf);
		return WANT_BACKUP;
	}
	if (!strcmp("Finish", val)) {
		di_free (hostlist.buf);
		return WANT_FINISH;
	}

	/* Remove the trailing "configured" information */
	delim = strchr(val, ' ');
	if (delim != NULL)
		*delim = '\0';

	ccw_parse_devid(&devid, val);
	*zfcp_host = di_tree_lookup(zfcp_hosts, &devid);
	if (*zfcp_host == NULL) {
		di_debug("Could not find FCP device for devid: %s\n", val);
		di_free (hostlist.buf);
		return WANT_ERROR;
	}
	di_debug("SELECT: Using FCP device %s\n", val);
	di_free (hostlist.buf);

	return WANT_NEXT;
}

static enum state_wanted remove_config_file(struct debconfclient *client,
					    struct zfcp_host *zfcp_host)
{
	char *val;
	cmdstatus_t rc;
	char path[PATH_MAX];

	/* Ignore FCP devices that are not configured */
	if (!zfcp_host->configured)
		return WANT_NEXT;

	/* Reset the question to its default value */
	debconf_reset(client, TEMPLATE_PREFIX "remove_zfcp_config");

	/* Confirm removal of the FCP device configuration file */
	rc = debconf_result(client, "critical",
			    TEMPLATE_PREFIX "remove_zfcp_config", &val);
	if (rc == CMD_GOBACK)
		return WANT_BACKUP;

	if (!strcmp(val, "false"))
		return WANT_NEXT;

	/* Remove FCP device configuration file */
	snprintf(path, sizeof(path), CONFIG_PREFIX "%s", zfcp_host->name);
	if (unlink(path)) {
		di_warning("Could not remove %s: %s\n", path, strerror(errno));
		return WANT_ERROR;
	}
	zfcp_host->configured = 0;

	return WANT_BACKUP;
}

static void udev_settle(void)
{
	if (di_exec_shell_log(UDEVADM " settle") == -1)
		di_warning("Could not run " UDEVADM " settle: %s", strerror(errno));
}

/* XXX
 *
 * Remove this function if the zfcp race condition is corrected.  For now,
 * delay the installation for some time to wait until the SCSI devices
 * becomes populated.
 */
static void debconf_udev_progress(struct debconfclient *client, useconds_t usec,
			     unsigned tries)
{
	unsigned count;

	debconf_capb(client);
	debconf_progress_start(client, 0, tries, TEMPLATE_PREFIX "udev_progress");

	for (count = 0; count < tries; count++) {
		usleep(usec);
		debconf_progress_step(client, 1);
	}
	udev_settle();

	debconf_progress_stop(client);
	debconf_capb(client, DEBCONF_CAPS);
}

static enum state_wanted enable_zfcp_host(struct zfcp_host *zfcp_host)
{
	char *attr;
	enum state_wanted rc;

	if (zfcp_host->online)
		return WANT_NEXT;

	rc = WANT_ERROR;
	attr = path_get_ccw_device_attr(NULL, zfcp_host->name, "online");
	if (attr == NULL)
		goto out;

	if (misc_write_text_file(attr, "1") != RESULT_OK)
		goto out_free;
	zfcp_host->online = 1;

	if (zfcp_dev_check_npiv(zfcp_host->name, &zfcp_host->npiv))
		goto out;

	di_debug("ENABLE: Activated FCP device %s (npiv=%d)\n",
		 zfcp_host->name, zfcp_host->npiv);

	rc = WANT_NEXT;
out_free:
	free(attr);
out:
	return rc;
}

struct lun_tree {
	struct zfcp_host *hba;
	struct di_tree *luns;
};

static int lun_tree_insert(const char *hctl, const char *hba_id,
			   const char *wwpn, const char *lun, void *data)
{
	struct zfcp_lun_devid *zfcplun;
	uint64_t wwpn_num, lun_num;
	struct lun_tree *lt = data;
	char *scsidev;

	/*
	 * Compare the host bus adapter IDs by using their stringified
	 * device name.  An improved version could use the parsed CCW
	 * bus ID instead.  Note that this filters the LUNs for a
	 * particular zfcp host adapter.
	 */
	if (strcmp(lt->hba->name, hba_id))
		return 0;

	/*
	 * Scan the WWPN and LUN value.  Note that both starts with 0x
	 * resulting in the offset by 2.  Ignore this LUN if it cannot
	 * be parsed successfully.
	 */
	if (sscanf(wwpn + 2, "%16" SCNx64, &wwpn_num) != 1)
		return 0;
	if (sscanf(lun + 2, "%16" SCNx64, &lun_num) != 1)
		return 0;

	zfcplun = di_malloc0(sizeof(*zfcplun));
	if (zfcplun == NULL) {
		di_error("Could not allocate memory\n");
		return 1;
	}
	zfcplun->fcp_dev = lt->hba->devid;
	zfcplun->wwpn = wwpn_num;
	zfcplun->lun = lun_num;
	scsidev = strdup(hctl);
	if (scsidev == NULL) {
		di_free(zfcplun);
		di_error("Could not allocate memory\n");
		return 1;
	}

	if (di_tree_lookup(lt->luns, zfcplun) != NULL) {
		di_free(zfcplun);
		free(scsidev);
		return 0;
	}
	di_tree_insert(lt->luns, zfcplun, scsidev);
	di_debug("POPULATE LUN TREE: %s:%s:%s [%s]\n", hba_id, wwpn, lun, hctl);

	return 0;
}

static di_tree *lun_tree(struct lun_tree *lt)
{
	return lt->luns;
}

static void lun_tree_destroy(struct lun_tree *lt)
{
	if (lt->luns != NULL)
		di_tree_destroy(lt->luns);
	lt->luns = NULL;
}

static int lun_tree_populate(struct lun_tree *lt)
{
	lun_tree_destroy(lt);

	lt->luns = di_tree_new_full(di_compare_zfcp_lun_devids, di_free, free);
	if (lt->luns == NULL) {
		di_error("Could not allocate memory for the LUN tree\n");
		return -1;
	}
	udev_settle();

	return zfcp_for_each_scsi_dev(lun_tree_insert, lt);
}

/* Returns a zfcp LUN ID consisting of FCP device, WWPN, and LUN part. */
static result_t zfcp_parse_wwpn_lun(struct zfcp_lun_devid *devid,
				    struct zfcp_host *hba, const char *lun_str)
{
	char *devid_str;
	result_t result;

	if (asprintf(&devid_str, "%s:%s", hba->name, lun_str) == -1)
		return RESULT_OUT_OF_MEMORY;
	result = zfcp_lun_parse_devid(devid, devid_str);
	free(devid_str);

	return result;
}

static result_t __add_lun(struct zfcp_lun_devid *lun, struct lun_tree *lt)
{
	int i;
	result_t result;

	/* Check if the specified LUN is already in the list */
	if (di_tree_lookup(lun_tree(lt), lun) != NULL)
		return RESULT_OK;

	/* Enable the LUN */
	result = zfcp_lun_add(lun);
	if (result != RESULT_OK)
		goto out;

	/*
	 * Repopulate the LUN tree and confirm that a SCSI device was created
	 * for the specified LUN.  The updated LUN tree contains a LUN node
	 * for it.
	 *
	 * If scsi_mod.scan is "async" is set, try several times until the
	 * SCSI asynchronous processing completes.  To prevent any issues
	 * here, set scsi_mod.scan=sync on the kernel command line.
	 */
	for (i = 0; i < SCSI_ASYNC_TRIES; i++) {
		lun_tree_populate(lt);
		if (di_tree_lookup(lun_tree(lt), lun) != NULL)
			goto out;
		usleep(SCSI_ASYNC_TIMEOUT);
	}

	/* Number of tries exceeded, remove LUN and fail */
	result = RESULT_ZFCP_SCSI_NOT_FOUND;
	zfcp_lun_remove(lun);
	di_info("Number of retries to find a SCSI device exceeded\n");
	di_info("Consider specifying the kernel parameter: scsi_mod.scan=sync\n");
out:
	return result;
}

static int add_lun(struct debconfclient *client, struct zfcp_host *zfcp_host,
		   struct lun_tree *lt)
{
	struct zfcp_lun_devid *lun;
	cmdstatus_t rc;
	result_t result;
	char *value;

	rc = debconf_result(client, "critical", TEMPLATE_PREFIX "get_lun", &value);
	if (rc == CMD_GOBACK)
		return 0;

	lun = di_malloc0(sizeof(*lun));
	if (lun == NULL)
		return -1;
	result = zfcp_parse_wwpn_lun(lun, zfcp_host, value);
	if (result != RESULT_OK) {
		di_free(lun);
		switch (result) {
		case RESULT_INVALID_LUN_FMT:
			debconf_show_error(client, TEMPLATE_PREFIX "invalid_lun_fmt");
			goto out;
		case RESULT_INVALID_WWPN_LEN:
		case RESULT_INVALID_WWPN:
		case RESULT_INVALID_LUN_LEN:
		case RESULT_INVALID_LUN:
			debconf_show_error(client, TEMPLATE_PREFIX "invalid_wwpn_or_lun");
			goto out;
		default:
			return -1;
		}
	}

	/* Enable the LUN */
	result = __add_lun(lun, lt);
	switch (result) {
	case RESULT_OK:
		di_debug("ADD_LUN: %s\n", value);
		break;
	case RESULT_ZFCP_WWPN_NOT_FOUND:
		debconf_show_error(client, TEMPLATE_PREFIX "port_not_found");
		goto out;
	case RESULT_ZFCP_INVALID_LUN:
		debconf_show_error(client, TEMPLATE_PREFIX "lun_not_added");
		goto out;
	case RESULT_ZFCP_SCSI_NOT_FOUND:
		debconf_show_error(client, TEMPLATE_PREFIX "lun_not_added");
		goto out;
	default:
		di_free(lun);
		return -1;
	}

	debconf_reset(client, TEMPLATE_PREFIX "get_lun");
	di_free(lun);
out:
	return 0;
}

static int remove_lun(struct debconfclient *client, const char *lunstr,
		      struct zfcp_host *zfcp_host, struct lun_tree *lt)
{
	int rc;
	char *value, *hctl;
	result_t result;
	struct zfcp_lun_devid *lun;

	lun = di_malloc0(sizeof(*lun));
	if (lun == NULL)
		return -1;
	result = zfcp_parse_wwpn_lun(lun, zfcp_host, lunstr);
	if (result != RESULT_OK) {
		di_free(lun);
		di_error("REMOVE_LUN: Failed to parse LUN: %s\n", lunstr);
		return -1;
	}

	rc = 0;
	debconf_subst(client, TEMPLATE_PREFIX "remove_lun", "LUN", lunstr);
	rc = debconf_result(client, "critical", TEMPLATE_PREFIX "remove_lun", &value);
	if (rc == CMD_GOBACK)
		goto out;

	if (!strcmp(value, "false"))
		goto out;

	/* Lookup the LUN and retrieve the associated SCSI device */
	hctl = di_tree_lookup(lun_tree(lt), lun);
	if (hctl == NULL) {
		di_warning("REMOVE_LUN: Could not find LUN %s\n", value);
		goto out;
	}

	/*
	 * Remove the SCSI device and, then, remove the LUN from the zfcp
	 * host adapter.
	 */
	scsi_device_remove(hctl);
	result = zfcp_lun_remove(lun);
	switch (result) {
	case RESULT_OK:
		break;
	case RESULT_ZFCP_FCP_NOT_FOUND:
		di_warning("REMOVE_LUN: Could not find FCP device: %s\n",
			   zfcp_host->name);
	default:/* fall-through */
		rc = -1;
		debconf_show_error(client, TEMPLATE_PREFIX "lun_not_removed");
		goto out;
	}

	/*
	 * Repopulate the LUN tree and confirm that the SCSI device was
	 * removed.
	 */
	lun_tree_populate(lt);
	if (di_tree_lookup(lun_tree(lt), lun) != NULL) {
		di_warning("REMOVE_LUN: LUN still present %s\n", lunstr);
		debconf_show_error(client, TEMPLATE_PREFIX "lun_not_removed");
	}
out:
	di_free(lun);
	return rc;
}

static void build_lun_list(void *key, void *value __unused, void *data)
{
	struct zfcp_lun_devid *lun = key;
	char *list = data;
	size_t len;

	if (list[0])
		strncat(list, ",", MAX_LUN_LIST_SIZE);
	len = strnlen(list, MAX_LUN_LIST_SIZE);
	if (len == MAX_LUN_LIST_SIZE)
		return;
	snprintf(list + len, MAX_LUN_LIST_SIZE - len,
		 "0x%016" PRIx64 ":0x%016" PRIx64, lun->wwpn, lun->lun);
}

static enum state_wanted add_zfcp_luns_user(struct debconfclient *client,
					    struct lun_tree *lt)
{
	int finished;
	cmdstatus_t rc;
	char *lunlist, *action;
	enum state_wanted wanted;
	struct zfcp_host *zfcp_host = lt->hba;

	lunlist = di_new0(char, MAX_LUN_LIST_SIZE);
	if (lunlist == NULL)
		return WANT_ERROR;

	wanted = WANT_NEXT;
	finished = 0;
	do {
		memset(lunlist, 0, MAX_LUN_LIST_SIZE);
		di_tree_foreach(lun_tree(lt), build_lun_list, lunlist);
		debconf_subst(client, TEMPLATE_PREFIX "add_zfcp_luns", "LUNS", lunlist);
		rc = debconf_result(client, "critical", TEMPLATE_PREFIX "add_zfcp_luns", &action);
		if (rc == CMD_GOBACK) {
			/*
			 * The current configuration is kept as-is.  Added and
			 * enabled LUNs are not removed.  The users can simply
			 * go back and can then continue where they have
			 * stopped.
			 */
			wanted = WANT_BACKUP;
			goto out;
		}

		if (!strcmp(action, "Add LUN"))
			if (add_lun(client, zfcp_host, lt) < 0) {
				wanted = WANT_ERROR;
				goto out;
			}

		if (starts_with(action, "0x"))
			if (remove_lun(client, action, zfcp_host, lt) < 0) {
				wanted = WANT_ERROR;
				goto out;
			}

		if (!strcmp(action, "Finish"))
			finished = 1;
	} while (!finished);
out:
	di_free(lunlist);
	return wanted;
}

struct preseed_cb {
	struct lun_tree *lt;
	int err;
};

static void preseed_add_luns(void *key, void *value, void *cookie)
{
	char *ent;
	result_t rc;
	int *seen = value;
	struct preseed_cb *data = cookie;
	struct zfcp_lun_devid *devid = key;

	/* Skip the preseed LUN entry if an error condition is pending */
	if (data->err)
		return;

	/*
	 * The preseed data tree contains LUNs for different zfcp host
	 * bus adapters.  Only those that match the specified host bus
	 * adapter in the lun tree are important here.
	 */
	if (ccw_cmp_devids(&devid->fcp_dev, &data->lt->hba->devid))
		return;

	zfcp_lun_devid_to_str(&ent, devid);

	rc = __add_lun(devid, data->lt);
	switch (rc) {
	case RESULT_OK:
		di_info("PRESEED Successfully added LUN: %s\n", ent);
		break;
	case RESULT_ZFCP_WWPN_NOT_FOUND:
		data->err = 1;
		di_error("Could not find target port: %s\n", ent);
		break;
	case RESULT_ZFCP_INVALID_LUN:
		data->err = 1;
		di_error("Could not add LUN, %s is not valid\n", ent);
		break;
	case RESULT_ZFCP_SCSI_NOT_FOUND:
		data->err = 1;
		di_error("Could not find SCSI device for %s\n", ent);
		break;
	default:
		data->err = 1;
		di_error("Could not add LUN %s: %d/%d\n", ent,
			 exit_code(rc), reason_code(rc));
		break;
	}

	free(ent);
	*seen = 1;	    /* Mark the preseed data entry as processed */
}

static enum state_wanted add_zfcp_luns(struct debconfclient *client,
				       struct zfcp_host *zfcp_host)
{
	enum state_wanted wanted;
	struct preseed_cb data;
	struct lun_tree lt = {
		.hba = zfcp_host,
		.luns = NULL,
	};

	/*
	 * Create a list of active LUNs for the specified zfcp host
	 * adapter.  This step is important for adapters using NPIV
	 * with automatic LUN scanning to include the LUNs in the
	 * device configuration.
	 *
	 * The list of active LUNs is presented to users and they
	 * can add or remove LUNs.  The list is then recreated and,
	 * if the user completes the configuration, the tree is added
	 * and written to the configuration file.
	 */
	lun_tree_populate(&lt);

	/*
	 * If automatic LUN scanning is allowed and the specified zfcp
	 * host adapter uses N_PORT ID virtualization, LUNs become
	 * automatically added.  Adding the SCSI devices takes some
	 * time to settle.
	 */
	if (zfcp_allow_lun_scan && zfcp_host->npiv) {
		if (zfcp_host->luns == NULL) {
			debconf_udev_progress(client, SCSI_ASYNC_TIMEOUT, 5);
			lun_tree_populate(&lt);
			zfcp_host->luns = lun_tree(&lt);
		}
		return WANT_NEXT;
	}

	/*
	 * Add LUNs to the zfcp host bus adapter that is specified in
	 * lun tree data structure.
	 */
	if (preseeded) {
		data.lt = &lt;
		data.err = 0;
		di_tree_foreach(preseed_data, preseed_add_luns, &data);
		wanted = data.err ? WANT_ERROR : WANT_NEXT;
	} else {
		wanted = add_zfcp_luns_user(client, &lt);
	}

	/*
	 * Replace the current LUN list with the updated LUN list before
	 * advacning to the next state.
	 *
	 * If the updated LUN list is empty, do not configure the FCP
	 * device and go back to (re-)select a FCP device for configuration.
	 * Note that an empty LUN list is considered an error condition if
	 * any LUNs were preseeded.
	 */
	if (wanted == WANT_NEXT) {
		if (zfcp_host->luns != NULL)
			di_tree_destroy(zfcp_host->luns);
		zfcp_host->luns = lun_tree(&lt);
		zfcp_host->write_luns = 1;
		if (!di_tree_size(zfcp_host->luns))
			wanted = preseeded ? WANT_ERROR : WANT_BACKUP;
	}

	/* Free a temporary LUN list */
	if (zfcp_host->luns == NULL)
		lun_tree_destroy(&lt);

	return wanted;
}

static void write_lun_config(void *key, void *value __unused, void *data)
{
	struct zfcp_lun_devid *lun = key;
	FILE *config = data;

	fprintf(config, "0x%016" PRIx64 ":0x%016" PRIx64 "\n",
		lun->wwpn, lun->lun);
}

static enum state_wanted write_config_file(struct zfcp_host *zfcp_host)
{
	result_t rc;
	FILE *config;
	char buf[PATH_MAX], *id;

	/* Build configuration file path using the adapter CCW device ID */
	rc = ccw_devid_to_str(&id, &zfcp_host->devid);
	if (rc != RESULT_OK)
		return WANT_ERROR;
	snprintf(buf, sizeof(buf), CONFIG_PREFIX "%s", id);
	free(id);

	/* Create configuration file */
	config = fopen(buf, "w");
	if (config == NULL) {
		di_error("Could not create configuration file: %s: %s\n",
			 buf, strerror(errno));
		return WANT_ERROR;
	}

	di_debug("WRITE CONFIG: %s\n", buf);

	/*
	 * Write the start of the zfcp device array.  For NPIV-enabled FCP
	 * devices, the zfcp device array might be empty depending on
	 * whether the user requested to write the LUN configuration.
	 *
	 * For non-NPIV FCP devices, the list contains the WWPN:LUN pairs
	 * which the user has configured.
	 */
	fprintf(config, "ZFCP_DEVICES=(\n");
	if (zfcp_host->write_luns)
		di_tree_foreach(zfcp_host->luns, write_lun_config, config);
	fprintf(config, ")\n");
	fflush(config);
	fclose(config);

	zfcp_host->configured = 1;

	return WANT_NEXT;
}



int main(void)
{
	int rc;
	enum state state;
	enum state_wanted wanted;
	static struct debconfclient *client;
	struct zfcp_host *selected_zfcp_host;

	/*
	 * Initializes the debian-installer library and
	 * create a debconf client handle.
	 */
	di_system_init("s390-zfcp");
	client = debconfclient_new();
	debconf_capb(client, DEBCONF_CAPS);

	/*
	 * Initialize the zfcp host adapeter list.
	 */
	zfcp_hosts = di_tree_new_full(di_compare_ccw_devids, NULL, di_free_zfcp_host);

	rc = 0;
	selected_zfcp_host = NULL;
	state = DETECT_ZFCP_HOSTS;
	while (1) {
		wanted = WANT_ERROR;
		switch (state) {
		case DETECT_ZFCP_HOSTS:
			wanted = detect_zfcp_hosts(client);
			break;
		case PRESEED:
			wanted = preseed_zfcp(client);
			break;
		case SELECT_ZFCP_HOST:
			if (preseeded)
				wanted = select_preseeded(&selected_zfcp_host);
			else
				wanted = select_zfcp_host(client, &selected_zfcp_host);
			break;
		case REMOVE_CONFIG:
			wanted = remove_config_file(client, selected_zfcp_host);
			break;
		case ENABLE:
			wanted = enable_zfcp_host(selected_zfcp_host);
			break;
		case ADD_ZFCP_LUNS:
			wanted = add_zfcp_luns(client, selected_zfcp_host);
			break;
		case WRITE:
			wanted = write_config_file(selected_zfcp_host);
			break;
		case BACKUP:
			rc = EXIT_BACKUP;
			goto out;
		case ERROR:
			rc = EXIT_FAILURE;
			goto out;
		case FINISH:
			if (zfcp_hosts)
				di_tree_destroy(zfcp_hosts);
			rc = EXIT_SUCCESS;
			goto out;
		}

		switch (wanted) {
		case WANT_NEXT:
			switch (state) {
			case DETECT_ZFCP_HOSTS:
				/*
				 * CIO_IGNORE:
				 *
				 * To support cio_ignore, use preceeding or
				 * ask the user for FCP devices to be removed
				 * from the blacklist.
				 */
				state = PRESEED;
				break;
			case PRESEED:
				state = SELECT_ZFCP_HOST;
				break;
			case SELECT_ZFCP_HOST:
				state = REMOVE_CONFIG;
				break;
			case REMOVE_CONFIG:
				state = ENABLE;
				break;
			case ENABLE:
				state = ADD_ZFCP_LUNS;
				break;
			case ADD_ZFCP_LUNS:
				state = WRITE;
				break;
			case WRITE:
				state = SELECT_ZFCP_HOST;
				break;
			default:
				/* Program is not in a valid state */
				state = WANT_ERROR;
				break;
			}
			break;
		case WANT_FINISH:
			state = FINISH;
			break;
		case WANT_BACKUP:
			/*
			 * If the user press the "Back" button, the debconf go
			 * returns CMD_GOBACK (== 30) and the WANT_BACKUP
			 * state should be set.  The WANT_BACKUP state should
			 * then go a step backward by setting the actual state
			 * variable to a previous state.
			 */
			switch (state) {
			case SELECT_ZFCP_HOST:
				state = BACKUP;
				break;
			case REMOVE_CONFIG:
				state = SELECT_ZFCP_HOST;
				break;
			case ADD_ZFCP_LUNS:
				state = SELECT_ZFCP_HOST;
				break;
			default:
				state = ERROR;
				break;
			}
			break;
		case WANT_ERROR:
			state = ERROR;
			break;
		};
	}
out:
	debconfclient_delete(client);
	return rc;
}
