/**
 * @file networkd.c
 * @brief networkd - drives dmnetbridge_handle_netif_rx() for every
 *        registered network interface
 *
 * One dmosi thread per interface currently registered with dmnetif at
 * startup, each running dmnetbridge_handle_netif_rx(iface) - the only
 * code path allowed to call dmnetif_receive() on a given interface once
 * this service owns it (see dmnetbridge.h's own doc comment).
 *
 * Restartable: dmnetbridge_reset() is called before spawning any pump
 * thread, so a previous instance's "which interfaces are being pumped"
 * bookkeeping in dmnetbridge can't make a fresh start think an interface
 * is already being pumped when nothing is actually reading it anymore
 * (see dmnetbridge_reset()'s own doc comment for why this exists instead
 * of a full dmod_preinit()-driven module restart).
 *
 * Scope limit: only interfaces already registered with dmnetif when
 * main() runs get a pump thread - dmnetif has no "a new interface was
 * just registered" notification today (unlike dmdevfs's hotplug callback
 * mechanism for devices), so an interface registered after networkd has
 * started is not auto-discovered. Follow-up work, not solved here.
 */
#include "dmod.h"
#include "dmnetif.h"
#include "dmnetbridge.h"
#include "dmlist.h"
#include "dmosi.h"

/**
 * @brief How often main()'s loop wakes up to check g_stop_requested while
 *        idle - not a responsiveness-critical value, just short enough
 *        that dmod_signal() doesn't take long to be noticed
 */
#define NETWORKD_MAIN_LOOP_SLEEP_MS 1000u

/**
 * @brief One interface's pump thread
 */
typedef struct
{
    dmnetif_iface_t iface;
    dmosi_thread_t  thread;
} pump_t;

/**
 * @brief Every pump_t spawned by main(), so dmod_signal() can join/stop
 *        them before returning
 */
static dmlist_context_t* g_pumps = NULL;

/**
 * @brief Set by dmod_signal(), checked by main()'s loop
 */
static volatile bool g_stop_requested = false;

/**
 * @brief Thread entry point for one interface's pump thread
 */
static void pump_thread_entry(void* arg)
{
    dmnetbridge_handle_netif_rx((dmnetif_iface_t)arg);
}

/**
 * @brief dmnetif_iterator_func_t spawning one pump_t per registered interface
 */
static bool spawn_pump(dmnetif_iface_t iface, void* user_data)
{
    (void)user_data;

    pump_t* pump = Dmod_Malloc(sizeof(*pump));
    if (pump == NULL)
    {
        DMOD_LOG_ERROR("networkd: cannot allocate pump state for '%s'\n", dmnetif_get_name(iface));
        return true; /* keep enumerating the remaining interfaces */
    }

    pump->iface = iface;
    pump->thread = dmosi_thread_create(pump_thread_entry, iface, 0, 4096, dmnetif_get_name(iface), NULL);
    if (pump->thread == NULL)
    {
        DMOD_LOG_ERROR("networkd: cannot start pump thread for '%s'\n", dmnetif_get_name(iface));
        Dmod_Free(pump);
        return true;
    }

    if (!dmlist_push_back(g_pumps, pump))
    {
        DMOD_LOG_ERROR("networkd: cannot track pump thread for '%s' - stopping it\n", dmnetif_get_name(iface));
        dmosi_thread_kill(pump->thread, 0);
        dmosi_thread_destroy(pump->thread);
        Dmod_Free(pump);
    }

    return true;
}

/**
 * @brief Join every tracked pump thread, killing it first if it hasn't
 *        stopped on its own (its interface is still present)
 */
static void stop_all_pumps(void)
{
    size_t count = dmlist_size(g_pumps);
    for (size_t i = 0; i < count; i++)
    {
        pump_t* pump = (pump_t*)dmlist_pop_front(g_pumps);

        if (dmnetif_is_present(pump->iface))
        {
            /* Still pumping - dmnetbridge_handle_netif_rx() only returns
             * once dmnetif_is_present() goes false (see its own doc
             * comment), which isn't the case here, so a cooperative join
             * would block indefinitely. */
            dmosi_thread_kill(pump->thread, 0);
        }

        dmosi_thread_join(pump->thread);
        dmosi_thread_destroy(pump->thread);
        Dmod_Free(pump);
    }
}

/**
 * @brief Requests a graceful stop - see main()'s loop
 */
int dmod_signal(int SignalNumber)
{
    (void)SignalNumber;
    g_stop_requested = true;
    return 0;
}

/**
 * @brief Main function of the application
 *
 * @return 0 on a clean stop (dmod_signal() called), negative errno if
 *         g_pumps itself could not be allocated
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_pumps = dmlist_create(Dmod_GetCurrentAllocatorName());
    if (g_pumps == NULL)
    {
        DMOD_LOG_ERROR("networkd: cannot allocate pump registry\n");
        return -1;
    }

    dmnetbridge_reset();
    dmnetif_for_each(spawn_pump, NULL);

    DMOD_LOG_INFO("networkd: pumping %u interface(s)\n", (unsigned)dmlist_size(g_pumps));

    while (!g_stop_requested)
    {
        dmosi_thread_sleep(NETWORKD_MAIN_LOOP_SLEEP_MS);
    }

    stop_all_pumps();
    dmlist_destroy(g_pumps);
    g_pumps = NULL;

    DMOD_LOG_INFO("networkd: stopped\n");
    return 0;
}
