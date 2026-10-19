#include "dmod.h"
#include "dmosi.h"
#include "dmnetif.h"
#include "dmdrvi_ioctl.h"
#include "dmsecrets.h"
#include <errno.h>
#include <string.h>

/**
 * @brief wifid - connects a Wi-Fi interface to a saved network by itself.
 *
 * Started by libsystemd for every interface dmnetif registers (wifid.rules, like
 * networkd and dhcp); on an interface that is not a Wi-Fi one it ends at once. On a
 * Wi-Fi one it keeps the interface connected: from the networks saved in the "wifi"
 * vault of dmsecrets (records "wifi/<SSID>" = the password; `wifi save`) it takes
 * the one in range with the strongest signal and connects to it, and again when the
 * connection has been gone for a while.
 *
 * The vault is opened with the device key (DMSECRETS_OPEN_DEVICE_KEY): nobody types
 * anything. Its path is DMSECRETS_WIFI_PATH, else DMSECRETS_PATH.
 */

#define SCAN_MAX            16
#define VAULT_PREFIX        "wifi/"
#define POLL_MS             5000u       /* How often the connection is looked at */
#define CONNECT_WAIT_MS     20000u      /* How long a connection attempt is given */
#define LOST_MS             30000u      /* Not connected for so long: take a network again */
#define NO_NETWORK_MS       15000u      /* No saved network in range: look again after */
#define NO_VAULT_MS         30000u      /* No vault (yet): the card may not be there yet */

static void log_line(const char *iface, const char *text, const char *detail)
{
    Dmod_Printf("wifid %s: %s%s%s\n", iface, text, detail != NULL ? " " : "", detail != NULL ? detail : "");
}

/* Picks the saved network in range with the strongest signal: 0 and the credentials, or -ENOENT */
static int pick_network(dmsecrets_t vault, const dmdrvi_wifi_ap_t *aps, size_t ap_count, dmdrvi_wifi_connect_t *out)
{
    size_t saved = 0;
    int best_rssi = -1000;
    bool found = false;
    char name[DMSECRETS_NAME_MAX + 1];

    int result = dmsecrets_count(vault, VAULT_PREFIX, &saved);
    if (result != 0)
        return result;

    for (size_t i = 0; i < saved; i++)
    {
        if (dmsecrets_name_at(vault, VAULT_PREFIX, i, name, sizeof(name)) != 0)
            continue;
        const char *ssid = name + (sizeof(VAULT_PREFIX) - 1);
        for (size_t a = 0; a < ap_count; a++)
        {
            if (strcmp(aps[a].ssid, ssid) == 0 && aps[a].rssi > best_rssi)
            {
                size_t length = 0;
                memset(out, 0, sizeof(*out));
                if (strlen(ssid) > DMDRVI_WIFI_SSID_MAX ||
                    dmsecrets_get(vault, name, out->password, DMDRVI_WIFI_PASSWORD_MAX, &length) != 0)
                    continue;
                out->password[length] = '\0';
                memcpy(out->ssid, ssid, strlen(ssid) + 1);
                best_rssi = aps[a].rssi;
                found = true;
            }
        }
    }
    return found ? 0 : -ENOENT;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        Dmod_Printf("Usage: %s INTERFACE\n", argv[0]);
        return -1;
    }
    const char *name = argv[1];
    dmnetif_iface_t iface = dmnetif_find_by_name(name);
    if (iface == NULL)
    {
        log_line(name, "no such interface", NULL);
        return -1;
    }

    dmdrvi_wifi_status_t status;
    if (dmnetif_ioctl(iface, DMDRVI_IOCTL_WIFI_GET_STATUS, &status) != 0)
        return 0;                               /* Not a Wi-Fi interface: nothing to do */

    dmsecrets_config_t config;
    dmsecrets_t vault = NULL;
    memset(&config, 0, sizeof(config));
    config.domain = "wifi";
    config.flags = DMSECRETS_OPEN_DEVICE_KEY | DMSECRETS_OPEN_READONLY;

    int result;
    bool said_no_vault = false;
    while ((result = dmsecrets_open(&config, &vault)) == -ENOENT)
    {
        if (!said_no_vault)
            log_line(name, "no vault of saved networks (DMSECRETS_WIFI_PATH / DMSECRETS_PATH, `wifi save`) - waiting", NULL);
        said_no_vault = true;
        dmosi_thread_sleep(NO_VAULT_MS);
    }
    if (result != 0)
    {
        log_line(name, "cannot open the vault", result == -EBADMSG ? "(not a vault)" : NULL);
        return result;
    }

    uint32_t lost_since = 0;
    bool said_no_network = false;
    for (;;)
    {
        dmdrvi_wifi_ap_t aps[SCAN_MAX];
        dmdrvi_wifi_scan_t scan;
        dmdrvi_wifi_connect_t network;
        uint32_t now = dmosi_get_tick_count();

        if (dmnetif_ioctl(iface, DMDRVI_IOCTL_WIFI_GET_STATUS, &status) != 0)
        {
            dmosi_thread_sleep(POLL_MS);
            continue;
        }
        if (status.state == DMDRVI_WIFI_STATE_CONNECTED)
        {
            lost_since = 0;
            said_no_network = false;
            dmosi_thread_sleep(POLL_MS);
            continue;
        }
        if (status.state == DMDRVI_WIFI_STATE_OFF || status.state == DMDRVI_WIFI_STATE_STARTING)
        {
            dmosi_thread_sleep(POLL_MS);        /* The radio is not there yet */
            continue;
        }

        /* Connecting by itself (the network it was given) is given LOST_MS before another is taken */
        if (lost_since == 0)
            lost_since = now ? now : 1;
        if (status.state == DMDRVI_WIFI_STATE_CONNECTING && (uint32_t)(now - lost_since) < LOST_MS)
        {
            dmosi_thread_sleep(POLL_MS);
            continue;
        }

        memset(&scan, 0, sizeof(scan));
        scan.aps = aps;
        scan.max = SCAN_MAX;
        result = dmnetif_ioctl(iface, DMDRVI_IOCTL_WIFI_SCAN, &scan);
        if (result == 0)
            result = pick_network(vault, aps, scan.count, &network);
        if (result == -EACCES || result == -ENODEV)
        {
            log_line(name, "cannot unlock the vault - is the device key (/dev/devkey) there?", NULL);
            break;
        }
        if (result != 0)
        {
            if (!said_no_network)
                log_line(name, "no saved network in range", NULL);
            said_no_network = true;
            dmosi_thread_sleep(NO_NETWORK_MS);
            continue;
        }
        said_no_network = false;

        log_line(name, "connecting to", network.ssid);
        if (dmnetif_ioctl(iface, DMDRVI_IOCTL_WIFI_CONNECT, &network) != 0)
        {
            log_line(name, "the radio did not take", network.ssid);
            memset(&network, 0, sizeof(network));
            dmosi_thread_sleep(NO_NETWORK_MS);
            continue;
        }
        memset(&network, 0, sizeof(network));

        for (uint32_t waited = 0; waited < CONNECT_WAIT_MS; waited += 1000)
        {
            dmosi_thread_sleep(1000);
            if (dmnetif_ioctl(iface, DMDRVI_IOCTL_WIFI_GET_STATUS, &status) == 0 &&
                status.state == DMDRVI_WIFI_STATE_CONNECTED)
            {
                log_line(name, "connected to", status.ssid);
                break;
            }
        }
        lost_since = dmosi_get_tick_count();
        if (lost_since == 0)
            lost_since = 1;
    }

    dmsecrets_close(vault);
    return -1;
}
