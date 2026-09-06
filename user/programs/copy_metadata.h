#ifndef RIXURIOS_COPY_METADATA_H
#define RIXURIOS_COPY_METADATA_H

#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t copy_meta_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static void copy_meta_error(const char *tool, const char *operation) {
    (void)write(2, tool, copy_meta_length(tool));
    const char *prefix = ": metadata ";
    (void)write(2, prefix, copy_meta_length(prefix));
    (void)write(2, operation, copy_meta_length(operation));
    const char *suffix = " failed\n";
    (void)write(2, suffix, copy_meta_length(suffix));
}

static int copy_metadata(const char *tool, const char *source, const char *destination) {
    rix_stat_t source_stat;
    rix_acl_t source_acl;
    if (stat(source, &source_stat) != 0) {
        copy_meta_error(tool, "stat");
        return -1;
    }
    if (getacl(source, &source_acl) != 0) {
        copy_meta_error(tool, "ACL read");
        return -1;
    }
    if (chown(destination, source_stat.uid, source_stat.gid) != 0) {
        copy_meta_error(tool, "chown");
        return -1;
    }
    if (chmod(destination, source_stat.mode & 07777u) != 0) {
        copy_meta_error(tool, "chmod");
        return -1;
    }
    if (source_acl.user != RIX_ACL_NONE || source_acl.group != RIX_ACL_NONE ||
        source_acl.mask != RIX_ACL_PERM_MASK) {
        if (setacl(destination, &source_acl) != 0) {
            copy_meta_error(tool, "ACL write");
            return -1;
        }
    } else if (clearacl(destination) != 0) {
        copy_meta_error(tool, "ACL clear");
        return -1;
    }
    return 0;
}

#endif
