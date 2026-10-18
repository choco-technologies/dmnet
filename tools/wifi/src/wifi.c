#include "dmod.h"
#include "dmosi.h"
#include "dmdrvi_ioctl.h"
#include <string.h>

/**
 * @brief The Wi-Fi of a network device, from the shell.
 *
 * Opens the device node (configured by dmdevfs from the board's INI) and
 * uses the standard DMDRVI_IOCTL_WIFI_* commands, so it works with every
 * Wi-Fi driver - the network itself (address, routes) is ifconfig's, as for
 * any interface.
 */

#define DEFAULT_DEVICE      "/dev/wlan0"
#define SCAN_MAX            16
#define WAIT_STEP_MS        250
#define CONNECT_WAIT_MS     15000

static void print_usage(const char *name)
{
    Dmod_Printf("Usage: %s [-d DEVICE] COMMAND [ARGS]\n", name);
    Dmod_Printf("  status                   the connection (default)\n");
    Dmod_Printf("  info                     the driver, radio, firmware, MAC, counters\n");
    Dmod_Printf("  scan                     the networks in range\n");
    Dmod_Printf("  connect SSID [PASSWORD]  connect (and again whenever lost), wait for it\n");
    Dmod_Printf("                           without PASSWORD it asks for one (not echoed)\n");
    Dmod_Printf("  disconnect               disconnect and forget the network\n");
    Dmod_Printf("DEVICE defaults to %s\n", DEFAULT_DEVICE);
}

/* A switch, not a table of name pointers: the dmod loader does not relocate
 * pointers stored in initialized data. */
static const char *auth_name(uint8_t auth)
{
    switch (auth)
    {
        case DMDRVI_WIFI_AUTH_OPEN:            return "open";
        case DMDRVI_WIFI_AUTH_WEP:             return "WEP";
        case DMDRVI_WIFI_AUTH_WPA:             return "WPA";
        case DMDRVI_WIFI_AUTH_WPA2:            return "WPA2";
        case DMDRVI_WIFI_AUTH_WPA_WPA2:        return "WPA/WPA2";
        case DMDRVI_WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case DMDRVI_WIFI_AUTH_WPA3:            return "WPA3";
        case DMDRVI_WIFI_AUTH_WPA2_WPA3:       return "WPA2/WPA3";
        default:                               return "other";
    }
}

static const char *state_name(dmdrvi_wifi_state_t state)
{
    switch (state)
    {
        case DMDRVI_WIFI_STATE_OFF:        return "off (no radio)";
        case DMDRVI_WIFI_STATE_STARTING:   return "starting";
        case DMDRVI_WIFI_STATE_IDLE:       return "not connected";
        case DMDRVI_WIFI_STATE_CONNECTING: return "connecting";
        case DMDRVI_WIFI_STATE_CONNECTED:  return "connected";
        default:                           return "unknown";
    }
}

