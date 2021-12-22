#!/bin/sh

set -e

# Make sure binNMUs are properly versioned:
if echo "$VERSION" | grep -qs '\+b[0-9][0-9]*$'; then
	echo "E: wrong version detected, please check debian/README to see how binNMUs are versioned"
	exit 1
fi

VERSION=$(echo "$VERSION" | sed 's,\.\(b[0-9][0-9]*\)$,+\1,g')
MAJORVERSION=$(echo "$MAJOR_VERSION")

if [ "${MIRROR}x" = "x" ]; then \
    MIRROR=$(./get-mirror.sh) ;\
fi

RELEASE_FILE=$PWD/Release

destdir() {
	ARCHITECTURE=$1
	echo debian/debian-installer-$MAJORVERSION-netboot-$ARCHITECTURE/usr/lib/debian-installer/images/$MAJOR_VERSION
}

get_installer () {
	ARCHITECTURE=$1

	BASEDIR=$MIRROR/dists/$DISTRIBUTION/main/installer-$ARCHITECTURE/$VERSION/images

	DESTDIR=$PWD/$(destdir $ARCHITECTURE)

	SHA256SUM_FILE=$PWD/SHA256SUMS
	MD5SUM_FILE=$PWD/MD5SUMS

	wget -c $BASEDIR/SHA256SUMS -O $SHA256SUM_FILE
	check_file_against_release main/installer-$ARCHITECTURE/$VERSION/images/SHA256SUMS $SHA256SUM_FILE

	wget -c $BASEDIR/MD5SUMS -O $MD5SUM_FILE
	check_file_against_release main/installer-$ARCHITECTURE/$VERSION/images/MD5SUMS $MD5SUM_FILE

	mkdir -p $DESTDIR/$ARCHITECTURE
	BUILD_PWD=$(pwd)
	cd $DESTDIR/$ARCHITECTURE

        NETBOOTDIR_POSTFIX=
	if [ ${ARCHITECTURE:-} = "kfreebsd-amd64" -o ${ARCHITECTURE:-} = "kfreebsd-i386" ]; then
		NETBOOTDIR_POSTFIX=-$KFREEBSD_KERNEL_MAJOR
	fi

	if wget -c $BASEDIR/netboot${NETBOOTDIR_POSTFIX}/netboot.tar.gz -O netboot-text.tar.gz; then
		# Only check the stronger hash
		# check_md5sum_file "./netboot${NETBOOTDIR_POSTFIX}/netboot.tar.gz" netboot-text.tar.gz $MD5SUM_FILE
		check_sha256sum_file "./netboot${NETBOOTDIR_POSTFIX}/netboot.tar.gz" netboot-text.tar.gz $SHA256SUM_FILE
		unpack_installer $ARCHITECTURE/text netboot-text.tar.gz

		if wget -c $BASEDIR/netboot${NETBOOTDIR_POSTFIX}/gtk/netboot.tar.gz -O netboot-gtk.tar.gz; then
			# Only check the stronger hash
			# check_md5sum_file "./netboot${NETBOOTDIR_POSTFIX}/gtk/netboot.tar.gz" netboot-gtk.tar.gz $MD5SUM_FILE
			check_sha256sum_file "./netboot${NETBOOTDIR_POSTFIX}/gtk/netboot.tar.gz" netboot-gtk.tar.gz $SHA256SUM_FILE
			unpack_installer $ARCHITECTURE/gtk netboot-gtk.tar.gz
		else
			rm -f netboot-gtk.tar.gz
		fi
	else
		rm -f netboot-text.tar.gz

		FILES=$(wget $BASEDIR/MANIFEST -O - | awk '/netboot/ { print $1 }' | grep -v mini.iso)

		for FILE in $FILES; do
			mkdir -p $(dirname $FILE | sed -e 's|netboot/||')
			FILENAME=$(echo $FILE | sed -e 's|netboot/||' -e 's|//|/|')
			FILE_CLEAN=$(echo $FILE | sed -e 's|//|/|')
			wget -c $BASEDIR/$FILE_CLEAN -O $FILENAME
			# Only check the stronger hash
			# check_md5sum_file $FILE_CLEAN $FILENAME $MD5SUM_FILE
			check_sha256sum_file ./$FILE_CLEAN $FILENAME $SHA256SUM_FILE
		done

		if [ ${ARCHITECTURE:-} = "armel" ]; then
			rm -Rf $DESTDIR/$ARCHITECTURE/kirkwood/marvell/guruplug
			ln -sf sheevaplug $DESTDIR/$ARCHITECTURE/kirkwood/marvell/guruplug
		fi
	fi
	rm $SHA256SUM_FILE
	rm $MD5SUM_FILE
	cd $BUILD_PWD
}

