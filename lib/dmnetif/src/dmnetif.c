/**
 * @file dmnetif.c
 * @brief DMOD Network Interface Manager - Implementation
 *
 * Registry of registered interfaces (name -> device file) backed by dmlist,
 * guarded by a single mutex. Every function that talks to a specific
 * interface's device forwards to the generic file SAL (Dmod_FileRead/
 * _FileWrite/_Ioctl) opened once in dmnetif_register() - see dmnetif.h and
 * docs/dmnetif.md for the full rationale.
 *
 * No real network driver exercises this yet - dmeth doesn't call
 * dmnetif_register() from its dmdrvi_path_ready() implementation yet (see
 * docs/dmnetif.md's "Registration flow" section). tests/dmnetif_test.c
 * covers the registry against "/dev/null" as a stand-in device file.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmnetif.h"
#include "dmlist.h"
#include "dmdrvi.h"
#include "dmdrvi_ioctl.h"
#include "dmosi.h"
#include <string.h>
#include <errno.h>

/* Magic set to "NTIF" */
#define DMNETIF_CONTEXT_MAGIC    0x4E544946u

struct dmnetif_iface
{
    uint32_t magic;
    char     name[DMNETIF_NAME_MAX_LEN + 1];
    char*    device_path;
    void*    file;
    bool     up;
};

static dmlist_context_t* g_ifaces = NULL;
static dmosi_mutex_t     g_mutex  = NULL;

static bool is_valid_iface(dmnetif_iface_t iface)
{
    return iface != NULL && iface->magic == DMNETIF_CONTEXT_MAGIC;
}

/* dmlist_find()/_remove() call compare_func(list_element, data) - see dmlist.c */
static int compare_name(const void* data, const void* user_data)
{
    const struct dmnetif_iface* iface = (const struct dmnetif_iface*)data;
    return strcmp(iface->name, (const char*)user_data);
}

static int compare_pointer(const void* data, const void* user_data)
{
    return (data == user_data) ? 0 : -1;
}