static void print_mac(const char *label, const uint8_t mac[DMDRVI_WIFI_MAC_LEN])
{
    Dmod_Printf("%s%02x:%02x:%02x:%02x:%02x:%02x\n", label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int show_status(void *dev)
{
    dmdrvi_wifi_status_t st;
    int ret = Dmod_Ioctl(dev, DMDRVI_IOCTL_WIFI_GET_STATUS, &st);
    if (ret != 0)
        return ret;

    Dmod_Printf("state:   %s\n", state_name(st.state));
    if (st.ssid[0] != '\0')
        Dmod_Printf("network: %s\n", st.ssid);
    if (st.state == DMDRVI_WIFI_STATE_CONNECTED)
    {
        print_mac("access point: ", st.bssid);
        Dmod_Printf("channel: %u, signal %d dBm\n", st.channel, st.rssi);
    }
    if (st.reason != 0 && st.state != DMDRVI_WIFI_STATE_CONNECTED)
        Dmod_Printf("last disconnection: reason %u\n", st.reason);
    return 0;
}

static int show_info(void *dev)
{
    dmdrvi_wifi_info_t info;
    int ret = Dmod_Ioctl(dev, DMDRVI_IOCTL_WIFI_GET_INFO, &info);
    if (ret != 0)
        return ret;

    Dmod_Printf("driver:    %s\n", info.driver);
    Dmod_Printf("radio:     %s\n", info.chip[0] != '\0' ? info.chip : "not reached");
    if (info.firmware[0] != '\0')
        Dmod_Printf("firmware:  %s\n", info.firmware);
    print_mac("MAC:       ", info.mac);
    Dmod_Printf("frames:    rx %u (dropped %u), tx %u (dropped %u), errors %u\n",
                (unsigned)info.rx_frames, (unsigned)info.rx_dropped,
                (unsigned)info.tx_frames, (unsigned)info.tx_dropped, (unsigned)info.errors);
    Dmod_Printf("restarts:  %u\n", (unsigned)info.resets);
    return 0;
}

static int do_scan(void *dev)
{
    dmdrvi_wifi_ap_t   aps[SCAN_MAX];
    dmdrvi_wifi_scan_t request = { aps, SCAN_MAX, 0 };

    Dmod_Printf("scanning...\n");
    int ret = Dmod_Ioctl(dev, DMDRVI_IOCTL_WIFI_SCAN, &request);
    if (ret != 0)
        return ret;

    Dmod_Printf("%-32s  %4s  %2s  %-10s  %s\n", "SSID", "dBm", "ch", "security", "access point");
    for (uint16_t i = 0; i < request.count; i++)
    {
        const dmdrvi_wifi_ap_t *ap = &aps[i];
        Dmod_Printf("%-32s  %4d  %2u  %-10s  %02x:%02x:%02x:%02x:%02x:%02x\n",
                    ap->ssid[0] != '\0' ? ap->ssid : "(hidden)", ap->rssi, ap->channel, auth_name(ap->auth),
                    ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    }
    Dmod_Printf("%u network(s)\n", request.count);
    return 0;
}

/* Reads a password from the terminal without echoing it; false when it
 * cannot be read */
static bool ask_password(char *out, size_t size)
{
    uint32_t flags = Dmod_Stdin_GetFlags();

    Dmod_Printf("Password (Enter: none): ");
    (void)Dmod_Stdin_SetFlags((flags & ~(uint32_t)DMOD_STDIN_FLAG_ECHO) | DMOD_STDIN_FLAG_CANONICAL);
    /* Dmod_Gets leaves "text\n" and does not terminate it: zeroed first, cut
     * at the end of the line */
    memset(out, 0, size);
    char *line = Dmod_Gets(out, (int)size - 1);
    (void)Dmod_Stdin_SetFlags(flags);
    Dmod_Printf("\n");
    if (line == NULL)
        return false;
    for (size_t i = 0; i < size; i++)
    {
        if (out[i] == '\n' || out[i] == '\r')
        {
            memset(out + i, 0, size - i);
            break;
        }
    }
    return true;
}

static int do_connect(void *dev, const char *ssid, const char *password)
{
    dmdrvi_wifi_connect_t network;
    char typed[DMDRVI_WIFI_PASSWORD_MAX + 2];
    memset(&network, 0, sizeof(network));
    if (password == NULL && ask_password(typed, sizeof(typed)))
        password = typed;
    if (strlen(ssid) > DMDRVI_WIFI_SSID_MAX || (password != NULL && strlen(password) > DMDRVI_WIFI_PASSWORD_MAX))
    {
        Dmod_Printf("wifi: SSID at most %d, password at most %d characters\n",
                    DMDRVI_WIFI_SSID_MAX, DMDRVI_WIFI_PASSWORD_MAX);
        return -1;
    }
    strcpy(network.ssid, ssid);
    if (password != NULL)
        strcpy(network.password, password);
    memset(typed, 0, sizeof(typed));

    int ret = Dmod_Ioctl(dev, DMDRVI_IOCTL_WIFI_CONNECT, &network);
    if (ret != 0)
    {
        Dmod_Printf("wifi: not taken (%d) - a password has 8..64 characters\n", ret);
        return ret;
    }

    Dmod_Printf("connecting to %s...\n", ssid);
    for (uint32_t waited = 0; waited < CONNECT_WAIT_MS; waited += WAIT_STEP_MS)
    {
        dmdrvi_wifi_status_t st;
        if (Dmod_Ioctl(dev, DMDRVI_IOCTL_WIFI_GET_STATUS, &st) == 0 && st.state == DMDRVI_WIFI_STATE_CONNECTED)
            return show_status(dev);
        dmosi_thread_sleep(WAIT_STEP_MS);
    }
    Dmod_Printf("not connected yet - it keeps trying (wifi status)\n");
    return show_status(dev);
}

int main(int argc, char **argv)
{
    const char *device = DEFAULT_DEVICE;
    int i = 1;

    if (i + 1 < argc && strcmp(argv[i], "-d") == 0)
    {
        device = argv[i + 1];
        i += 2;
    }
    const char *command = (i < argc) ? argv[i] : "status";
    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "help") == 0)
    {
        print_usage(argv[0]);
        return 0;
    }

    void *dev = Dmod_FileOpen(device, "r+");
    if (dev == NULL)
    {
        Dmod_Printf("wifi: cannot open %s\n", device);
        return -1;
    }

    int ret;
    if (strcmp(command, "status") == 0)
        ret = show_status(dev);
    else if (strcmp(command, "info") == 0)
        ret = show_info(dev);
    else if (strcmp(command, "scan") == 0)
        ret = do_scan(dev);
    else if (strcmp(command, "connect") == 0 && i + 1 < argc)
        ret = do_connect(dev, argv[i + 1], (i + 2 < argc) ? argv[i + 2] : NULL);
    else if (strcmp(command, "disconnect") == 0)
        ret = Dmod_Ioctl(dev, DMDRVI_IOCTL_WIFI_DISCONNECT, NULL);
    else
    {
        print_usage(argv[0]);
        ret = -1;
    }

    if (ret != 0 && ret != -1)
        Dmod_Printf("wifi: %s failed (%d)\n", command, ret);
    Dmod_FileClose(dev);
    return ret;
}
