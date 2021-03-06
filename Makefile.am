DISTCHECK_CONFIGURE_FLAGS = --with-ldb-lib-dir="$$dc_install_base"/lib/ldb

SUBDIRS = po src/man

# Some old versions of automake don't define builddir
builddir ?= .

DOXYGEN = @DOXYGEN@

DISTSETUPOPTS =
if HAVE_DEBIAN
DISTSETUPOPTS += --install-layout=deb
endif

sssdlibexecdir = $(libexecdir)/sssd
sssdlibdir = $(libdir)/sssd
ldblibdir = @ldblibdir@
if BUILD_KRB5_LOCATOR_PLUGIN
krb5plugindir = @krb5pluginpath@
endif
sssdconfdir = $(sysconfdir)/sssd
sssdapiplugindir = $(sssdconfdir)/sssd.api.d
dbusintrospectdir = $(datarootdir)/sssd/introspect
localedir = @localedir@
nsslibdir = @nsslibdir@
pamlibdir = @pammoddir@
libsss_sudoplugindir = @sudo_pluginpath@

dbpath = @dbpath@
pluginpath = @pluginpath@
pidpath = @pidpath@
pipepath = @pipepath@
initdir = @initdir@
systemdunitdir = @systemdunitdir@
logpath = @logpath@
pubconfpath = @pubconfpath@
pkgconfigdir = $(libdir)/pkgconfig
krb5rcachedir = @krb5rcachedir@

AM_CFLAGS =
if WANT_AUX_INFO
    AM_CFLAGS += -aux-info $@.X
endif
if HAVE_GCC
    AM_CFLAGS += -Wall -Wshadow -Wstrict-prototypes -Wpointer-arith \
                 -Wcast-qual -Wcast-align -Wwrite-strings \
                 -Werror-implicit-function-declaration
endif

dist_pkgconfig_DATA =

ACLOCAL_AMFLAGS = -I m4 -I .

sbin_PROGRAMS = \
    sssd \
    sss_useradd \
    sss_userdel \
    sss_groupadd \
    sss_groupdel \
    sss_usermod \
    sss_groupmod \
    sss_groupshow \
    sss_cache \
    sss_debuglevel

sssdlibexec_PROGRAMS = \
    sssd_nss \
    sssd_pam \
    sssd_be \
    krb5_child \
    ldap_child \
    proxy_child \
    sssd_sudo

if HAVE_CHECK
    non_interactive_check_based_tests = \
        sysdb-tests \
        strtonum-tests \
        resolv-tests \
        krb5-utils-tests \
        check_and_open-tests \
        files-tests \
        refcount-tests \
        fail_over-tests \
        find_uid-tests \
        auth-tests \
        ipa_ldap_opt-tests \
        simple_access-tests \
        crypto-tests \
        util-tests \
        debug-tests \
        ipa_hbac-tests
endif

check_PROGRAMS = \
    stress-tests \
    $(non_interactive_check_based_tests)

PYTHON_TESTS =

if BUILD_PYTHON_BINDINGS
PYTHON_TESTS += $(srcdir)/src/config/SSSDConfigTest.py \
                $(srcdir)/src/tests/pyhbac-test.py
endif

TESTS = \
    $(PYTHON_TESTS) \
    $(non_interactive_check_based_tests)

sssdlib_LTLIBRARIES = \
    libsss_ldap.la \
    libsss_krb5.la \
    libsss_proxy.la \
    libsss_ipa.la \
    libsss_simple.la

ldblib_LTLIBRARIES = \
    memberof.la

if BUILD_KRB5_LOCATOR_PLUGIN
krb5plugin_LTLIBRARIES = \
    sssd_krb5_locator_plugin.la
endif

noinst_LTLIBRARIES = \
    libsss_crypt.la

if HAVE_NSS
    SSS_CRYPT_SOURCES = src/util/crypto/nss/nss_sha512crypt.c \
                        src/util/crypto/nss/nss_obfuscate.c \
                        src/util/crypto/nss/nss_util.c
    SSS_CRYPT_CFLAGS = $(NSS_CFLAGS)
    SSS_CRYPT_LIBS = $(NSS_LIBS)
else
    SSS_CRYPT_SOURCES = src/util/crypto/libcrypto/crypto_sha512crypt.c \
                        src/util/crypto/libcrypto/crypto_obfuscate.c
    SSS_CRYPT_CFLAGS = $(CRYPTO_CFLAGS)
    SSS_CRYPT_LIBS = $(CRYPTO_LIBS)
endif

libsss_crypt_la_SOURCES = \
    $(SSS_CRYPT_SOURCES)
libsss_crypt_la_CFLAGS = \
    $(SSS_CRYPT_CFLAGS) \
    $(DHASH_CFLAGS)
libsss_crypt_la_LIBADD = \
    $(SSS_CRYPT_LIBS)

if BUILD_PYTHON_BINDINGS
pyexec_LTLIBRARIES = \
    pysss.la \
    pyhbac.la
endif

dist_noinst_SCRIPTS = \
    $(EXTRA_SCRIPTS) \
    src/config/setup.py \
    src/config/ipachangeconf.py \
    src/config/SSSDConfig.py \
    src/config/SSSDConfigTest.py \
    src/config/sssd_upgrade_config.py \
    src/tests/pyhbac-test.py