static void close_iface(struct dmnetif_iface* iface)
{
    if (iface->up)
    {
        Dmod_Ioctl(iface->file, DMDRVI_IOCTL_NET_STOP, NULL);
    }
    Dmod_FileClose(iface->file);
    Dmod_Free(iface->device_path);
    iface->magic = 0;
    Dmod_Free(iface);
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    g_ifaces = dmlist_create(Dmod_GetCurrentAllocatorName());
    g_mutex  = dmosi_mutex_create(false);
    if (g_ifaces == NULL || g_mutex == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate dmnetif registry\n");
        return -1;
    }

    DMOD_LOG_INFO("DMNETIF interface manager initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    size_t count = dmlist_size(g_ifaces);
    for (size_t i = 0; i < count; i++)
    {
        close_iface((struct dmnetif_iface*)dmlist_pop_front(g_ifaces));
    }
    dmlist_destroy(g_ifaces);
    g_ifaces = NULL;

    dmosi_mutex_destroy(g_mutex);
    g_mutex = NULL;

    DMOD_LOG_INFO("DMNETIF interface manager deinitialized\n");
    return 0;
}

/* ---- Registration (driver-facing) ---- */

dmod_dmnetif_api_declaration(1.0, dmnetif_iface_t, _register, ( const char* name, const char* device_path ))
{
    if (name == NULL || device_path == NULL)
    {
        DMOD_LOG_ERROR("dmnetif_register: NULL name or device_path\n");
        return NULL;
    }

    if (strlen(name) > DMNETIF_NAME_MAX_LEN)
    {
        DMOD_LOG_ERROR("dmnetif_register: name '%s' longer than %d bytes\n", name, DMNETIF_NAME_MAX_LEN);
        return NULL;
    }

    struct dmnetif_iface* iface = Dmod_Malloc(sizeof(*iface));
    if (iface == NULL)
        return NULL;

    memset(iface, 0, sizeof(*iface));
    iface->magic = DMNETIF_CONTEXT_MAGIC;
    Dmod_SnPrintf(iface->name, sizeof(iface->name), "%s", name);
    iface->device_path = Dmod_StrDup(device_path);
    iface->file = (iface->device_path != NULL) ? Dmod_FileOpen(device_path, "r+") : NULL;
    if (iface->file == NULL)
    {
        DMOD_LOG_ERROR("dmnetif_register: failed to open '%s'\n", device_path);
        Dmod_Free(iface->device_path);
        Dmod_Free(iface);
        return NULL;
    }

    /* Duplicate-name check and insertion happen under the same lock, so a
     * concurrent dmnetif_register() with the same name cannot slip in
     * between the check and the insert. */
    dmosi_mutex_lock(g_mutex);
    bool duplicate = dmlist_find(g_ifaces, name, compare_name) != NULL;
    bool added = !duplicate && dmlist_push_back(g_ifaces, iface);
    dmosi_mutex_unlock(g_mutex);

    if (!added)
    {
        if (duplicate)
        {
            DMOD_LOG_ERROR("dmnetif_register: interface '%s' already registered\n", name);
        }
        Dmod_FileClose(iface->file);
        Dmod_Free(iface->device_path);
        Dmod_Free(iface);
        return NULL;
    }

    DMOD_LOG_INFO("Registered network interface '%s' -> %s\n", name, device_path);
    return iface;
}

dmod_dmnetif_api_declaration(1.0, void, _unregister, ( dmnetif_iface_t iface ))
{
    if (!is_valid_iface(iface))
        return;

    dmosi_mutex_lock(g_mutex);
    dmlist_remove(g_ifaces, iface, compare_pointer);
    dmosi_mutex_unlock(g_mutex);

    close_iface(iface);
}

/* ---- Lookup / enumeration ---- */

dmod_dmnetif_api_declaration(1.0, dmnetif_iface_t, _find_by_name, ( const char* name ))
{
    if (name == NULL)
        return NULL;

    dmosi_mutex_lock(g_mutex);
    void* found = dmlist_find(g_ifaces, name, compare_name);
    dmosi_mutex_unlock(g_mutex);
    return (dmnetif_iface_t)found;
}

dmod_dmnetif_api_declaration(1.0, size_t, _count, ( void ))
{
    dmosi_mutex_lock(g_mutex);
    size_t count = dmlist_size(g_ifaces);
    dmosi_mutex_unlock(g_mutex);
    return count;
}

typedef struct
{
    dmnetif_iterator_func_t callback;
    void*                   user_data;
} for_each_ctx_t;

static bool for_each_trampoline(void* data, void* user_data)
{
    for_each_ctx_t* ctx = (for_each_ctx_t*)user_data;
    return ctx->callback((dmnetif_iface_t)data, ctx->user_data);
}

dmod_dmnetif_api_declaration(1.0, void, _for_each, ( dmnetif_iterator_func_t callback, void* user_data ))
{
    if (callback == NULL)
        return;

    for_each_ctx_t ctx = { .callback = callback, .user_data = user_data };
    dmosi_mutex_lock(g_mutex);
    dmlist_foreach(g_ifaces, for_each_trampoline, &ctx);
    dmosi_mutex_unlock(g_mutex);
}

dmod_dmnetif_api_declaration(1.0, const char*, _get_name, ( dmnetif_iface_t iface ))
{
    return is_valid_iface(iface) ? iface->name : NULL;
}

/* ---- State control ---- */

dmod_dmnetif_api_declaration(1.0, int, _up, ( dmnetif_iface_t iface ))
{
    if (!is_valid_iface(iface))
        return -EINVAL;

    int ret = Dmod_Ioctl(iface->file, DMDRVI_IOCTL_NET_START, NULL);
    if (ret == 0)
        iface->up = true;
    return ret;
}

dmod_dmnetif_api_declaration(1.0, int, _down, ( dmnetif_iface_t iface ))
{
    if (!is_valid_iface(iface))
        return -EINVAL;

    int ret = Dmod_Ioctl(iface->file, DMDRVI_IOCTL_NET_STOP, NULL);
    iface->up = false;
    return ret;
}

dmod_dmnetif_api_declaration(1.0, bool, _is_up, ( dmnetif_iface_t iface ))
{
    return is_valid_iface(iface) && iface->up;
}

dmod_dmnetif_api_declaration(1.0, dmnetif_link_status_t, _get_link_status, ( dmnetif_iface_t iface ))
{
    if (!is_valid_iface(iface))
        return dmnetif_link_down;

    dmdrvi_net_link_status_t status = DMDRVI_NET_LINK_DOWN;
    if (Dmod_Ioctl(iface->file, DMDRVI_IOCTL_NET_GET_LINK_STATUS, &status) != 0)
        return dmnetif_link_down;

    return (status == DMDRVI_NET_LINK_UP) ? dmnetif_link_up : dmnetif_link_down;
}

/* ---- MAC address ---- */

dmod_dmnetif_api_declaration(1.0, int, _get_mac_address, ( dmnetif_iface_t iface, dmnetif_mac_addr_t* mac ))
{
    if (!is_valid_iface(iface) || mac == NULL)
        return -EINVAL;

    dmdrvi_net_mac_addr_t addr;
    int ret = Dmod_Ioctl(iface->file, DMDRVI_IOCTL_NET_GET_MAC_ADDR, &addr);
    if (ret == 0)
    {
        memcpy(mac->addr, addr.addr, DMNETIF_MAC_ADDR_LEN);
    }
    return ret;
}

dmod_dmnetif_api_declaration(1.0, int, _set_mac_address, ( dmnetif_iface_t iface, const dmnetif_mac_addr_t* mac ))
{
    if (!is_valid_iface(iface) || mac == NULL)
        return -EINVAL;

    dmdrvi_net_mac_addr_t addr;
    memcpy(addr.addr, mac->addr, DMNETIF_MAC_ADDR_LEN);
    return Dmod_Ioctl(iface->file, DMDRVI_IOCTL_NET_SET_MAC_ADDR, &addr);
}

/* ---- Frame I/O ---- */

dmod_dmnetif_api_declaration(1.0, size_t, _send, ( dmnetif_iface_t iface, const void* frame, size_t length ))
{
    if (!is_valid_iface(iface) || frame == NULL || length == 0 || !iface->up)
        return 0;

    return Dmod_FileWrite(frame, 1, length, iface->file);
}

dmod_dmnetif_api_declaration(1.0, size_t, _receive, ( dmnetif_iface_t iface, void* buffer, size_t size ))
{
    if (!is_valid_iface(iface) || buffer == NULL || size == 0 || !iface->up)
        return 0;

    return Dmod_FileRead(buffer, 1, size, iface->file);
}

/* ---- Escape hatch ---- */

dmod_dmnetif_api_declaration(1.0, int, _ioctl, ( dmnetif_iface_t iface, int command, void* arg ))
{
    if (!is_valid_iface(iface))
        return -EINVAL;

    return Dmod_Ioctl(iface->file, command, arg);
}
