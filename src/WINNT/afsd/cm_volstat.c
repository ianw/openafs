/* Copyright 2007 Secure Endpoints Inc.
 *
 * BSD 2-part License 
 */

/* This source file provides the declarations 
 * which specify the AFS Cache Manager Volume Status Event
 * Notification API
 */

#include <afs/param.h>
#include <afs/stds.h>

#include <windows.h>
#include <winsock2.h>
#include <nb30.h>
#include <string.h>
#include <malloc.h>
#include "afsd.h"
#include <WINNT/afsreg.h>

HMODULE hVolStatus = NULL;
dll_VolStatus_Funcs_t dll_funcs;
cm_VolStatus_Funcs_t cm_funcs;

/* This function is used to load any Volume Status Handlers 
 * and their associated function pointers.  
 */
long 
cm_VolStatus_Initialization(void)
{
    long (__fastcall * dll_VolStatus_Initialization)(dll_VolStatus_Funcs_t * dll_funcs, cm_VolStatus_Funcs_t *cm_funcs) = NULL;
    long code = 0;
    HKEY parmKey;
    DWORD dummyLen;
    char wd[MAX_PATH+1] = "";

    code = RegOpenKeyEx(HKEY_LOCAL_MACHINE, AFSREG_CLT_SVC_PARAM_SUBKEY,
                         0, KEY_QUERY_VALUE, &parmKey);
    if (code == ERROR_SUCCESS) {
        dummyLen = sizeof(wd);
        code = RegQueryValueEx(parmKey, "VolStatusHandler", NULL, NULL,
                                (BYTE *) &wd, &dummyLen);
        RegCloseKey (parmKey);
    }

    if (code == ERROR_SUCCESS && wd[0])
        hVolStatus = LoadLibrary(wd);
    if (hVolStatus) {
        (FARPROC) dll_VolStatus_Initialization = GetProcAddress(hVolStatus, "@VolStatus_Initialization@8");
        if (dll_VolStatus_Initialization) {
            cm_funcs.version = CM_VOLSTATUS_FUNCS_VERSION;
            cm_funcs.cm_VolStatus_Path_To_ID = cm_VolStatus_Path_To_ID;
            cm_funcs.cm_VolStatus_Path_To_DFSlink = cm_VolStatus_Path_To_DFSlink;

            code = dll_VolStatus_Initialization(&dll_funcs, &cm_funcs);
        } 

        if (dll_VolStatus_Initialization == NULL || code != 0 || 
            dll_funcs.version != DLL_VOLSTATUS_FUNCS_VERSION) {
            FreeLibrary(hVolStatus);
            hVolStatus = NULL;
            code = -1;
        }
    }

    return code;
}

/* This function is used to unload any Volume Status Handlers
 * that were loaded during initialization.
 */
long 
cm_VolStatus_Finalize(void)
{
    if (hVolStatus == NULL)
        return 0;

    FreeLibrary(hVolStatus);
    hVolStatus = NULL;
    return 0;
}

/* This function notifies the Volume Status Handlers that the
 * AFS client service has started.  If the network is started
 * at this point we call cm_VolStatus_Network_Started().
 */
long 
cm_VolStatus_Service_Started(void)
{
    long code = 0;

    if (hVolStatus == NULL)
        return 0;
   
    code = dll_funcs.dll_VolStatus_Service_Started();
    if (code == 0 && smb_IsNetworkStarted())
        code = dll_funcs.dll_VolStatus_Network_Started(cm_NetbiosName, cm_NetbiosName);

    return code;
}

/* This function notifies the Volume Status Handlers that the
 * AFS client service is stopping.
 */
long 
cm_VolStatus_Service_Stopped(void)
{
    long code = 0;

    if (hVolStatus == NULL)
        return 0;
   
    code = dll_funcs.dll_VolStatus_Service_Stopped();

    return code;
}


/* This function notifies the Volume Status Handlers that the
 * AFS client service is accepting network requests using the 
 * specified netbios names.
 */
long
#ifdef _WIN64
cm_VolStatus_Network_Started(const char * netbios32, const char * netbios64)
#else /* _WIN64 */
cm_VolStatus_Network_Started(const char * netbios)
#endif /* _WIN64 */
{
    long code = 0;

    if (hVolStatus == NULL)
        return 0;

#ifdef _WIN64
    code = dll_funcs.dll_VolStatus_Network_Started(netbios32, netbios64);
#else
    code = dll_funcs.dll_VolStatus_Network_Started(netbios, netbios);
#endif

    return code;
}

