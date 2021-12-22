/*
 * Interface for s390 device helper functions.
 *
 * Copyright IBM Corp. 2015
 * Author(s):	Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *		Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#ifndef _S390_DEV_H
#define _S390_DEV_H

#include <inttypes.h>
#include <string.h>

#define PATH_CCW_BUS		"/sys/bus/ccw"
#define PATH_CCWGROUP_BUS	"/sys/bus/ccwgroup"
#define	PATH_PROC		"/proc"

#define	ZFCP_MOD_NAME		"zfcp"
#define ZFCP_CCWDRV_NAME	"zfcp"
#define ZFCP_LUN_NAME		"zfcp-lun"
#define SCSI_ATTR_PREFIX	"scsi_dev"

#define CSSID_MAX	0x00ff
#define SSID_MAX	0x000f
#define DEVNO_MAX	0xffff

/**
 * ccw_devid - CCW device ID
 *
 * @cssid: Channel Subsystem ID
 * @ssid:  Subchannel Set ID
 * @devno: Device number
 */
struct ccw_devid {
	unsigned int cssid:8;
	unsigned int ssid:8;
	unsigned int devno:16;
} __attribute__((packed));

struct zfcp_lun_devid {
	struct ccw_devid fcp_dev;
	uint64_t wwpn;
	uint64_t lun;
} __attribute__ ((packed));

struct scsi_hctl_devid {
	unsigned int host;
	unsigned int channel;
	unsigned int target;
	uint64_t lun;
};

/* Program exit codes. */
typedef enum {
	EXIT_OK			= 0,  /* Program finished successfully */

	/* Usage related */
	EXIT_INVALID_ID		= 10, /* Invalid device ID specified */
	EXIT_INCOMPLETE_ID	= 11, /* Incomplete device ID specified */

	/* Run-time related */
	EXIT_RUNTIME_ERROR	= 15, /* A run-time error occurred */
	EXIT_OUT_OF_MEMORY	= 22, /* Not enough available memory */

	/* zfcp-related */
	EXIT_ZFCP_FCP_NOT_FOUND  = 23, /* FCP device not found */
	EXIT_ZFCP_WWPN_NOT_FOUND = 25, /* WWPN not found */
	EXIT_ZFCP_INVALID_LUN	 = 26, /* Invalid LUN specified */
	EXIT_ZFCP_SCSI_NOT_FOUND = 27, /* SCSI device not found */

} exit_code_t;


/*
 * Function result code.
 *
 * A result code consists of a reason code and a desired program
 * exit code.
 */
#define DEFINE_RESULT(exit_code, reason)	(((exit_code) << 16) | reason)
typedef enum {
	RESULT_OK	  = DEFINE_RESULT(EXIT_OK, 0),

	/* Usage related */
	RESULT_INVALID_ID     = DEFINE_RESULT(EXIT_INVALID_ID, 0),
	RESULT_INVALID_HEXNUM = DEFINE_RESULT(EXIT_INVALID_ID, 2),

	/* CCW device ID related */
	RESULT_INVALID_DEVID_FMT   = DEFINE_RESULT(EXIT_INVALID_ID, 20),
	RESULT_INVALID_CSSID_RANGE = DEFINE_RESULT(EXIT_INVALID_ID, 21),
	RESULT_INVALID_SSID_RANGE  = DEFINE_RESULT(EXIT_INVALID_ID, 22),
	RESULT_INVALID_DEVNO_RANGE = DEFINE_RESULT(EXIT_INVALID_ID, 23),

	/* FCP related */
	RESULT_INVALID_LUN_FMT	   = DEFINE_RESULT(EXIT_INVALID_ID, 24),
	RESULT_INVALID_WWPN_LEN	   = DEFINE_RESULT(EXIT_INVALID_ID, 25),
	RESULT_INVALID_WWPN	   = DEFINE_RESULT(EXIT_INVALID_ID, 26),
	RESULT_INVALID_LUN_LEN	   = DEFINE_RESULT(EXIT_INVALID_ID, 27),
	RESULT_INVALID_LUN	   = DEFINE_RESULT(EXIT_INVALID_ID, 28),
	RESULT_ZFCP_FCP_NOT_FOUND  = DEFINE_RESULT(EXIT_ZFCP_FCP_NOT_FOUND, 29),
	RESULT_ZFCP_WWPN_NOT_FOUND = DEFINE_RESULT(EXIT_ZFCP_WWPN_NOT_FOUND, 30),
	RESULT_ZFCP_INVALID_LUN	   = DEFINE_RESULT(EXIT_ZFCP_INVALID_LUN, 31),
	RESULT_ZFCP_SCSI_NOT_FOUND = DEFINE_RESULT(EXIT_ZFCP_SCSI_NOT_FOUND, 32),

	/* Run-time related */
	RESULT_RUNTIME_ERROR	   = DEFINE_RESULT(EXIT_RUNTIME_ERROR, 0),
	RESULT_OUT_OF_MEMORY	   = DEFINE_RESULT(EXIT_OUT_OF_MEMORY, 0),

} result_t;