check_file_against_release() {
	# Only check the stronger hash
	# grep '^ [a-f0-9]\{32\} ' $RELEASE_FILE | sed 's|^ \([a-f0-9]\{32\}\) .* \(.*\)$|\1  ./\2|' > $RELEASE_FILE.md5sums
	# check_md5sum_file ./$1 $(basename $2) $RELEASE_FILE.md5sums

	# Only check the stronger hash
	# grep '^ [a-f0-9]\{40\} ' $RELEASE_FILE | sed 's|^ \([a-f0-9]\{40\}\) .* \(.*\)$|\1  ./\2|' > $RELEASE_FILE.sha1sums
	# check_sha1sum_file ./$1 $(basename $2) $RELEASE_FILE.sha1sums

	# SHA256 _is_ the stronger hash
	grep '^ [a-f0-9]\{64\} ' $RELEASE_FILE | sed 's|^ \([a-f0-9]\{64\}\) .* \(.*\)$|\1  ./\2|' > $RELEASE_FILE.sha256sums
	check_sha256sum_file ./$1 $(basename $2) $RELEASE_FILE.sha256sums
}

check_sha256sum_file () {
	grep " $1$" $3 | sed -e "s| $1| $2|" > $2.sha256sum
	sha256sum -c $2.sha256sum
	rm $2.sha256sum
}

check_sha1sum_file () {
	grep " $1$" $3 | sed -e "s| $1| $2|" > $2.sha1sum
	sha1sum -c $2.sha1sum
	rm $2.sha1sum
}

check_md5sum_file () {
	grep " $1$" $3 | sed -e "s| $1| $2|" > $2.md5sum
	md5sum -c $2.md5sum
	rm $2.md5sum
}

get_di_built_using() {
	ARCHITECTURE=$1
	PACKAGES_FULL_PATH=main/binary-$ARCHITECTURE/Packages.xz
	DI_DEB=debian-installer_${VERSION}_${ARCHITECTURE}.deb

	wget -c $MIRROR/dists/$DISTRIBUTION/$PACKAGES_FULL_PATH -O $ARCHITECTURE.Packages.xz
	check_file_against_release $PACKAGES_FULL_PATH $ARCHITECTURE.Packages.xz

	unxz -f $ARCHITECTURE.Packages.xz

	# Check that the version is correct
	DI_VERSION=$(grep-dctrl -P debian-installer -X -s Version $ARCHITECTURE.Packages | sed -e 's/^Version: //')
	if [ "${DI_VERSION}" != "${VERSION}" ]; then
		echo "Building ${VERSION}, but ${DISTRIBUTION} has ${DI_VERSION}, failing the build"
		exit 1
	fi

	wget -c $MIRROR/pool/main/d/debian-installer/${DI_DEB}

	grep-dctrl -P debian-installer -X -s Sha256 $ARCHITECTURE.Packages \
		| sed -e "s/^S.*256: \([a-f0-9]\{64\}\)$/\1  .\/${DI_DEB}/" \
		> ${DI_DEB}.sha256sum

	sha256sum -c ${DI_DEB}.sha256sum
	rm ${DI_DEB}.sha256sum

	dpkg -I ${DI_DEB} | grep '^ Built-Using' \
		| sed -e 's/^ Built-Using: /d-i:built-using=/' \
		>> debian/debian-installer-$MAJORVERSION-netboot-$ARCHITECTURE.substvars
	rm ${DI_DEB}
}

unpack_installer () {
	DIRECTORY=$1
	FILE=$2

	cd ../$(dirname $DIRECTORY)
	mkdir -p $(basename $DIRECTORY)
	tar xfz $FILE -C $(basename $DIRECTORY)
	rm -f $FILE
}

wget -c $MIRROR/dists/$DISTRIBUTION/Release.gpg -O $RELEASE_FILE.gpg
wget -c $MIRROR/dists/$DISTRIBUTION/Release     -O $RELEASE_FILE

gpgv --keyring /usr/share/keyrings/debian-archive-keyring.gpg $RELEASE_FILE.gpg $RELEASE_FILE

trap "rm -f *.Packages* ; rm $RELEASE_FILE $RELEASE_FILE.gpg ; rm -f $RELEASE_FILE.* ;" 0

get_di_built_using $1
get_installer $1