dist_noinst_DATA = \
    src/config/testconfigs/sssd-valid.conf \
    src/config/testconfigs/noparse.api.conf \
    src/config/testconfigs/sssd-noversion.conf \
    src/config/testconfigs/sssd-badversion.conf \
    src/config/testconfigs/sssd-invalid.conf \
    src/config/testconfigs/sssd-invalid-badbool.conf

###############################
# Global compilation settings #
###############################

AM_CPPFLAGS = \
    -Wall \
    -Iinclude \
    -I.. \
    -I$(srcdir)/include \
    -I$(srcdir)/src/sss_client \
    -I$(srcdir)/src \
    -Iinclude \
    -I. \
    $(POPT_CFLAGS) \
    $(TALLOC_CFLAGS) \
    $(TDB_CFLAGS) \
    $(TEVENT_CFLAGS) \
    $(LDB_CFLAGS) \
    $(DBUS_CFLAGS) \
    $(PCRE_CFLAGS) \
    $(COLLECTION_CFLAGS) \
    $(INI_CONFIG_CFLAGS) \
    $(DHASH_CFLAGS) \
    $(LIBNL_CFLAGS) \
    $(OPENLDAP_CFLAGS) \
    -DLIBDIR=\"$(libdir)\" \
    -DVARDIR=\"$(localstatedir)\" \
    -DSHLIBEXT=\"$(SHLIBEXT)\" \
    -DSSSD_LIBEXEC_PATH=\"$(sssdlibexecdir)\" \
    -DSSSD_INTROSPECT_PATH=\"$(dbusinstropectdir)\" \
    -DSSSD_CONF_DIR=\"$(sssdconfdir)\" \
    -DSSS_NSS_SOCKET_NAME=\"$(pipepath)/nss\" \
    -DSSS_PAM_SOCKET_NAME=\"$(pipepath)/pam\" \
    -DSSS_SUDO_SOCKET_NAME=\"$(pipepath)/sudo\" \
    -DSSS_PAM_PRIV_SOCKET_NAME=\"$(pipepath)/private/pam\" \
    -DSSS_SUDO_PRIV_SOCKET_NAME=\"$(pipepath)/private/sudo\" \
    -DLOCALEDIR=\"$(localedir)\"

EXTRA_DIST = build/config.rpath

SSSD_RESPONDER_OBJ = \
    src/responder/common/negcache.c \
    src/responder/common/responder_cmd.c \
    src/responder/common/responder_common.c \
    src/responder/common/responder_dp.c \
    src/responder/common/responder_packet.c

SSSD_TOOLS_OBJ = \
    src/tools/sss_sync_ops.c \
    src/tools/tools_util.c \
    src/tools/files.c \
    src/tools/selinux.c \
    src/tools/nscd.c

SSSD_RESOLV_OBJ = \
    src/resolv/async_resolv.c
if BUILD_ARES_DATA
    SSSD_RESOLV_OBJ += \
    src/resolv/ares/ares_parse_srv_reply.c \
    src/resolv/ares/ares_data.c
endif

SSSD_FAILOVER_OBJ = \
    src/providers/fail_over.c \
    $(SSSD_RESOLV_OBJ)

SSSD_LIBS = \
    $(TALLOC_LIBS) \
    $(TEVENT_LIBS) \
    $(POPT_LIBS) \
    $(LDB_LIBS) \
    $(DBUS_LIBS) \
    $(PCRE_LIBS) \
    $(INI_CONFIG_LIBS) \
    $(COLLECTION_LIBS) \
    $(DHASH_LIBS) \
    $(SSS_CRYPT_LIBS) \
    $(OPENLDAP_LIBS) \
    $(TDB_LIBS)

PYTHON_BINDINGS_LIBS = \
    $(TALLOC_LIBS) \
    $(TEVENT_LIBS) \
    $(POPT_LIBS) \
    $(LDB_LIBS) \
    $(DBUS_LIBS) \
    $(PCRE_LIBS) \
    $(DHASH_LIBS) \
    $(SSS_CRYPT_LIBS) \
    $(OPENLDAP_LIBS) \
    $(TDB_LIBS)

TOOLS_LIBS = \
    $(TALLOC_LIBS) \
    $(TEVENT_LIBS) \
    $(POPT_LIBS) \
    $(LDB_LIBS) \
    $(DBUS_LIBS) \
    $(PCRE_LIBS) \
    $(INI_CONFIG_LIBS) \
    $(COLLECTION_LIBS) \
    $(DHASH_LIBS) \
    $(OPENLDAP_LIBS) \
    $(TDB_LIBS)

if BUILD_SELINUX
    PYTHON_BINDINGS_LIBS += $(SELINUX_LIBS)
    TOOLS_LIBS += $(SELINUX_LIBS)
endif
if BUILD_SEMANAGE
    PYTHON_BINDINGS_LIBS += $(SEMANAGE_LIBS)
    TOOLS_LIBS += $(SEMANAGE_LIBS)
endif

