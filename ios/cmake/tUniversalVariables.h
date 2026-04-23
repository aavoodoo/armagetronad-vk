/* tUniversalVariables.h — iOS build
 * On iOS, paths are managed via SDL_GetBasePath() and SDL_GetPrefPath().
 * These compile-time path constants are not used at runtime.
 */

#ifndef INITDIR
    #define INITDIR "/etc/init.d"
#endif
#ifndef PREFIX
    #define PREFIX "."
#endif
#ifndef EXEC_PREFIX
    #define EXEC_PREFIX "."
#endif
#ifndef BINDIR
    #define BINDIR "."
#endif
#ifndef SBINDIR
    #define SBINDIR "."
#endif
#ifndef DATADIR
    #define DATADIR "."
#endif
#ifndef DATADIR_SUFFIX
    #define DATADIR_SUFFIX ""
#endif
#ifndef AA_DATADIR
    #define AA_DATADIR "."
#endif
#ifndef SYSCONFDIR
    #define SYSCONFDIR "."
#endif
#ifndef SYSCONFDIR_SUFFIX
    #define SYSCONFDIR_SUFFIX ""
#endif
#ifndef AA_SYSCONFDIR
    #define AA_SYSCONFDIR "."
#endif
#ifndef LOCALSTATEDIR
    #define LOCALSTATEDIR "."
#endif
#ifndef LOCALSTATEDIR_SUFFIX
    #define LOCALSTATEDIR_SUFFIX ""
#endif
#ifndef AA_LOCALSTATEDIR
    #define AA_LOCALSTATEDIR "."
#endif
#ifndef RUNDIR
    #define RUNDIR "."
#endif
#ifndef RUNDIR_SUFFIX
    #define RUNDIR_SUFFIX ""
#endif
#ifndef AA_RUNDIR
    #define AA_RUNDIR "."
#endif
#ifndef LOGDIR
    #define LOGDIR "."
#endif
#ifndef LOGDIR_SUFFIX
    #define LOGDIR_SUFFIX ""
#endif
#ifndef AA_LOGDIR
    #define AA_LOGDIR "."
#endif
#ifndef MANDIR
    #define MANDIR "."
#endif
#ifndef MANDIR_SUFFIX
    #define MANDIR_SUFFIX ""
#endif
#ifndef AA_MANDIR
    #define AA_MANDIR "."
#endif
#ifndef INFODIR
    #define INFODIR "."
#endif
#ifndef INFODIR_SUFFIX
    #define INFODIR_SUFFIX ""
#endif
#ifndef AA_INFODIR
    #define AA_INFODIR "."
#endif
#ifndef WWWROOTDIR
    #define WWWROOTDIR "."
#endif
#ifndef OLDVARDIR
    #define OLDVARDIR "."
#endif
#ifndef OLDVARDIR_SUFFIX
    #define OLDVARDIR_SUFFIX ""
#endif
#ifndef AA_OLDVARDIR
    #define AA_OLDVARDIR "."
#endif
#ifndef SCRIPTDIR
    #define SCRIPTDIR "."
#endif
#ifndef RELOCATABLE
    #define RELOCATABLE 1
#endif
#ifndef ENABLE_MIGRATESTATE
    /* #undef ENABLE_MIGRATESTATE */
#endif