static inline int reason_code(result_t result)
{
	return result & 0xffff;
}

static inline exit_code_t exit_code(result_t result)
{
	return result >> 16;
}

char *path_get(const char *, ...);
char *path_get_sys_module(const char *);
char *path_get_sys_module_param(const char *, const char *);
char *path_get_sys_class(const char *, const char *);
char *path_get_ccw_device(const char *, const char *);
char *path_get_ccw_device_attr(const char *, const char *, const char *);
char *path_get_ccw_devices(const char *);
char *path_get_sys_bus_dev(const char *, const char *);
char *path_get_sys_bus_drv(const char *, const char *);
char *path_get_zfcp_lun_dev(const struct zfcp_lun_devid *);
char *path_get_zfcp_port_dev(const struct zfcp_lun_devid *);
char *path_get_scsi_hctl_dev(const char *);

result_t path_for_each(const char *,
			  result_t (*callback)(const char *, const char *,
						  void *), void *);
/*
 * Iterate through a directory opened with opendir().
 *
 * @dir:    the DIR pointer, returned from opendir()
 * @de_rp:  pointer to a caller-allocated struct dirent
 *	    to ensure thread-safety
 * @dep:    struct dirent pointer used as iterator
 */
#define dir_for_each_entry_safe(_dir, _de_rp, _dep, _err)	\
	 for (_err = readdir_r(_dir, _de_rp, &_dep);		\
	      !_err && _dep != NULL;				\
	      _err = readdir_r(_dir, _de_rp, &_dep))

char *misc_read_text_file(const char *path, int chomp);
result_t misc_write_text_file(const char *path, const char *text);
char *misc_readlink(const char *path);
int dir_exists(const char *path);
char *strtrim(char *s);

result_t ccw_parse_devid(struct ccw_devid *devid_ptr, const char *id);
result_t ccw_devid_to_str(char **id, const struct ccw_devid *devid);
result_t zfcp_lun_parse_devid(struct zfcp_lun_devid *devid_ptr, const char *id);
result_t zfcp_lun_to_str(char **id, const struct zfcp_lun_devid *lun);
result_t zfcp_lun_devid_to_str(char **id, const struct zfcp_lun_devid *devid);
int scsi_hctl_parse_devid(struct scsi_hctl_devid *id, const char *str);
void byte_swap(uint8_t *addr, unsigned int *pos, unsigned num);
uint64_t scsi_lun_to_fcp_lun(uint64_t lun);


static inline int ccw_cmp_devids(const struct ccw_devid *a, const struct ccw_devid *b)
{
	return memcmp(a, b, sizeof(*a));
}

static inline int zfcp_lun_cmp_devids(const struct zfcp_lun_devid *a,
				      const struct zfcp_lun_devid *b)
{
	return memcmp(a, b, sizeof(*a));
}

/* Helper functions */
static inline int starts_with(const char *str, const char *s)
{
	size_t len;

	len = strlen(s);
	return !strncmp(str, s, len);
}

/* /a/b/ -> /b/ */
static inline char *skip_comp(const char *s)
{
	return s != NULL ? strchr(s + 1, '/') : NULL;
}

/* zfcp-related helper functions */
int zfcp_check_allow_lun_scan(int *enabled);
result_t zfcp_dev_check_npiv(const char *ccwid, int *enabled);
result_t zfcp_lun_add(const struct zfcp_lun_devid *lun);
result_t scsi_device_remove(const char *hctl);
result_t zfcp_lun_remove(const struct zfcp_lun_devid *lun);
int zfcp_for_each_scsi_dev(int (*cb)(const char *, const char *, const char *,
			   const char*, void *data), void *data);

#endif /* _S390_DEV_H */