dist_noinst_HEADERS = \
    src/monitor/monitor.h \
    src/util/crypto/sss_crypto.h \
    src/util/dlinklist.h \
    src/util/util.h \
    src/util/strtonum.h \
    src/util/sss_ldap.h \
    src/util/sss_python.h \
    src/util/sss_krb5.h \
    src/util/refcount.h \
    src/util/find_uid.h \
    src/util/user_info_msg.h \
    src/monitor/monitor.h \
    src/monitor/monitor_interfaces.h \
    src/responder/common/responder.h \
    src/responder/common/responder_packet.h \
    src/responder/pam/pamsrv.h \
    src/responder/nss/nsssrv.h \
    src/responder/nss/nsssrv_private.h \
    src/responder/nss/nsssrv_netgroup.h \
    src/responder/common/negcache.h \
    src/sbus/sbus_client.h \
    src/sbus/sssd_dbus.h \
    src/sbus/sssd_dbus_private.h \
    src/db/sysdb.h \
    src/db/sysdb_private.h \
    src/confdb/confdb.h \
    src/confdb/confdb_private.h \
    src/confdb/confdb_setup.h \
    src/providers/data_provider.h \
    src/providers/dp_backend.h \
    src/providers/fail_over.h \
    src/providers/providers.h \
    src/providers/child_common.h \
    src/providers/simple/simple_access.h \
    src/providers/krb5/krb5_auth.h \
    src/providers/krb5/krb5_common.h \
    src/providers/krb5/krb5_utils.h \
    src/providers/ldap/ldap_common.h \
    src/providers/ldap/sdap.h \
    src/providers/ldap/sdap_access.h \
    src/providers/ldap/sdap_async.h \
    src/providers/ldap/sdap_async_private.h \
    src/providers/ldap/sdap_id_op.h \
    src/providers/ipa/ipa_common.h \
    src/providers/ipa/ipa_access.h \
    src/providers/ipa/ipa_auth.h \
    src/providers/ipa/ipa_dyndns.h \
    src/providers/proxy/proxy.h \
    src/tools/tools_util.h \
    src/tools/sss_sync_ops.h \
    src/resolv/async_resolv.h \
    src/resolv/ares/ares_parse_srv_reply.h \
    src/resolv/ares/ares_parse_txt_reply.h \
    src/resolv/ares/ares_data.h \
    src/tests/common.h

if HAVE_NSS
    dist_noinst_HEADERS += src/util/crypto/nss/nss_util.h
endif

#####################
# Utility libraries #
#####################
noinst_LTLIBRARIES += libsss_debug.la
libsss_debug_la_SOURCES = \
    src/util/debug.c \
    src/util/sss_log.c

noinst_LTLIBRARIES += libsss_util.la
libsss_util_la_SOURCES = \
    src/confdb/confdb.c \
    src/db/sysdb.c \
    src/db/sysdb_ops.c \
    src/db/sysdb_search.c \
    src/monitor/monitor_sbus.c \
    src/providers/dp_auth_util.c \
    src/providers/dp_pam_data_util.c \
    src/providers/dp_sbus.c \
    src/sbus/sbus_client.c \
    src/sbus/sssd_dbus_common.c \
    src/sbus/sssd_dbus_connection.c \
    src/sbus/sssd_dbus_server.c \
    src/sbus/sssd_dbus_messages_helpers.c \
    src/util/util.c \
    src/util/memory.c \
    src/util/server.c \
    src/util/signal.c \
    src/util/usertools.c \
    src/util/backup_file.c \
    src/util/strtonum.c \
    src/util/check_and_open.c \
    src/util/refcount.c
libsss_util_la_LIBADD = \
    $(SSSD_LIBS) \
    libsss_crypt.la \
    libsss_debug.la

lib_LTLIBRARIES = libipa_hbac.la
dist_pkgconfig_DATA += src/providers/ipa/ipa_hbac.pc
libipa_hbac_la_SOURCES = \
    src/providers/ipa/hbac_evaluator.c
libipa_hbac_la_LDFLAGS = \
    -version 1:0:1 \
    -lunistring

include_HEADERS = \
    src/providers/ipa/ipa_hbac.h

####################
# Program Binaries #
####################
sssd_SOURCES = \
    src/monitor/monitor.c \
    src/monitor/monitor_netlink.c \
    src/confdb/confdb_setup.c
sssd_LDADD = \
    $(SSSD_LIBS) \
    $(LIBNL_LIBS) \
    libsss_util.la

sssd_nss_SOURCES = \
    src/responder/nss/nsssrv.c \
    src/responder/nss/nsssrv_cmd.c \
    src/responder/nss/nsssrv_netgroup.c \
    $(SSSD_RESPONDER_OBJ)
sssd_nss_LDADD = \
    $(TDB_LIBS) \
    $(SSSD_LIBS) \
    libsss_util.la

sssd_pam_SOURCES = \
    src/responder/pam/pam_LOCAL_domain.c \
    src/responder/pam/pamsrv.c \
    src/responder/pam/pamsrv_cmd.c \
    src/responder/pam/pamsrv_dp.c \
    $(SSSD_RESPONDER_OBJ)
sssd_pam_LDADD = \
    $(TDB_LIBS) \
    $(SSSD_LIBS) \
    libsss_util.la

sssd_sudo_SOURCES = \
    src/responder/sudo/sudosrv.c \
    src/responder/sudo/match_sudo_cmnd.c \
    $(SSSD_RESPONDER_OBJ)
sssd_sudo_LDADD = \
    $(TDB_LIBS) \
    $(SSSD_LIBS) \
    $(LDB_LIBS) \
    libsss_util.la \
    $(DBUS_LIBS) \
    $(DHASH_LIBS)
sssd_sudo_LDFLAGS = \
    -g

sssd_be_SOURCES = \
    src/providers/data_provider_be.c \
    src/providers/data_provider_fo.c \
    src/providers/data_provider_opts.c \
    src/providers/data_provider_callbacks.c \
    $(SSSD_FAILOVER_OBJ)
sssd_be_LDADD = \
    $(SSSD_LIBS) \
    $(CARES_LIBS) \
    libsss_util.la
