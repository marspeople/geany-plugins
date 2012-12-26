AC_DEFUN([GP_CHECK_GEANYDIFF],
[
    GP_ARG_DISABLE([GeanyDiff], [yes])
    GP_STATUS_PLUGIN_ADD([GeanyDiff], [$enable_geanydiff])
    AC_CONFIG_FILES([
        geanydiff/Makefile
        geanydiff/src/Makefile
    ])
])
