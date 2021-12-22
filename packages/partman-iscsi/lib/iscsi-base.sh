. /usr/share/debconf/confmodule

iscsi_login () {
	local portal discovery targets target
	local state=0
	while :; do
		case $state in
		    0)
			# Ask for target address.
			db_input critical partman-iscsi/login/address || true
			;;

		    1)
			# Validate target address.
			db_get partman-iscsi/login/address
			if [ -z "$RET" ]; then
				state=0
				continue
			fi

			portal="$RET"
			if [ "${portal%:*}" = "$portal" ]; then
				portal="$portal:3260"
			fi

			# Ask for initiator username.
			db_subst partman-iscsi/login/username PORTAL "$portal"
			db_input critical partman-iscsi/login/username || true
			;;

		    2)
			db_get partman-iscsi/login/username
			if [ "$RET" ]; then
				# Ask for initiator password.
				db_subst partman-iscsi/login/password PORTAL "$portal"
				db_input critical partman-iscsi/login/password || true
			fi
			;;

		    3)
			db_get partman-iscsi/login/username
			if [ "$RET" ]; then
				# Validate initiator password.
				db_get partman-iscsi/login/password
				if [ -z "$RET" ]; then
					db_capb align
					db_input critical partman-iscsi/login/empty_password || true
					db_go || true
					db_capb backup align
					state=2
					continue
				fi

				# Ask for target username.
				db_subst partman-iscsi/login/incoming_username PORTAL "$portal"
				db_input critical partman-iscsi/login/incoming_username || true
			fi
			;;

		    4)
			db_get partman-iscsi/login/username
			if [ "$RET" ]; then
				db_get partman-iscsi/login/incoming_username
				if [ "$RET" ]; then
					# Ask for target password.
					db_subst partman-iscsi/login/incoming_password PORTAL "$portal"
					db_input critical partman-iscsi/login/incoming_password || true
				fi
			fi
			;;

		    5)
			db_get partman-iscsi/login/username
			if [ "$RET" ]; then
				db_get partman-iscsi/login/incoming_username
				if [ "$RET" ]; then
					# Validate target password.
					db_get partman-iscsi/login/incoming_password
					if [ -z "$RET" ]; then
						db_capb align
						db_input critical partman-iscsi/login/empty_password || true
						db_go || true
						db_capb backup align
						state=4
						continue
					fi
				fi
			fi
			;;

		    6)
			# Set up discovery authentication. We have to do a
			# spurious no-op sendtargets in order to create the
			# discovery record.
			iscsiadm -m discovery --type sendtargets --portal "$portal" >/dev/null 2>&1 || true
			db_get partman-iscsi/login/username
			if [ "$RET" ]; then
				iscsiadm -m discovery --portal "$portal" -o update -n discovery.sendtargets.auth.authmethod -v CHAP
				iscsiadm -m discovery --portal "$portal" -o update -n discovery.sendtargets.auth.username -v "$RET"
				db_get partman-iscsi/login/password
				iscsiadm -m discovery --portal "$portal" -o update -n discovery.sendtargets.auth.password -v "$RET"
				db_get partman-iscsi/login/incoming_username
				if [ "$RET" ]; then
					iscsiadm -m discovery --portal "$portal" -o update -n discovery.sendtargets.auth.username_in -v "$RET"
					db_get partman-iscsi/login/incoming_password
					iscsiadm -m discovery --portal "$portal" -o update -n discovery.sendtargets.auth.password_in -v "$RET"
				else
					iscsiadm -m discovery --portal "$portal" -o update -n discovery.sendtargets.auth.username_in -v ''
				fi
			else
				iscsiadm -m discovery --portal "$portal" -o update -n discovery.sendtargets.auth.authmethod -v None
			fi

			# Discover targets.
			if ! discovery="$(iscsiadm -m discovery --type sendtargets \
						   --portal "$portal")"; then
				db_capb align
				db_subst partman-iscsi/login/no_targets \
					PORTAL "$portal"
				db_input critical partman-iscsi/login/no_targets || true
				db_go || true
				db_capb backup align
				state=0
				continue
			fi

			# Ask for targets.
			targets="$(echo "$discovery" | sort -t' ' -k2 -u | \
				while read portal target; do
					printf ', %s' "$(echo "$target" | sed 's/,/\\,/g')"
				done | sed 's/^, //')"
			db_subst partman-iscsi/login/targets PORTAL "$portal"
			db_subst partman-iscsi/login/targets TARGETS "$targets"
			if db_get partman-iscsi/login/all_targets && \
			   [ "$RET" = true ]; then
				db_set partman-iscsi/login/targets "$targets"
			else
				db_input critical partman-iscsi/login/targets || true
			fi
			;;

		    7)
			# Log into targets.
			db_get partman-iscsi/login/targets
			targets="$RET"
			while [ "$targets" ]; do
				target="${targets%%, *}"

				# Set up node authentication.
				iscsiadm -m node --portal "$portal" --target "$target" -o new >/dev/null 2>&1 || true
				db_get partman-iscsi/login/username
				if [ "$RET" ]; then
					iscsiadm -m node --portal "$portal" --target "$target" -o update -n node.session.auth.authmethod -v CHAP
					iscsiadm -m node --portal "$portal" --target "$target" -o update -n node.session.auth.username -v "$RET"
					db_get partman-iscsi/login/password
					iscsiadm -m node --portal "$portal" --target "$target" -o update -n node.session.auth.password -v "$RET"
					db_get partman-iscsi/login/incoming_username
					if [ "$RET" ]; then
						iscsiadm -m node --portal "$portal" --target "$target" -o update -n node.session.auth.username_in -v "$RET"
						db_get partman-iscsi/login/incoming_password
						iscsiadm -m node --portal "$portal" --target "$target" -o update -n node.session.auth.password_in -v "$RET"
					fi
				else
					iscsiadm -m node --portal "$portal" --target "$target" -o update -n node.session.auth.authmethod -v None
				fi

				if ! iscsiadm -m node --portal "$portal" \
					      --target "$target" --login; then
					db_capb align
					db_subst partman-iscsi/login/failed \
						PORTAL "$portal"
					db_subst partman-iscsi/login/failed \
						TARGET "$target"
					db_input critical partman-iscsi/login/failed || true
					db_go || true
					db_capb backup align
				fi

				if [ "$target" = "$targets" ]; then
					targets=
				else
					targets="${targets#*, }"
				fi
			done

			# Clear passwords from the database.
			db_set partman-iscsi/login/password ''
			db_set partman-iscsi/login/incoming_password ''
			;;

		    *)
			break
			;;
		esac

		if db_go; then
			state=$(($state + 1))
		else
			state=$(($state - 1))
		fi
	done

	if [ "$state" = -1 ]; then
		return 1
	fi
}