sssd_be_LDFLAGS = \
    -Wl,--version-script,$(srcdir)/src/providers/sssd_be.exports \
    -export-dynamic \
    $(PAM_LIBS)

if BUILD_PYTHON_BINDINGS
sss_obfuscate_pythondir = $(sbindir)
dist_sss_obfuscate_python_SCRIPTS = \
	src/tools/sss_obfuscate
endif



dist_noinst_DATA += \
    src/examples/sssd-example.conf \
    src/examples/sssdproxytest \
    src/examples/sudo \
    src/examples/logrotate \
    src/providers/sssd_be.exports \
    src/sss_client/COPYING \
    src/sss_client/COPYING.LESSER \
    src/m4

######################
# Command-line Tools #
######################
sss_useradd_SOURCES = \
    src/tools/sss_useradd.c \
    $(SSSD_TOOLS_OBJ)
sss_useradd_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_userdel_SOURCES = \
    src/tools/sss_userdel.c \
    src/util/find_uid.c \
    $(SSSD_TOOLS_OBJ)
sss_userdel_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_groupadd_SOURCES = \
    src/tools/sss_groupadd.c \
    $(SSSD_TOOLS_OBJ)
sss_groupadd_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_groupdel_SOURCES = \
    src/tools/sss_groupdel.c \
    $(SSSD_TOOLS_OBJ)
sss_groupdel_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_usermod_SOURCES = \
    src/tools/sss_usermod.c \
    $(SSSD_TOOLS_OBJ)
sss_usermod_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_groupmod_SOURCES = \
    src/tools/sss_groupmod.c \
    $(SSSD_TOOLS_OBJ)
sss_groupmod_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_groupshow_SOURCES = \
    src/tools/sss_groupshow.c \
    $(SSSD_TOOLS_OBJ)
sss_groupshow_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_cache_SOURCES = \
    src/tools/sss_cache.c \
    $(SSSD_TOOLS_OBJ)
sss_cache_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

sss_debuglevel_SOURCES = \
    src/tools/sss_debuglevel.c \
    $(SSSD_TOOLS_OBJ)
sss_debuglevel_LDADD = \
    libsss_util.la \
    $(TOOLS_LIBS)

#################
# Feature Tests #
#################
TESTS_ENVIRONMENT = LDB_MODULES_PATH=$(abs_top_builddir)/ldb_mod_test_dir

ldb_mod_test_dir: memberof.la
	mkdir -p $(builddir)/ldb_mod_test_dir
	cp $(builddir)/.libs/memberof.so $(builddir)/ldb_mod_test_dir

noinst_LTLIBRARIES += \
    libsss_test_common.la

libsss_test_common_la_SOURCES = \
    src/tests/common.c

if HAVE_CHECK
libsss_test_common_la_SOURCES += \
    src/tests/common_check.c

sysdb_tests_DEPENDENCIES = \
    $(ldblib_LTLIBRARIES)
sysdb_tests_SOURCES = \
    src/tests/sysdb-tests.c
sysdb_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
sysdb_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la \
    libsss_test_common.la

strtonum_tests_SOURCES = \
    src/tests/strtonum-tests.c \
    src/util/strtonum.c
strtonum_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
strtonum_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_debug.la \
    libsss_test_common.la

krb5_utils_tests_SOURCES = \
    src/tests/krb5_utils-tests.c \
    src/providers/krb5/krb5_utils.c \
    src/providers/krb5/krb5_common.c \
    src/util/sss_krb5.c \
    src/providers/data_provider_fo.c \
    src/providers/data_provider_opts.c \
    src/providers/data_provider_callbacks.c \
    $(SSSD_FAILOVER_OBJ)
krb5_utils_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
krb5_utils_tests_LDADD = \
    $(SSSD_LIBS)\
    $(CARES_LIBS) \
    $(KRB5_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la \
    libsss_test_common.la


check_and_open_tests_SOURCES = \
    src/tests/check_and_open-tests.c \
    src/util/check_and_open.c
check_and_open_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
check_and_open_tests_LDADD = \
    libsss_debug.la \
    $(CHECK_LIBS) \
    libsss_test_common.la

FILES_TESTS_LIBS = \
    $(CHECK_LIBS) \
    $(POPT_LIBS) \
    $(TALLOC_LIBS) \
    libsss_test_common.la
if BUILD_SELINUX
    FILES_TESTS_LIBS += $(SELINUX_LIBS)
endif
if BUILD_SEMANAGE
    FILES_TESTS_LIBS += $(SEMANAGE_LIBS)
endif

files_tests_SOURCES = \
    src/tests/files-tests.c \
    src/util/check_and_open.c \
    src/tools/selinux.c \
    src/tools/files.c
files_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
files_tests_LDADD = \
    libsss_debug.la \
    $(FILES_TESTS_LIBS) \
    libsss_test_common.la

SSSD_RESOLV_TESTS_OBJ = \
    $(SSSD_RESOLV_OBJ)
if BUILD_ARES_DATA
    SSSD_RESOLV_TESTS_OBJ += \
    src/resolv/ares/ares_parse_txt_reply.c
endif

resolv_tests_SOURCES = \
    src/tests/resolv-tests.c \
    $(SSSD_RESOLV_TESTS_OBJ)
resolv_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS) \
    -DBUILD_TXT
resolv_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    $(CARES_LIBS) \
    libsss_util.la \
    libsss_test_common.la

refcount_tests_SOURCES = \
    src/tests/refcount-tests.c \
    $(CHECK_OBJ)
