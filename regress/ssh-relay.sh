#	Placed in the Public Domain.

tid="transparent ssh relay"

RELAYPORT=`expr $PORT + 73`
RELAYCFG=$OBJ/sshd_relay
RELAYCLIENT=$OBJ/ssh_relay
RELAYPID=$OBJ/pidfile-relay
RELAYLOG=$OBJ/sshd-relay.log
IN=$OBJ/relay.in
OUT=$OBJ/relay.out
relay_pid=

cleanup_relay() {
	[ -z "$relay_pid" ] || kill "$relay_pid" >/dev/null 2>&1
	rm -f "$RELAYPID"
}
trap cleanup_relay EXIT

# Configuration validation and defaults.
${SSHD} -t -f $OBJ/sshd_config \
	-oSSHRelayTarget=127.0.0.1:$PORT \
	-oSSHRelayConnectTimeout=10s || fatal "valid relay config rejected"
${SSHD} -t -f $OBJ/sshd_config \
	-oSSHRelayTarget='[::1]:22' || fatal "valid IPv6 relay target rejected"
for bad in 127.0.0.1 user@127.0.0.1:22 127.0.0.1:0 '[::1' ; do
	${SSHD} -t -f $OBJ/sshd_config -oSSHRelayTarget="$bad" \
	    >/dev/null 2>&1 && fatal "invalid relay target accepted: $bad"
done
cp $OBJ/sshd_config $OBJ/sshd_relay_match
cat >>$OBJ/sshd_relay_match <<EOF
Match User __ssh_relay_config_test__
	SSHRelayTarget 127.0.0.1:$PORT
EOF
${SSHD} -t -f $OBJ/sshd_relay_match >/dev/null 2>&1 &&
	fatal "SSHRelayTarget accepted inside Match"
${SSHD} -T -f $OBJ/sshd_config | \
	grep '^sshrelaytarget none$' >/dev/null ||
	fatal "relay target default is not none"
${SSHD} -T -f $OBJ/sshd_config | \
	grep '^sshrelayconnecttimeout 10$' >/dev/null ||
	fatal "relay timeout default is not 10"

# B is a normal sshd. A is a second, dedicated relay-mode instance.
start_sshd
sed -e "s/^\([[:space:]]*Port[[:space:]]*\).*/\1$RELAYPORT/" \
	-e "s|^\([[:space:]]*PidFile[[:space:]]*\).*|\1$RELAYPID|" \
	$OBJ/sshd_config >$RELAYCFG
cat >>$RELAYCFG <<EOF
SSHRelayTarget 127.0.0.1:$PORT
SSHRelayConnectTimeout 10s
EOF
sed "s/^\([[:space:]]*Port[[:space:]]*\).*/\1$RELAYPORT/" \
	$OBJ/ssh_config >$RELAYCLIENT

${SSHD} -t -f $RELAYCFG || fatal "relay sshd config rejected"
$SUDO env SSH_SK_HELPER="$SSH_SK_HELPER" ${TEST_SSH_SSHD_ENV} \
	${SSHD} -f $RELAYCFG -E$RELAYLOG
i=0
while [ ! -f "$RELAYPID" -a $i -lt 10 ]; do
	i=`expr $i + 1`
	sleep 1
done
test -f "$RELAYPID" || fatal "relay sshd did not start"
relay_pid=`cat "$RELAYPID"`

result=`${SSH} -F $RELAYCLIENT somehost 'printf relay-ok'` ||
	fatal "remote command through relay failed"
test "x$result" = "xrelay-ok" || fatal "bad remote command output"

dd if=/dev/urandom of=$IN bs=32768 count=32 2>/dev/null ||
	fatal "could not create relay input"
${SSH} -F $RELAYCLIENT somehost cat <$IN >$OUT ||
	fatal "large transfer through relay failed"
cmp $IN $OUT || fatal "relay changed transferred bytes"

grep 'Relay connection.*established' $RELAYLOG >/dev/null ||
	fatal "relay establishment was not logged"
