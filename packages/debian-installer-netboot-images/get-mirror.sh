#!/bin/sh

set -e

# DISTRIBUTION needs to be set

# Inspired by debian-installer's gen-sources.list.udeb

OLDIFS="$IFS"
NL="
"
test_url() {
	local url=$1
	if wget -q --spider $url >/dev/null 2>&1; then
		return 0
	fi
	return 1
}

get_mirrors() {
	local file
	for file in $@; do
		[ -s $file ] || continue
		grep '^deb[[:space:]]' $file | \
		   grep -v '^deb[[:space:]]\+cdrom:' | \
		   sed 's,^deb \[[^]]*\] ,deb ,' | \
		   grep -v 'security.debian.org' | \
		   grep '[[:space:]]main' | \
		   awk '{print $1 " " $2}' | \
		   sed 's,^deb file,deb copy,' | \
		   sed 's,/* *$,,'
	done
}
# Cache the apt configuration dump, with only the needed namespace
APT_CONFIG=$(apt-config dump | grep '^Dir::Etc')

# Get the system apt directory
APT_DIR_ETC=$(echo "$APT_CONFIG" | sed -n -e 's/^Dir::Etc *\"\(.*\)\";$/\1/p')

# Fetch APT's sources.list
APT_SOURCELIST=$(echo "$APT_CONFIG" | sed -n -e 's/^Dir::Etc::sourcelist *\"\(.*\)\";$/\1/p')

# Fetch APT' sources.list.d
APT_SOURCEPARTS=$(echo "$APT_CONFIG" | sed -n -e 's/^Dir::Etc::sourceparts *\"\(.*\)\";$/\1/p')

MIRRORS="$(get_mirrors /$APT_DIR_ETC/$APT_SOURCELIST /$APT_DIR_ETC/$APT_SOURCEPARTS/*)"

# Remove any duplicates at the end of the loop (the perl statement)
IFS="$NL"
for mirror in $MIRRORS; do
	IFS="$OLDIFS"

	tmirror="$(echo $mirror | sed -r "s/^deb //")"
	# We should also check that d-i is available, but that's more complex.
	# Settle for just checking the suite/codename for now.
	if echo "$mirror" | grep -Eq "^deb (f|ht)tp"; then
		if test_url $tmirror/dists/$DISTRIBUTION/Release; then
			echo "$tmirror"
		else
			echo "WARNING: mirror '$tmirror' appears to be invalid; skipping" >&2
		fi
		if [ "$USE_PROPOSED_UPDATES" = 1 ] &&
		   test_url $tmirror/dists/$DISTRIBUTION-proposed-updates/Release; then
			echo "$tmirror"
			echo "INFO: using '$tmirror' for $DISTRIBUTION-proposed-updates" >&2
		fi
	else
		echo "$tmirror"
	fi

done | perl -ne 'print unless $seen{$_}; $seen{$_}=1' | head -n1