refcount_tests_CFLAGS = \
    $(CHECK_CFLAGS)
refcount_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la \
    libsss_test_common.la

fail_over_tests_SOURCES = \
    src/tests/fail_over-tests.c \
    $(SSSD_FAILOVER_OBJ) \
    $(CHECK_OBJ)
fail_over_tests_CFLAGS = \
    $(CHECK_CFLAGS)
fail_over_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    $(CARES_LIBS) \
    libsss_util.la \
    libsss_test_common.la

find_uid_tests_SOURCES = \
    src/tests/find_uid-tests.c \
    src/util/find_uid.c \
    src/util/strtonum.c
find_uid_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(TALLOC_CFLAGS) \
    $(DHASH_CFLAGS) \
    $(CHECK_CFLAGS)
find_uid_tests_LDADD = \
    libsss_debug.la \
    $(TALLOC_LIBS) \
    $(DHASH_LIBS) \
    $(CHECK_LIBS) \
    libsss_test_common.la

auth_tests_SOURCES = \
    src/tests/auth-tests.c
auth_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
auth_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la \
    libsss_test_common.la

ipa_ldap_opt_tests_SOURCES = \
    src/providers/ipa/ipa_utils.c \
    src/tests/ipa_ldap_opt-tests.c
ipa_ldap_opt_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
ipa_ldap_opt_tests_LDADD = \
    $(CHECK_LIBS) \
    $(TALLOC_LIBS) \
    libsss_test_common.la

simple_access_tests_SOURCES = \
    src/tests/simple_access-tests.c \
    src/providers/simple/simple_access.c
simple_access_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
simple_access_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la

util_tests_SOURCES = \
    src/tests/util-tests.c
util_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
util_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la \
    libsss_test_common.la

debug_tests_SOURCES = \
    src/tests/debug-tests.c
debug_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
debug_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la

crypto_tests_SOURCES = \
    $(SSS_CRYPT_SOURCES) \
    src/tests/crypto-tests.c
crypto_tests_CFLAGS = \
    $(SSS_CRYPT_CFLAGS) \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
crypto_tests_LDADD = \
    libsss_debug.la \
    $(SSS_CRYPT_LIBS) \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_test_common.la

ipa_hbac_tests_SOURCES = \
    src/tests/ipa_hbac-tests.c
ipa_hbac_tests_CFLAGS = \
    $(AM_CFLAGS) \
    $(CHECK_CFLAGS)
ipa_hbac_tests_LDADD = \
    $(SSSD_LIBS) \
    $(CHECK_LIBS) \
    libsss_util.la \
    libsss_test_common.la \
    libipa_hbac.la

endif

stress_tests_SOURCES = \
    src/tests/stress-tests.c
stress_tests_LDADD = \
    $(SSSD_LIBS) \
    libsss_util.la \
    libsss_test_common.la

noinst_PROGRAMS = pam_test_client
pam_test_client_SOURCES = src/sss_client/pam_test_client.c
pam_test_client_LDFLAGS = -lpam -lpam_misc

####################
# Client Libraries #
####################

nsslib_LTLIBRARIES = libnss_sss.la
libnss_sss_la_SOURCES = \
    src/sss_client/common.c \
    src/sss_client/nss_passwd.c \
    src/sss_client/nss_group.c \
    src/sss_client/nss_netgroup.c \
    src/sss_client/sss_cli.h \
    src/sss_client/nss_compat.h
libnss_sss_la_LDFLAGS = \
    -module \
    -version-info 2:0:0 \
    -Wl,--version-script,$(srcdir)/src/sss_client/sss_nss.exports

pamlib_LTLIBRARIES = pam_sss.la
pam_sss_la_SOURCES = \
    src/sss_client/pam_sss.c \
    src/sss_client/common.c \
    src/sss_client/sss_cli.h \
    src/sss_client/sss_pam_macros.h

pam_sss_la_LDFLAGS = \
    -lpam \
    -module \
    -avoid-version \
    -g \
    -Wl,--version-script,$(srcdir)/src/sss_client/sss_pam.exports

dist_noinst_DATA += \
    src/sss_client/sss_nss.exports \
    src/sss_client/sss_pam.exports

libsss_sudoplugin_LTLIBRARIES = libsss_sudoplugin.la
libsss_sudoplugin_la_SOURCES = \
    src/sss_client/sudo_plugin/sss_sudoplugin.c \
    src/sss_client/sudo_plugin/sss_sudo_cli.h

libsss_sudoplugin_la_LDFLAGS = \
    -module \
    -lpam \
    -lpam_misc \
    -g \
    $(DBUS_LIBS) \
    $(DHASH_LIBS) \
    libsss_util.la

libsss_sudoplugin_la_CPPFLAGS = \
     $(AM_CPPFLAGS) \
     -D_GNU_SOURCE \
     -ldbus-1
    
####################
# Plugin Libraries #
####################
libsss_ldap_la_SOURCES = \
    src/util/find_uid.c \
    src/providers/child_common.c \
    src/providers/ldap/ldap_id.c \
    src/providers/ldap/ldap_id_enum.c \
    src/providers/ldap/ldap_id_cleanup.c \
    src/providers/ldap/ldap_id_netgroup.c \
    src/providers/ldap/sdap_access.c \
    src/providers/ldap/ldap_auth.c \
    src/providers/ldap/ldap_init.c \
    src/providers/ldap/ldap_common.c \
    src/providers/ldap/sdap_async.c \
    src/providers/ldap/sdap_async_users.c \
    src/providers/ldap/sdap_async_groups.c \
    src/providers/ldap/sdap_async_initgroups.c \
    src/providers/ldap/sdap_async_connection.c \
    src/providers/ldap/sdap_async_netgroups.c \
    src/providers/ldap/sdap_child_helpers.c \
    src/providers/ldap/sdap_fd_events.c \
    src/providers/ldap/sdap_id_op.c \
    src/providers/ldap/sdap.c \
    src/providers/krb5/krb5_common.c \
    src/util/user_info_msg.c \
    src/util/sss_ldap.c \
    src/util/sss_krb5.c
