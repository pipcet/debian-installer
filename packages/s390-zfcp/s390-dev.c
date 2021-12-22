/*
 * Helper functions to manage s390 devices
 *
 * Copyright IBM Corp. 2015
 * Author(s):	Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *		Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include "s390-dev.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/**
 * ccw_parse_devid - Parse a string into a ccw_devid
 *
 * @devid_ptr: Target ccw_devid or NULL
 * @id: String to parse
 *
 * Return %RESULT_OK on success, a corresponding result code otherwise.
 */
result_t ccw_parse_devid(struct ccw_devid *devid_ptr, const char *id)
{
	unsigned int cssid, ssid, devno;
	char d;
	result_t result = RESULT_INVALID_ID;

	if (sscanf(id, "%8x %c", &devno, &d) == 1) {
		if (strcasecmp(id, "0x") == 0) {
			result = RESULT_INVALID_HEXNUM;
			goto out;
		}
		cssid = 0;
		ssid = 0;
	} else if (sscanf(id, "%8x.%8x.%8x %c", &cssid, &ssid, &devno, &d) != 3) {
		result = RESULT_INVALID_DEVID_FMT;
		goto out;
	}

	if (cssid > CSSID_MAX) {
		result = RESULT_INVALID_CSSID_RANGE;
		goto out;
	}
	if (ssid > SSID_MAX) {
		result = RESULT_INVALID_SSID_RANGE;
		goto out;
	}
	if (devno > DEVNO_MAX) {
		result = RESULT_INVALID_DEVNO_RANGE;
		goto out;
	}

	result = RESULT_OK;
	if (devid_ptr) {
		devid_ptr->cssid = cssid;
		devid_ptr->ssid = ssid;
		devid_ptr->devno = devno;
	}
out:
	return result;
}

result_t ccw_devid_to_str(char **id, const struct ccw_devid *devid)
{
	int rc;

	rc = asprintf(id, "%x.%x.%04x", devid->cssid, devid->ssid, devid->devno);

	return rc == -1 ? RESULT_OUT_OF_MEMORY : RESULT_OK;
}

/*
 * Parse zfcp luns
 *
 * Format: <wwpn>:<lun>
 * wwpn: Target port World Wide Port Name: aaaaaaaaaaaaaaaa or
 *	 0xaaaaaaaaaaaaaaaa
 * lun: FCP LUN: aaaaaaaaaaaaaaaa or 0xaaaaaaaaaaaaaaaa
 *
 * @lun_ptr:  Target zfcp_lun or NULL
 * @id:	      String to parse
 *
 * Return %RESULT_OK on success, a corresponding result code otherwise.
 */
result_t zfcp_lun_parse_devid(struct zfcp_lun_devid *devid_ptr,
			      const char *id)
{
	result_t result = EXIT_INVALID_ID;
	char *copy, *curr, *delim;
	struct ccw_devid fcp_dev;
	uint64_t wwpn, lun;
	char dummy;

	copy = strdup(id);
	if (copy == NULL)
		return RESULT_OUT_OF_MEMORY;
	curr = copy;

	/* Parse FCP device ID. */
	delim = strchr(curr, ':');
	if (!delim) {
		result = RESULT_INVALID_LUN_FMT;
		goto out;
	}
	*delim = 0;

	if (ccw_parse_devid(&fcp_dev, copy) != RESULT_OK)
		goto out;

	curr = delim + 1;

	/* Parse WWPN. */
	delim = strchr(curr, ':');
	if (!delim) {
		result = RESULT_INVALID_LUN_FMT;
		goto out;
	}
	*delim = 0;
	if (strncasecmp(curr, "0x", 2) == 0)
		curr += 2;
	if (strlen(curr) != 16) {
		result = RESULT_INVALID_WWPN_LEN;
		goto out;
	}
	if (sscanf(curr, "%16" SCNx64 " %c", &wwpn, &dummy) != 1) {
		result = RESULT_INVALID_WWPN;
		goto out;
	}
	curr = delim + 1;

	/* Parse LUN. */
	if (strncasecmp(curr, "0x", 2) == 0)
		curr += 2;
	if (strlen(curr) != 16) {
		result = RESULT_INVALID_LUN_LEN;
		goto out;
	}
	if (sscanf(curr, "%16" SCNx64 " %c", &lun, &dummy) != 1) {
		result = RESULT_INVALID_LUN;
		goto out;
	}

	if (devid_ptr) {
		devid_ptr->fcp_dev = fcp_dev;
		devid_ptr->wwpn = wwpn;
		devid_ptr->lun = lun;
	}
	result = RESULT_OK;

out:
	free(copy);

	return result;
}

result_t zfcp_lun_to_str(char **id, const struct zfcp_lun_devid *lun)
{
	if (asprintf(id, "0x%016" PRIx64 ":0x%016" PRIx64, lun->wwpn, lun->lun) == -1)
		return RESULT_OUT_OF_MEMORY;
	return RESULT_OK;
}

/* Return a newly allocated string representing the specified device ID. */
result_t zfcp_lun_devid_to_str(char **id, const struct zfcp_lun_devid *devid)
{
	int rc;

	rc = asprintf(id, "%x.%x.%04x:0x%016" PRIx64 ":0x%016" PRIx64,
		      devid->fcp_dev.cssid, devid->fcp_dev.ssid,
		      devid->fcp_dev.devno, devid->wwpn, devid->lun);
	return rc == -1 ? RESULT_OUT_OF_MEMORY : RESULT_OK;
}


int scsi_hctl_parse_devid(struct scsi_hctl_devid *id, const char *str)
{
	unsigned int host, channel, target;
	uint64_t lun;
	char dummy;

	if (sscanf(str, "%u:%u:%u:%" SCNu64 " %c", &host, &channel, &target,
		   &lun, &dummy) != 4)
		return 1;

	if (id) {
		id->host = host;
		id->channel = channel;
		id->target = target;
		id->lun = lun;
	}

	return 0;
}

/* Swap @len bytes in @addr according to @pos. */
void byte_swap(uint8_t *addr, unsigned int *pos, unsigned num)
{
	unsigned int i;
	uint8_t s;

	for (i = 0; i < num; i++) {
		s = addr[i];
		addr[i] = addr[pos[i]];
		addr[pos[i]] = s;
	}
}

static unsigned int lun_swap[] = { 6, 7, 4, 5 };

uint64_t scsi_lun_to_fcp_lun(uint64_t lun)
{
	byte_swap((uint8_t *) &lun, lun_swap, ARRAY_SIZE(lun_swap));

	return lun;
}