/* This function notifies the Volume Status Handlers that the
 * AFS client service is no longer accepting network requests 
 * using the specified netbios names 
 */
long
#ifdef _WIN64
cm_VolStatus_Network_Stopped(const char * netbios32, const char * netbios64)
#else /* _WIN64 */
cm_VolStatus_Network_Stopped(const char * netbios)
#endif /* _WIN64 */
{
    long code = 0;

    if (hVolStatus == NULL)
        return 0;

#ifdef _WIN64
    code = dll_funcs.dll_VolStatus_Network_Stopped(netbios32, netbios64);
#else
    code = dll_funcs.dll_VolStatus_Network_Stopped(netbios, netbios);
#endif

    return code;
}

/* This function is called when the IP address list changes.
 * Volume Status Handlers can use this notification as a hint 
 * that it might be possible to determine volume IDs for paths 
 * that previously were not accessible.  
 */
long 
cm_VolStatus_Network_Addr_Change(void)
{
    long code = 0;

    if (hVolStatus == NULL)
        return 0;

    code = dll_funcs.dll_VolStatus_Network_Addr_Change();

    return code;
}

/* This function notifies the Volume Status Handlers that the 
 * state of the specified cell.volume has changed.
 */
long 
cm_VolStatus_Change_Notification(afs_uint32 cellID, afs_uint32 volID, enum volstatus status)
{
    long code = 0;

    if (hVolStatus == NULL)
        return 0;

    code = dll_funcs.dll_VolStatus_Change_Notification(cellID, volID, status);

    return code;
}


long __fastcall
cm_VolStatus_Path_To_ID(const char * share, const char * path, afs_uint32 * cellID, afs_uint32 * volID)
{
    afs_uint32  code;
    cm_req_t    req;
    cm_scache_t *scp;

    if (cellID == NULL || volID == NULL)
        return CM_ERROR_INVAL;

    cm_InitReq(&req);


    code = cm_NameI(cm_data.rootSCachep, (char *)path, CM_FLAG_FOLLOW, cm_rootUserp, (char *)share, &req, &scp);
    if (code)
        return code;

    lock_ObtainMutex(&scp->mx);
    code = cm_SyncOp(scp, NULL,cm_rootUserp, &req, 0,
                     CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);
    if (code) {
        lock_ReleaseMutex(&scp->mx);
        cm_ReleaseSCache(scp);
        return code;
    }
        
    cm_SyncOpDone(scp, NULL, CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);

    *cellID = scp->fid.cell;
    *volID  = scp->fid.volume;

    lock_ReleaseMutex(&scp->mx);
    cm_ReleaseSCache(scp);

    return 0;
}

long __fastcall
cm_VolStatus_Path_To_DFSlink(const char * share, const char * path, afs_uint32 *pBufSize, char *pBuffer)
{
    afs_uint32  code;
    cm_req_t    req;
    cm_scache_t *scp;
    size_t      len;

    if (pBufSize == NULL || (pBuffer == NULL && *pBufSize != 0))
        return CM_ERROR_INVAL;

    cm_InitReq(&req);

    code = cm_NameI(cm_data.rootSCachep, (char *)path, CM_FLAG_FOLLOW, cm_rootUserp, (char *)share, &req, &scp);
    if (code)
        return code;

    lock_ObtainMutex(&scp->mx);
    code = cm_SyncOp(scp, NULL, cm_rootUserp, &req, 0,
                     CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);
    if (code) {
        lock_ReleaseMutex(&scp->mx);
        cm_ReleaseSCache(scp);
        return code;
    }
        
    cm_SyncOpDone(scp, NULL, CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);

    if (scp->fileType != CM_SCACHETYPE_DFSLINK)
        return CM_ERROR_NOT_A_DFSLINK;

    len = strlen(scp->mountPointStringp) + 1;
    if (pBuffer == NULL)
        *pBufSize = len;
    else if (*pBufSize >= len) {
        strcpy(pBuffer, scp->mountPointStringp);
        *pBufSize = len;
    } else 
        code = CM_ERROR_TOOBIG;

    lock_ReleaseMutex(&scp->mx);
    cm_ReleaseSCache(scp);

    return 0;
}