libsss_ldap_la_CFLAGS = \
    $(AM_CFLAGS) \
    $(LDAP_CFLAGS) \
    $(KRB5_CFLAGS)
libsss_ldap_la_LIBADD = \
    $(OPENLDAP_LIBS) \
    $(DHASH_LIBS) \
    $(KRB5_LIBS) \
    libsss_util.la
libsss_ldap_la_LDFLAGS = \
    -avoid-version \
    -module

libsss_proxy_la_SOURCES = \
    src/providers/proxy/proxy_common.c \
    src/providers/proxy/proxy_init.c \
    src/providers/proxy/proxy_id.c \
    src/providers/proxy/proxy_netgroup.c \
    src/providers/proxy/proxy_auth.c
libsss_proxy_la_CFLAGS = \
    $(AM_CFLAGS)
libsss_proxy_la_LIBADD = \
    libsss_util.la \
    $(PAM_LIBS)
libsss_proxy_la_LDFLAGS = \
    -avoid-version \
    -module

libsss_simple_la_SOURCES = \
    src/providers/simple/simple_access.c
libsss_simple_la_CFLAGS = \
    $(AM_CFLAGS)
libsss_simple_la_LIBADD = \
    $(PAM_LIBS) \
    libsss_util.la
libsss_simple_la_LDFLAGS = \
    -avoid-version \
    -module

libsss_krb5_la_SOURCES = \
    src/util/find_uid.c \
    src/providers/child_common.c \
    src/providers/krb5/krb5_utils.c \
    src/providers/krb5/krb5_become_user.c \
    src/providers/krb5/krb5_delayed_online_authentication.c \
    src/providers/krb5/krb5_renew_tgt.c \
    src/providers/krb5/krb5_wait_queue.c \
    src/providers/krb5/krb5_auth.c \
    src/providers/krb5/krb5_access.c \
    src/providers/krb5/krb5_child_handler.c \
    src/providers/krb5/krb5_common.c \
    src/providers/krb5/krb5_init.c \
    src/util/sss_krb5.c
libsss_krb5_la_CFLAGS = \
    $(AM_CFLAGS) \
    $(DHASH_CFLAGS)
libsss_krb5_la_LIBADD = \
    $(DHASH_LIBS) \
    $(KEYUTILS_LIBS) \
    $(KRB5_LIBS) \
    libsss_util.la
libsss_krb5_la_LDFLAGS = \
    -avoid-version \
    -module

libsss_ipa_la_SOURCES = \
    src/providers/child_common.c \
    src/providers/ipa/ipa_init.c \
    src/providers/ipa/ipa_common.c \
    src/providers/ipa/ipa_utils.c \
    src/providers/ipa/ipa_auth.c \
    src/providers/ipa/ipa_access.c \
    src/providers/ipa/ipa_dyndns.c \
    src/providers/ipa/ipa_hbac_hosts.c \
    src/providers/ipa/ipa_hbac_private.h \
    src/providers/ipa/ipa_hbac_rules.c \
    src/providers/ipa/ipa_hbac_services.c \
    src/providers/ipa/ipa_hbac_users.c \
    src/providers/ipa/ipa_hbac_common.c \
    src/providers/ldap/ldap_id.c \
    src/providers/ldap/ldap_id_enum.c \
    src/providers/ldap/ldap_id_cleanup.c \
    src/providers/ldap/ldap_id_netgroup.c \
    src/providers/ldap/ldap_auth.c \
    src/providers/ldap/ldap_common.c \
    src/providers/ldap/sdap_async.c \
    src/providers/ldap/sdap_async_users.c \
    src/providers/ldap/sdap_async_groups.c \
    src/providers/ldap/sdap_async_initgroups.c \
    src/providers/ldap/sdap_async_connection.c \
    src/providers/ldap/sdap_async_netgroups.c \
    src/providers/ldap/sdap_child_helpers.c \
    src/providers/ldap/sdap_fd_events.c \
    src/providers/ldap/sdap_id_op.c \
    src/providers/ldap/sdap.c \
    src/util/user_info_msg.c \
    src/util/sss_ldap.c \
    src/util/sss_krb5.c \
    src/util/find_uid.c \
    src/providers/krb5/krb5_utils.c \
    src/providers/krb5/krb5_become_user.c \
    src/providers/krb5/krb5_delayed_online_authentication.c \
    src/providers/krb5/krb5_renew_tgt.c \
    src/providers/krb5/krb5_wait_queue.c \
    src/providers/krb5/krb5_common.c \
    src/providers/krb5/krb5_auth.c \
    src/providers/krb5/krb5_access.c \
    src/providers/krb5/krb5_child_handler.c
libsss_ipa_la_CFLAGS = \
    $(AM_CFLAGS) \
    $(LDAP_CFLAGS) \
    $(DHASH_CFLAGS) \
    $(KRB5_CFLAGS)
