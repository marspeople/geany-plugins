AC_DEFUN([GP_CHECK_GEANYDIFF],
[
    GP_ARG_DISABLE([GeanyDiff], [auto])
    GP_CHECK_PLUGIN_GTK2_ONLY([GeanyDiff])
    GP_COMMIT_PLUGIN_STATUS([GeanyDiff])
    AC_CONFIG_FILES([
        geanydiff/Makefile
        geanydiff/src/Makefile
    ])
])
