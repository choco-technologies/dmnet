#include "dmod.h"
#include "dmosi.h"
#include "dmdrvi_ioctl.h"
#include "dmsecrets.h"
#include <errno.h>
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
    Dmod_Printf("  connect SSID [PASSWORD] -s   ... and save the network, so that wifid connects by itself\n");
    Dmod_Printf("  save SSID [PASSWORD]     save a network (in the \"wifi\" vault of dmsecrets)\n");
    Dmod_Printf("  saved                    the saved networks\n");
    Dmod_Printf("  forget SSID              remove a saved network\n");
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

/* Reads a line from the terminal without echoing it; false when it
 * cannot be read */
static bool ask_line(const char *prompt, char *out, size_t size)
{
    uint32_t flags = Dmod_Stdin_GetFlags();

    Dmod_Printf("%s", prompt);
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

static bool ask_password(char *out, size_t size)
{
    return ask_line("Password (Enter: none): ", out, size);
}

/* ---- The vault of saved networks: records "wifi/<SSID>" = the password ----
 * Opened with the device key (what wifid uses); where the system has none, the
 * passphrase is asked for - but then wifid cannot open the vault. */

#define VAULT_PREFIX    "wifi/"

static int vault_passphrase(void *context, bool creating, void *passphrase, size_t size, size_t *length)
{
    char first[100], again[100];

    (void)context;
    if (size > sizeof(first) - 1)
        size = sizeof(first) - 1;
    if (!ask_line(creating ? "New vault of networks - passphrase: " : "Vault passphrase: ", first, sizeof(first)))
        return -ECANCELED;
    if (creating)
    {
        if (!ask_line("Again: ", again, sizeof(again)) || strcmp(first, again) != 0)
        {
            memset(first, 0, sizeof(first));
            memset(again, 0, sizeof(again));
            return -EINVAL;
        }
        memset(again, 0, sizeof(again));
    }
    *length = strlen(first);
    if (*length == 0 || *length > size)
    {
        memset(first, 0, sizeof(first));
        return -EINVAL;
    }
    memcpy(passphrase, first, *length);
    memset(first, 0, sizeof(first));
    return 0;
}

static dmsecrets_t open_vault(bool create)
{
    dmsecrets_config_t config;
    dmsecrets_t vault = NULL;

    memset(&config, 0, sizeof(config));
    config.domain = "wifi";
    config.flags = DMSECRETS_OPEN_DEVICE_KEY | (create ? DMSECRETS_OPEN_CREATE : 0);
    config.passphrase_provider = vault_passphrase;
    int ret = dmsecrets_open(&config, &vault);
    if (ret != 0)
    {
        Dmod_Printf("wifi: cannot open the vault (%d): %s\n", ret,
                    ret == -ENOENT ? "no path (DMSECRETS_WIFI_PATH / DMSECRETS_PATH) or no such vault (wifi save makes it)"
                                   : "not a vault, or busy");
        return NULL;
    }
    return vault;
}

static int save_network(const char *ssid, const char *password)
{
    char name[DMSECRETS_NAME_MAX + 1];

    if (strlen(ssid) == 0 || strlen(ssid) > DMDRVI_WIFI_SSID_MAX)
    {
        Dmod_Printf("wifi: SSID at most %d characters\n", DMDRVI_WIFI_SSID_MAX);
        return -1;
    }
    dmsecrets_t vault = open_vault(true);
    if (vault == NULL)
        return -1;
    strcpy(name, VAULT_PREFIX);
    strcat(name, ssid);
    int ret = dmsecrets_set(vault, name, password, strlen(password));
    dmsecrets_close(vault);
    if (ret != 0)
        Dmod_Printf("wifi: not saved (%d)%s\n", ret, ret == -ENODEV ? " - no entropy source for dmcrypto" : "");
    else
        Dmod_Printf("saved %s\n", ssid);
    return ret;
}

static int list_saved(void)
{
    char name[DMSECRETS_NAME_MAX + 1];
    size_t count = 0;

    dmsecrets_t vault = open_vault(false);
    if (vault == NULL)
        return -1;
    int ret = dmsecrets_count(vault, VAULT_PREFIX, &count);
    for (size_t n = 0; ret == 0 && n < count; n++)
    {
        ret = dmsecrets_name_at(vault, VAULT_PREFIX, n, name, sizeof(name));
        if (ret == 0)
            Dmod_Printf("%s\n", name + sizeof(VAULT_PREFIX) - 1);
    }
    if (ret == 0 && count == 0)
        Dmod_Printf("(none)\n");
    dmsecrets_close(vault);
    return ret;
}

static int forget_network(const char *ssid)
{
    char name[DMSECRETS_NAME_MAX + 1];

    if (strlen(ssid) == 0 || strlen(ssid) > DMDRVI_WIFI_SSID_MAX)
        return -1;
    dmsecrets_t vault = open_vault(false);
    if (vault == NULL)
        return -1;
    strcpy(name, VAULT_PREFIX);
    strcat(name, ssid);
    int ret = dmsecrets_delete(vault, name);
    dmsecrets_close(vault);
    if (ret == 0)
        Dmod_Printf("forgot %s\n", ssid);
    return ret;
}

static int do_connect(void *dev, const char *ssid, const char *password, bool save)
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
        {
            int shown = show_status(dev);
            if (save)
                (void)save_network(ssid, network.password);     /* only a network that worked is saved */
            memset(&network, 0, sizeof(network));
            return shown;
        }
        dmosi_thread_sleep(WAIT_STEP_MS);
    }
    memset(&network, 0, sizeof(network));
    Dmod_Printf("not connected yet - it keeps trying (wifi status)%s\n", save ? "; not saved" : "");
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
    bool save = false;
    for (int a = i + 1; a < argc; a++)          /* "-s" after the command: save the network */
    {
        if (strcmp(argv[a], "-s") == 0)
        {
            save = true;
            for (int b = a; b + 1 < argc; b++)
                argv[b] = argv[b + 1];
            argc--;
            break;
        }
    }
    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "help") == 0)
    {
        print_usage(argv[0]);
        return 0;
    }

    /* The vault of saved networks needs no device */
    if (strcmp(command, "saved") == 0)
        return list_saved();
    if (strcmp(command, "forget") == 0 && i + 1 < argc)
        return forget_network(argv[i + 1]);
    if (strcmp(command, "save") == 0 && i + 1 < argc)
    {
        char typed[DMDRVI_WIFI_PASSWORD_MAX + 2];
        const char *password = (i + 2 < argc) ? argv[i + 2] : NULL;
        if (password == NULL)
        {
            if (!ask_password(typed, sizeof(typed)))
                return -1;
            password = typed;
        }
        int saved = (strlen(password) <= DMDRVI_WIFI_PASSWORD_MAX) ? save_network(argv[i + 1], password) : -1;
        memset(typed, 0, sizeof(typed));
        return saved;
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
        ret = do_connect(dev, argv[i + 1], (i + 2 < argc) ? argv[i + 2] : NULL, save);
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