libsss_ipa_la_LIBADD = \
    $(OPENLDAP_LIBS) \
    $(DHASH_LIBS) \
    $(KEYUTILS_LIBS) \
    $(KRB5_LIBS) \
    libsss_util.la \
    libipa_hbac.la
libsss_ipa_la_LDFLAGS = \
    -avoid-version \
    -module

krb5_child_SOURCES = \
    src/providers/krb5/krb5_become_user.c \
    src/providers/krb5/krb5_child.c \
    src/providers/child_common.c \
    src/providers/dp_pam_data_util.c \
    src/util/user_info_msg.c \
    src/util/sss_krb5.c
krb5_child_CFLAGS = \
    $(AM_CFLAGS) \
    $(POPT_CFLAGS) \
    $(KRB5_CFLAGS)
krb5_child_LDADD = \
    libsss_debug.la \
    $(TALLOC_LIBS) \
    $(TEVENT_LIBS) \
    $(POPT_LIBS) \
    $(KRB5_LIBS)

ldap_child_SOURCES = \
    src/providers/ldap/ldap_child.c \
    src/providers/child_common.c \
    src/util/sss_krb5.c
ldap_child_CFLAGS = \
    $(AM_CFLAGS) \
    $(POPT_CFLAGS) \
    $(KRB5_CFLAGS)
ldap_child_LDADD = \
    libsss_debug.la \
    $(TALLOC_LIBS) \
    $(TEVENT_LIBS) \
    $(POPT_LIBS) \
    $(OPENLDAP_LIBS) \
    $(KRB5_LIBS)

proxy_child_SOURCES = \
    src/providers/proxy/proxy_child.c
proxy_child_CFLAGS = \
    $(AM_CFLAGS) \
    $(POPT_CFLAGS)
proxy_child_LDADD = \
    $(PAM_LIBS) \
    $(SSSD_LIBS) \
    libsss_util.la

memberof_la_SOURCES = \
    src/ldb_modules/memberof.c \
    src/util/util.c
memberof_la_CFLAGS = \
    $(AM_CFLAGS)
memberof_la_LIBADD = \
    libsss_debug.la \
    $(LDB_LIBS) \
    $(DHASH_LIBS)
memberof_la_LDFLAGS = \
    -avoid-version \
    -module

if BUILD_KRB5_LOCATOR_PLUGIN
sssd_krb5_locator_plugin_la_SOURCES = \
    src/krb5_plugin/sssd_krb5_locator_plugin.c
sssd_krb5_locator_plugin_la_CFLAGS = \
    $(AM_CFLAGS) \
    $(KRB5_CFLAGS)
sssd_krb5_locator_plugin_la_LDFLAGS = \
    -avoid-version \
    -module
endif

if BUILD_PYTHON_BINDINGS
pysss_la_SOURCES = \
    $(SSSD_TOOLS_OBJ) \
    src/python/pysss.c
pysss_la_CFLAGS = \
    $(AM_CFLAGS)  \
    $(PYTHON_CFLAGS)
pysss_la_LIBADD = \
    $(PYTHON_BINDINGS_LIBS) \
    $(PYTHON_LIBS) \
    libsss_util.la
pysss_la_LDFLAGS = \
    -avoid-version \
    -module

pyhbac_la_SOURCES = \
    src/python/pyhbac.c \
    src/util/sss_python.c
pyhbac_la_CFLAGS = \
    $(AM_CFLAGS)  \
    $(PYTHON_CFLAGS)
pyhbac_la_LIBADD = \
    $(PYTHON_LIBS) \
    libipa_hbac.la
pyhbac_la_LDFLAGS = \
    -avoid-version \
    -module
endif

################
# TRANSLATIONS #
################
update-po:
	$(MAKE) -C src/man update-po
	$(MAKE) -C po update-po

#######################
# Installation Extras #
#######################

dist_init_SCRIPTS =
dist_systemdunit_DATA =
if HAVE_SYSTEMD_UNIT
    dist_systemdunit_DATA += \
        src/sysv/systemd/sssd.service
else
if HAVE_SUSE
    dist_init_SCRIPTS += \
        src/sysv/SUSE/sssd
else
if HAVE_GENTOO
    dist_init_SCRIPTS += \
        src/sysv/gentoo/sssd
else
    dist_init_SCRIPTS += \
        src/sysv/sssd
endif
endif
endif


dist_sssdconf_DATA = \
    src/config/etc/sssd.api.conf
dist_sssdapiplugin_DATA = \
    src/config/etc/sssd.api.d/sssd-ipa.conf \
    src/config/etc/sssd.api.d/sssd-krb5.conf \
    src/config/etc/sssd.api.d/sssd-ldap.conf \
    src/config/etc/sssd.api.d/sssd-local.conf \
    src/config/etc/sssd.api.d/sssd-proxy.conf \
    src/config/etc/sssd.api.d/sssd-simple.conf

installsssddirs::
	mkdir -p \
    $(DESTDIR)$(includedir) \
    $(DESTDIR)$(libdir) \
    $(DESTDIR)$(sbindir) \
    $(DESTDIR)$(mandir) \
    $(DESTDIR)$(pluginpath) \
    $(DESTDIR)$(libdir)/ldb \
    $(DESTDIR)$(dbusintrospectdir) \
    $(DESTDIR)$(pipepath)/private \
    $(DESTDIR)$(sssdlibdir) \
    $(DESTDIR)$(sssdconfdir) \
    $(DESTDIR)$(dbpath) \
    $(DESTDIR)$(pidpath) \
    $(DESTDIR)$(logpath) \
    $(DESTDIR)$(pubconfpath)

if HAVE_DOXYGEN
docs:
	$(DOXYGEN) src/doxy.config
	$(DOXYGEN) src/providers/ipa/ipa_hbac.doxy
else
docs:
	@echo "Doxygen not installed, cannot generate documentation"
	@exit 1
endif

all-local: ldb_mod_test_dir
if BUILD_PYTHON_BINDINGS
	cd $(srcdir)/src/config; $(PYTHON) setup.py build --build-base $(abs_builddir)/src/config
endif

install-exec-hook: installsssddirs
if BUILD_PYTHON_BINDINGS
	if [ "$(DESTDIR)" = "" ]; then \
		cd $(srcdir)/src/config; $(PYTHON) setup.py build --build-base $(abs_builddir)/src/config install $(DISTSETUPOPTS) --prefix=$(PYTHON_PREFIX) --record=$(abs_builddir)/src/config/.files; \
	else \
		cd $(srcdir)/src/config; $(PYTHON) setup.py build --build-base $(abs_builddir)/src/config install $(DISTSETUPOPTS) --prefix=$(PYTHON_PREFIX) --root=$(DESTDIR) --record=$(abs_builddir)/src/config/.files; \
	fi
endif
	mkdir -p doc $(DESTDIR)/$(docdir); cp -a doc $(DESTDIR)/$(docdir)/
if HAVE_SYSTEMD_UNIT
	mkdir -p $(DESTDIR)$(systemdunitdir)
else
	mkdir -p $(DESTDIR)$(initdir)
endif

install-data-hook:
	rm $(DESTDIR)/$(nsslibdir)/libnss_sss.so.2 \
       $(DESTDIR)/$(nsslibdir)/libnss_sss.so
	mv $(DESTDIR)/$(nsslibdir)/libnss_sss.so.2.0.0 $(DESTDIR)/$(nsslibdir)/libnss_sss.so.2
	if [ ! $(krb5rcachedir) = "__LIBKRB5_DEFAULTS__" ]; then \
        mkdir -p $(DESTDIR)/$(krb5rcachedir) ; \
	fi

uninstall-hook:
	if [ -f $(abs_builddir)/src/config/.files ]; then \
	    cat $(abs_builddir)/src/config/.files | xargs -iq rm -f $(DESTDIR)/q; \
	    rm $(abs_builddir)/src/config/.files ; \
	fi
	rm -Rf $(DESTDIR)/$(docdir)/doc

clean-local:
if BUILD_PYTHON_BINDINGS
	cd $(srcdir)/src/config; $(PYTHON) setup.py build --build-base $(abs_builddir)/src/config clean --all
endif
	rm -Rf doc
	rm -Rf ldb_mod_test_dir

CLEANFILES = *.X */*.X */*/*.X

tests: all $(check_PROGRAMS)


# RPM-related tasks

RPMBUILD ?= $(PWD)/rpmbuild

dist_noinst_DATA += \
    m4 \
    contrib/sssd.spec.in \
    BUILD.txt \
    COPYING

rpmroot:
	mkdir -p $(RPMBUILD)/BUILD
	mkdir -p $(RPMBUILD)/RPMS
	mkdir -p $(RPMBUILD)/SOURCES
	mkdir -p $(RPMBUILD)/SPECS
	mkdir -p $(RPMBUILD)/SRPMS

rpms: dist-gzip rpmroot
	cp $(builddir)/contrib/sssd.spec $(RPMBUILD)/SPECS
	cp $(distdir).tar.gz $(RPMBUILD)/SOURCES
	cd $(RPMBUILD); \
	rpmbuild --define "_topdir $(RPMBUILD)" -ba SPECS/sssd.spec

if GIT_CHECKOUT
prerelease-rpms:
	cp $(srcdir)/version.m4 $(srcdir)/version.m4.orig
	sed -e "s/m4_define(\[PRERELEASE_VERSION_NUMBER\], \[.*\])/m4_define(\[PRERELEASE_VERSION_NUMBER\], \[.`date +%Y%m%d.%H%M`.git`git log -1 --pretty=format:%h`\])/" < $(srcdir)/version.m4.orig > $(srcdir)/version.m4
	$(MAKE) rpms
	mv $(srcdir)/version.m4.orig $(srcdir)/version.m4
endif

# make srpms will use the old digest algorithm to be compatible
# with RHEL5
srpm: dist-gzip rpmroot
	cp $(builddir)/contrib/sssd.spec $(RPMBUILD)/SPECS
	cp $(distdir).tar.gz $(RPMBUILD)/SOURCES
	cd $(RPMBUILD); \
	rpmbuild --define "_topdir $(RPMBUILD)" \
	         --define _source_filedigest_algorithm=1 \
	         -bs SPECS/sssd.spec

if GIT_CHECKOUT
prerelease-srpm:
	cp $(srcdir)/version.m4 $(srcdir)/version.m4.orig
	sed -e "s/m4_define(\[PRERELEASE_VERSION_NUMBER\], \[.*\])/m4_define(\[PRERELEASE_VERSION_NUMBER\], \[.`date +%Y%m%d.%H%M`.git`git log -1 --pretty=format:%h`\])/" < $(srcdir)/version.m4.orig > $(srcdir)/version.m4
	$(MAKE) srpm
	mv $(srcdir)/version.m4.orig $(srcdir)/version.m4
endif

