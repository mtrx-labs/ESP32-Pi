#include <Arduino.h>
#include <WiFi.h>
#include <Network.h>
#include <WebServer.h>
#include <Preferences.h>
#include <FFat.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <vector>
#include <algorithm>
#include <cstring>
#include <cctype>

#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <lwip/sockets.h>
#include <fcntl.h>
#include <unistd.h>

#include "dashboard.h"
#define FW_VERSION "0.4"

// 1 = upstream Wi-Fi link negotiates 40 MHz when the router allows it.
// Set to 0 if the link becomes unstable in a crowded 2.4 GHz band.
#define WIFI_STA_HT40 1

// 1 = print every DNS query to Serial (slows DNS, debugging only)
#define DNS_DEBUG 0


// ============================================================
// 1. USER CONFIGURATION
// ============================================================

static const char *AP_SSID = "ESP32_WiFi";
static constexpr char AP_PASSWORD[] = "esp32_wifi";

static const char *DASH_USER = "admin";
static const char *DASH_PASSWORD = "esp32_login";

static_assert(
    sizeof(AP_PASSWORD) >= 9 && sizeof(AP_PASSWORD) <= 64,
    "AP_PASSWORD must be 8-63 characters"
);

static constexpr uint8_t MAX_SAVED_NETWORKS = 5;
static constexpr size_t MAX_MANUAL_DOMAINS = 512;

static const char *BLOCK_FILE = "/blocked.txt";
static const char *ALLOW_FILE = "/allowed.txt";

static const char *HAGEZI_URL =
    "https://cdn.jsdelivr.net/gh/hagezi/dns-blocklists@latest/adblock/pro.txt";

static const char *HAGEZI_FILE = "/hagezi-pro.txt";
static const char *HAGEZI_TEMP = "/hagezi-pro.tmp";
static const char *HAGEZI_BACKUP = "/hagezi-pro.bak";


// ============================================================
// 2. LIMITS AND TIMINGS
// ============================================================

static constexpr size_t DNS_MAX_PACKET = 1232;
static constexpr size_t DNS_MAX_QUERY = 512;
static constexpr int DNS_MAX_PENDING = 32;

static constexpr uint32_t DNS_TIMEOUT_MS = 2500;
static constexpr uint32_t DNS_RETRY_SECOND_MS = 250;   // try 2nd upstream
static constexpr uint32_t DNS_RETRY_BOTH_MS = 700;     // try both

// DNS cache (PSRAM): 256 sets x 4 ways = 1024 entries, ~540 KB
static constexpr uint32_t CACHE_SETS = 256;
static constexpr uint32_t CACHE_WAYS = 4;
static constexpr uint32_t CACHE_ENTRIES = CACHE_SETS * CACHE_WAYS;
static constexpr uint32_t CACHE_MAX_RESPONSE = 512;
static constexpr uint32_t CACHE_MIN_TTL = 20;
static constexpr uint32_t CACHE_MAX_TTL = 21600;
static constexpr uint32_t CACHE_NEG_MAX_TTL = 900;
static constexpr uint32_t CACHE_NEG_DEFAULT_TTL = 60;
static constexpr uint32_t CACHE_STALE_WINDOW = 3600;   // seconds
static constexpr uint32_t CACHE_STALE_TTL = 5;         // TTL sent with stale answers

static constexpr uint32_t BLOCK_TTL = 60;

static constexpr uint32_t MANUAL_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t AUTO_ATTEMPT_MS = 10000;
static constexpr uint32_t AUTO_CYCLE_PAUSE_MS = 30000;

static constexpr uint32_t HAGEZI_MAX_DOWNLOAD = 6 * 1024 * 1024;
static constexpr uint32_t HAGEZI_MIN_DOWNLOAD = 1000000;

static constexpr uint32_t MAX_HAGEZI_DOMAINS = 350000;

static constexpr uint32_t HAGEZI_HASH_SLOTS = 1UL << 19;       // 4 MB
static constexpr uint32_t HAGEZI_EXCEPTION_SLOTS = 1UL << 17;  // 1 MB

static constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
static constexpr uint64_t FNV_PRIME = 1099511628211ULL;


// ============================================================
// 3. GLOBAL OBJECTS
// ============================================================

WebServer server(80);
Preferences preferences;

SemaphoreHandle_t domainMutex = nullptr;
SemaphoreHandle_t filesystemMutex = nullptr;

std::vector<String> blockedDomains;
std::vector<String> allowedDomains;

// Sorted reverse-suffix hashes of the manual lists (DNS fast path).
static std::vector<uint64_t> manualBlockHashes;
static std::vector<uint64_t> manualAllowHashes;

bool filesystemReady = false;
volatile bool dnsStarted = false;

volatile uint32_t totalQueries = 0;
volatile uint32_t blockedQueries = 0;
volatile uint32_t forwardedQueries = 0;
volatile uint32_t failedQueries = 0;
volatile uint32_t cachedQueries = 0;
volatile uint32_t aaaaSuppressed = 0;
volatile uint32_t latencyEwmaMs = 0;

static volatile bool cacheFlushRequested = false;
static volatile uint32_t restartAt = 0;

// ---- upstream DNS selection ----

enum UpstreamMode : uint8_t {
    UP_CLOUDFLARE = 0,
    UP_GOOGLE = 1,
    UP_QUAD9 = 2,
    UP_ROUTER = 3
};

#define IP4(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

struct Provider {
    const char *code;
    const char *name;
    uint32_t a;
    uint32_t b;
};

static const Provider PROVIDERS[] = {
    {"cloudflare", "Cloudflare",   IP4(1, 1, 1, 1), IP4(1, 0, 0, 1)},
    {"google",     "Google",       IP4(8, 8, 8, 8), IP4(8, 8, 4, 4)},
    {"quad9",      "Quad9",        IP4(9, 9, 9, 9), IP4(149, 112, 112, 112)},
    {"router",     "Router / ISP", 0, 0}
};

static volatile uint8_t upstreamMode = UP_CLOUDFLARE;
static volatile uint32_t upstreamAddr[2] = {IP4(1, 1, 1, 1), IP4(1, 0, 0, 1)};
static volatile bool blockAAAA = true;

// ============================================================
// 4. HAGEZI STATE
// ============================================================

// Reverse-suffix 64-bit FNV-1a hashes, open addressing, in PSRAM.
// Main table: 4 MB | Exception table: 1 MB

static uint64_t *hageziBlockIndex = nullptr;
static uint64_t *hageziExceptionIndex = nullptr;

static volatile uint32_t hageziLoadedDomains = 0;
static volatile uint32_t hageziLoadedExceptions = 0;

enum HaGeZiState : uint8_t {
    HAGEZI_IDLE = 0,
    HAGEZI_DOWNLOADING = 1,
    HAGEZI_SUCCESS = 2,
    HAGEZI_FAILED = 3,
    HAGEZI_INDEXING = 4
};

static volatile uint8_t hageziState = HAGEZI_IDLE;
static volatile uint32_t hageziUpdateCount = 0;

static char hageziMessage[160] = "No downloaded blocklist.";

portMUX_TYPE hageziStatusMux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t fileReadBuffer[4096];   // used only while holding the FS lock

static uint32_t fsTotalCached = 0;
static uint32_t fsUsedCached = 0;
static volatile bool fsStatsStale = true;
static uint32_t fsStatsAt = 0;


// ============================================================
// 5. WIFI CONNECTION STATE
// ============================================================

enum ConnectState : uint8_t {
    CONN_IDLE,
    CONN_CONNECTING,
    CONN_OK,
    CONN_FAILED
};

ConnectState connectState = CONN_IDLE;

volatile bool naptEnabled = false;

String connectMessage;

String pendingSSID;
String pendingPassword;

String previousSSID;
String previousPassword;

uint32_t connectStarted = 0;

bool autoActive = false;

uint32_t autoStarted = 0;
uint32_t nextAutoAttempt = 0;

int autoCursor = 0;


// ============================================================
// 6. DASHBOARD
// ============================================================


static const char ASSET_ETAG[] = "\"esp32pi-" FW_VERSION "-" __DATE__ "-" __TIME__ "\"";


// ============================================================
// 7. GENERAL UTILITIES
// ============================================================

String jsonEscape(const String &value) {

    String result;
    result.reserve(value.length() + 8);

    for (size_t i = 0; i < value.length(); i++) {

        char c = value[i];

        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (c == '\n') {
            result += "\\n";
        } else if (c == '\r') {
            result += "\\r";
        } else if ((uint8_t)c >= 32) {
            result += c;
        }
    }

    return result;
}


String normalizeDomain(String domain) {

    domain.trim();
    domain.toLowerCase();

    while (domain.endsWith(".")) {
        domain.remove(domain.length() - 1);
    }

    return domain;
}


bool validDomainCString(const char *domain) {

    if (!domain) return false;

    size_t length = strlen(domain);

    if (length == 0 || length > 253) return false;

    size_t labelLength = 0;
    bool previousHyphen = false;

    for (size_t i = 0; i < length; i++) {

        char c = domain[i];

        if (c == '.') {
            if (labelLength == 0 || previousHyphen) return false;
            labelLength = 0;
            previousHyphen = false;
            continue;
        }

        bool alphanumeric =
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9');

        if (c == '-') {
            if (labelLength == 0) return false;
            previousHyphen = true;
        } else if (alphanumeric) {
            previousHyphen = false;
        } else {
            return false;
        }

        labelLength++;

        if (labelLength > 63) return false;
    }

    return labelLength > 0 && !previousHyphen;
}


bool validDomain(const String &domain) {
    return validDomainCString(domain.c_str());
}


// ============================================================
// 8. HASHING (shared by manual lists and HaGeZi)
// ============================================================

// Hashes are computed from the LAST character to the FIRST. That way
// the hash of every parent domain ("example.com" of "a.b.example.com")
// is produced incrementally in a single pass over the hostname.

static inline uint64_t fixHash(uint64_t h) {
    return h ? h : 1;
}

static uint64_t hashReverse(const char *s, size_t len) {

    uint64_t h = FNV_OFFSET;

    while (len--) {
        h ^= (uint8_t)s[len];
        h *= FNV_PRIME;
    }

    return fixHash(h);
}

static inline uint32_t slotIndex(uint64_t h, uint32_t mask) {
    return ((uint32_t)(h >> 32) ^ (uint32_t)h) & mask;
}

// Returns true if the hash was stored or already present.
bool insertHash(
    uint64_t *table,
    uint32_t slots,
    uint64_t hash,
    bool &inserted
) {

    inserted = false;

    if (!table || slots == 0) return false;

    uint32_t mask = slots - 1;
    uint32_t index = slotIndex(hash, mask);

    for (uint32_t i = 0; i < slots; i++) {

        uint64_t slot = table[index];

        if (slot == 0) {
            table[index] = hash;
            inserted = true;
            return true;
        }

        if (slot == hash) return true;

        index = (index + 1) & mask;
    }

    return false;
}


bool containsHash(const uint64_t *table, uint32_t slots, uint64_t hash) {

    uint32_t mask = slots - 1;
    uint32_t index = slotIndex(hash, mask);

    for (uint32_t i = 0; i < slots; i++) {

        uint64_t slot = table[index];

        if (slot == 0) return false;
        if (slot == hash) return true;

        index = (index + 1) & mask;
    }

    return false;
}


// True if the host or any parent domain is present in the table.
static bool lookupIndexed(
    const char *host,
    size_t len,
    const uint64_t *table,
    uint32_t slots
) {

    if (!table || len == 0) return false;

    uint64_t h = FNV_OFFSET;

    for (size_t i = len; i-- > 0;) {

        h ^= (uint8_t)host[i];
        h *= FNV_PRIME;

        // Only test at label boundaries: "com", "example.com", ...
        if (i == 0 || host[i - 1] == '.') {
            if (containsHash(table, slots, fixHash(h))) return true;
        }
    }

    return false;
}


static bool lookupSorted(
    const char *host,
    size_t len,
    const std::vector<uint64_t> &sorted
) {

    if (sorted.empty() || len == 0) return false;

    uint64_t h = FNV_OFFSET;

    for (size_t i = len; i-- > 0;) {

        h ^= (uint8_t)host[i];
        h *= FNV_PRIME;

        if (i == 0 || host[i - 1] == '.') {
            if (std::binary_search(sorted.begin(), sorted.end(), fixHash(h))) {
                return true;
            }
        }
    }

    return false;
}


// Caller must hold domainMutex (or run before DNS starts).
static void rebuildManualHashes() {

    manualBlockHashes.clear();
    manualAllowHashes.clear();

    for (const String &d : blockedDomains) {
        manualBlockHashes.push_back(hashReverse(d.c_str(), d.length()));
    }

    for (const String &d : allowedDomains) {
        manualAllowHashes.push_back(hashReverse(d.c_str(), d.length()));
    }

    std::sort(manualBlockHashes.begin(), manualBlockHashes.end());
    std::sort(manualAllowHashes.begin(), manualAllowHashes.end());
}


// ============================================================
// 9. FILESYSTEM: FFAT
// ============================================================

bool takeFilesystemLock(uint32_t timeoutMs = 1000) {

    if (!filesystemReady || filesystemMutex == nullptr) return false;

    return xSemaphoreTake(filesystemMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}


void releaseFilesystemLock() {

    if (filesystemMutex) xSemaphoreGive(filesystemMutex);
}


void recoverManualFiles() {

    if (!filesystemReady) return;
    if (!takeFilesystemLock()) return;

    const char *files[] = {BLOCK_FILE, ALLOW_FILE};

    for (const char *path : files) {

        String temporary = String(path) + ".tmp";
        String backup = String(path) + ".bak";

        if (!FFat.exists(path) && FFat.exists(backup)) {
            FFat.rename(backup, path);
        }

        if (FFat.exists(backup)) FFat.remove(backup);
        if (FFat.exists(temporary)) FFat.remove(temporary);
    }

    releaseFilesystemLock();
}


bool saveDomainFile(const char *path, const std::vector<String> &domains) {

    if (!takeFilesystemLock(200)) return false;

    String temporary = String(path) + ".tmp";
    String backup = String(path) + ".bak";

    FFat.remove(temporary);

    File file = FFat.open(temporary, "w");

    if (!file) {
        releaseFilesystemLock();
        return false;
    }

    bool success = true;

    for (const String &domain : domains) {

        if (file.print(domain) != domain.length() || file.write('\n') != 1) {
            success = false;
            break;
        }
    }

    file.flush();
    file.close();

    if (!success) {
        FFat.remove(temporary);
        releaseFilesystemLock();
        return false;
    }

    FFat.remove(backup);

    bool hadOriginal = FFat.exists(path);

    if (hadOriginal && !FFat.rename(path, backup)) {
        FFat.remove(temporary);
        releaseFilesystemLock();
        return false;
    }

    if (!FFat.rename(temporary, path)) {
        if (hadOriginal) FFat.rename(backup, path);
        releaseFilesystemLock();
        return false;
    }

    if (FFat.exists(backup)) FFat.remove(backup);

    releaseFilesystemLock();

    return true;
}


void loadDomainFile(
    const char *path,
    std::vector<String> &domains,
    const char *defaults[],
    size_t defaultCount
) {

    domains.clear();

    if (!filesystemReady) {
        for (size_t i = 0; i < defaultCount; i++) domains.emplace_back(defaults[i]);
        return;
    }

    if (!takeFilesystemLock()) return;

    if (!FFat.exists(path)) {

        for (size_t i = 0; i < defaultCount; i++) domains.emplace_back(defaults[i]);

        releaseFilesystemLock();
        saveDomainFile(path, domains);

        return;
    }

    File file = FFat.open(path, "r");

    if (!file) {
        releaseFilesystemLock();
        return;
    }

    while (file.available() && domains.size() < MAX_MANUAL_DOMAINS) {

        String line = normalizeDomain(file.readStringUntil('\n'));

        if (validDomain(line) &&
            std::find(domains.begin(), domains.end(), line) == domains.end()) {

            domains.push_back(line);
        }
    }

    file.close();

    releaseFilesystemLock();
}


void initializeFilesystem() {

    if (!filesystemMutex) filesystemMutex = xSemaphoreCreateMutex();

    if (!filesystemMutex) {
        Serial.println("ERROR: Filesystem mutex creation failed.");
        return;
    }

    // Partition label is "ffat" in the Arduino ESP32 FATFS scheme.
    if (FFat.begin(true)) {

        filesystemReady = true;

        Serial.printf(
            "FFat mounted: %u KB used / %u KB total\n",
            (unsigned)(FFat.usedBytes() / 1024),
            (unsigned)(FFat.totalBytes() / 1024)
        );

    } else {

        filesystemReady = false;
        Serial.println("ERROR: FFat could not be mounted.");
        return;
    }

    recoverManualFiles();

    static const char *defaultBlocked[] = {
        "doubleclick.net",
        "googlesyndication.com",
        "googleadservices.com",
        "adservice.google.com",
        "adservice.google.co.in",
        "adnxs.com",
        "adsrvr.org",
        "adform.net",
        "taboola.com",
        "outbrain.com",
        "scorecardresearch.com"
    };

    static const char *defaultAllowed[] = {"localhost"};

    loadDomainFile(
        BLOCK_FILE, blockedDomains, defaultBlocked,
        sizeof(defaultBlocked) / sizeof(defaultBlocked[0])
    );

    loadDomainFile(
        ALLOW_FILE, allowedDomains, defaultAllowed,
        sizeof(defaultAllowed) / sizeof(defaultAllowed[0])
    );
}


// FFat free/used space is slow to compute (it walks the FAT), so it is
// refreshed in the background instead of on every dashboard poll.
static bool hageziBusy() {
    return hageziState == HAGEZI_DOWNLOADING || hageziState == HAGEZI_INDEXING;
}

static void refreshFsStats() {

    if (!filesystemReady || hageziBusy()) return;

    uint32_t now = millis();

    if (!fsStatsStale && now - fsStatsAt < 60000) return;

    if (takeFilesystemLock(5)) {

        fsTotalCached = (uint32_t)FFat.totalBytes();
        fsUsedCached = (uint32_t)FFat.usedBytes();

        releaseFilesystemLock();

        fsStatsAt = now;
        fsStatsStale = false;
    }
}


// ============================================================
// 10. HAGEZI ADBLOCK RULE PARSER
// ============================================================

// Supported syntax:
//   ||example.com^           ||example.com^$third-party
//   @@||example.com^         @@||example.com^$document
// Cosmetic, regex, URL-path and other browser-only rules are ignored.

bool parseHaGeZiRule(
    char *line,
    char *domainOutput,
    size_t &domainLength,
    bool &exception
) {

    exception = false;

    while (*line && isspace((unsigned char)*line)) line++;

    if (*line == '\0' || *line == '!' || *line == '#' || *line == '[') {
        return false;
    }

    if (line[0] == '@' && line[1] == '@') {
        exception = true;
        line += 2;
    }

    if (line[0] != '|' || line[1] != '|') return false;

    line += 2;

    char *delimiter = strchr(line, '^');

    if (!delimiter) return false;

    *delimiter = '\0';

    size_t length = strlen(line);

    if (length == 0 || length > 253) return false;

    for (size_t i = 0; i < length; i++) {
        domainOutput[i] = (char)tolower((unsigned char)line[i]);
    }

    domainOutput[length] = '\0';
    domainLength = length;

    return validDomainCString(domainOutput);
}


// ============================================================
// 11. HAGEZI FILE READING (buffered)
// ============================================================

// Reads the file in 4 KB blocks instead of one byte per call.
// Uses the shared fileReadBuffer: caller must hold the FS lock.
class LineReader {
public:
    explicit LineReader(File &f) : file(f) {}

    bool next(char *out, size_t capacity, bool &overflow) {

        size_t length = 0;
        bool any = false;

        overflow = false;

        for (;;) {

            if (pos >= fill) {

                if (!eof) {
                    size_t n = file.read(fileReadBuffer, sizeof(fileReadBuffer));
                    if (n == 0) {
                        eof = true;
                    } else {
                        fill = n;
                        pos = 0;
                    }
                }

                if (pos >= fill) break;
            }

            while (pos < fill) {

                char c = (char)fileReadBuffer[pos++];
                any = true;

                if (c == '\n') {
                    out[length] = '\0';
                    return true;
                }

                if (c == '\r') continue;

                if (length + 1 < capacity) {
                    out[length++] = c;
                } else {
                    overflow = true;
                }
            }
        }

        out[length] = '\0';
        return any;
    }

private:
    File &file;
    size_t pos = 0;
    size_t fill = 0;
    bool eof = false;
};


bool countHaGeZiRules(
    const char *path,
    uint32_t &blockCount,
    uint32_t &exceptionCount
) {

    blockCount = 0;
    exceptionCount = 0;

    if (!filesystemReady || !takeFilesystemLock(5000)) return false;

    File file = FFat.open(path, "r");

    if (!file) {
        releaseFilesystemLock();
        return false;
    }

    LineReader reader(file);

    char line[512];
    char domain[254];

    bool overflow = false;
    bool tooBig = false;

    while (reader.next(line, sizeof(line), overflow)) {

        if (overflow) continue;

        bool exception = false;
        size_t length = 0;

        if (!parseHaGeZiRule(line, domain, length, exception)) continue;

        if (exception) exceptionCount++;
        else blockCount++;

        if (blockCount > MAX_HAGEZI_DOMAINS ||
            exceptionCount > HAGEZI_EXCEPTION_SLOTS / 2) {
            tooBig = true;
            break;
        }
    }

    file.close();
    releaseFilesystemLock();

    return !tooBig && blockCount >= 100000;
}


void recoverHaGeZiFiles() {

    if (!filesystemReady || !takeFilesystemLock()) return;

    // Power was lost between renaming the old and new files:
    // restore the previous known-good file.
    if (!FFat.exists(HAGEZI_FILE) && FFat.exists(HAGEZI_BACKUP)) {
        FFat.rename(HAGEZI_BACKUP, HAGEZI_FILE);
    }

    if (FFat.exists(HAGEZI_BACKUP)) FFat.remove(HAGEZI_BACKUP);
    if (FFat.exists(HAGEZI_TEMP)) FFat.remove(HAGEZI_TEMP);

    releaseFilesystemLock();
}


// ============================================================
// 12. HAGEZI STATUS
// ============================================================

void setHaGeZiStatus(uint8_t state, uint32_t count, const char *message) {

    portENTER_CRITICAL(&hageziStatusMux);

    hageziState = state;
    hageziUpdateCount = count;

    strncpy(hageziMessage, message, sizeof(hageziMessage) - 1);
    hageziMessage[sizeof(hageziMessage) - 1] = '\0';

    portEXIT_CRITICAL(&hageziStatusMux);
}


void getHaGeZiStatus(
    uint8_t &state,
    uint32_t &count,
    char *message,
    size_t messageCapacity
) {

    portENTER_CRITICAL(&hageziStatusMux);

    state = hageziState;
    count = hageziUpdateCount;

    strncpy(message, hageziMessage, messageCapacity - 1);
    message[messageCapacity - 1] = '\0';

    portEXIT_CRITICAL(&hageziStatusMux);
}


// ============================================================
// 13. BUILD / REBUILD HAGEZI INDEX IN PSRAM
// ============================================================

static bool allocateHaGeZiIndexes() {

    if (hageziBlockIndex && hageziExceptionIndex) return true;

    if (!psramFound() || ESP.getPsramSize() < 5 * 1024 * 1024) return false;

    if (!hageziBlockIndex) {
        hageziBlockIndex = (uint64_t *)heap_caps_calloc(
            HAGEZI_HASH_SLOTS, sizeof(uint64_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
    }

    if (!hageziExceptionIndex) {
        hageziExceptionIndex = (uint64_t *)heap_caps_calloc(
            HAGEZI_EXCEPTION_SLOTS, sizeof(uint64_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
    }

    return hageziBlockIndex && hageziExceptionIndex;
}


static void clearHaGeZiIndexes() {

    if (hageziBlockIndex) {
        memset(hageziBlockIndex, 0, HAGEZI_HASH_SLOTS * sizeof(uint64_t));
    }

    if (hageziExceptionIndex) {
        memset(hageziExceptionIndex, 0, HAGEZI_EXCEPTION_SLOTS * sizeof(uint64_t));
    }

    hageziLoadedDomains = 0;
    hageziLoadedExceptions = 0;
}


// Builds (or hot-reloads) the index from the installed list.
// The tables are allocated once and refilled in place: lookups that run
// while this is happening can only miss (never false-positive), so DNS
// keeps working throughout.
bool rebuildHaGeZiIndex() {

    if (!filesystemReady) {
        setHaGeZiStatus(HAGEZI_FAILED, 0, "FFat is unavailable.");
        return false;
    }

    if (!allocateHaGeZiIndexes()) {
        setHaGeZiStatus(HAGEZI_FAILED, 0, "Could not allocate PSRAM indexes.");
        return false;
    }

    setHaGeZiStatus(HAGEZI_INDEXING, 0, "Indexing blocklist...");

    if (!takeFilesystemLock(5000)) {
        setHaGeZiStatus(HAGEZI_FAILED, 0, "Could not access FFat.");
        return false;
    }

    if (!FFat.exists(HAGEZI_FILE)) {
        releaseFilesystemLock();
        clearHaGeZiIndexes();
        setHaGeZiStatus(HAGEZI_IDLE, 0, "HaGeZi has not been downloaded.");
        return false;
    }

    File file = FFat.open(HAGEZI_FILE, "r");

    if (!file) {
        releaseFilesystemLock();
        setHaGeZiStatus(HAGEZI_FAILED, 0, "Could not read HaGeZi file.");
        return false;
    }

    clearHaGeZiIndexes();

    LineReader reader(file);

    uint32_t loadedBlocks = 0;
    uint32_t loadedExceptions = 0;
    uint32_t lines = 0;

    char line[512];
    char domain[254];

    bool overflow = false;
    bool failed = false;

    while (reader.next(line, sizeof(line), overflow)) {

        // Be polite to the web server and DNS task.
        if ((++lines & 0x3FFF) == 0) vTaskDelay(1);

        if (overflow) continue;

        bool exception = false;
        size_t length = 0;

        if (!parseHaGeZiRule(line, domain, length, exception)) continue;

        uint64_t hash = hashReverse(domain, length);
        bool inserted = false;

        if (exception) {

            if (!insertHash(hageziExceptionIndex, HAGEZI_EXCEPTION_SLOTS, hash, inserted)) {
                failed = true;
                break;
            }

            if (inserted) loadedExceptions++;

        } else {

            if (!insertHash(hageziBlockIndex, HAGEZI_HASH_SLOTS, hash, inserted)) {
                failed = true;
                break;
            }

            if (inserted) loadedBlocks++;
        }

        if (loadedBlocks > MAX_HAGEZI_DOMAINS ||
            loadedExceptions > HAGEZI_EXCEPTION_SLOTS / 2) {
            failed = true;
            break;
        }
    }

    file.close();
    releaseFilesystemLock();

    if (failed || loadedBlocks < 100000) {

        clearHaGeZiIndexes();
        setHaGeZiStatus(HAGEZI_FAILED, 0, "Index validation failed.");

        return false;
    }

    hageziLoadedDomains = loadedBlocks;
    hageziLoadedExceptions = loadedExceptions;

    Serial.printf("HaGeZi index ready: %lu blocks, %lu exceptions\n",
                  (unsigned long)loadedBlocks, (unsigned long)loadedExceptions);

    setHaGeZiStatus(HAGEZI_SUCCESS, loadedBlocks, "HaGeZi Pro list is active.");

    return true;
}


// ============================================================
// 14. COMBINED DOMAIN DECISION
// ============================================================

// Rule precedence:
//   1. Manual allowlist   2. Manual blocklist
//   3. HaGeZi exceptions  4. HaGeZi blocklist

static bool isBlockedHost(const char *host, size_t len) {

    if (len == 0 || domainMutex == nullptr) return false;

    bool manuallyAllowed = false;
    bool manuallyBlocked = false;

    // Fail open on lock timeout rather than stalling DNS.
    if (xSemaphoreTake(domainMutex, pdMS_TO_TICKS(20)) == pdTRUE) {

        manuallyAllowed = lookupSorted(host, len, manualAllowHashes);

        if (!manuallyAllowed) {
            manuallyBlocked = lookupSorted(host, len, manualBlockHashes);
        }

        xSemaphoreGive(domainMutex);
    }

    if (manuallyAllowed) return false;
    if (manuallyBlocked) return true;

    if (hageziLoadedExceptions > 0 &&
        lookupIndexed(host, len, hageziExceptionIndex, HAGEZI_EXCEPTION_SLOTS)) {
        return false;
    }

    return lookupIndexed(host, len, hageziBlockIndex, HAGEZI_HASH_SLOTS);
}


// ============================================================
// 15. HAGEZI DOWNLOAD
// ============================================================

static char downloadError[160];

// Returns nullptr on success, otherwise an error message.
static const char *downloadHaGeZi() {

    if (WiFi.status() != WL_CONNECTED) return "Connect ESP32 to Wi-Fi first.";
    if (!filesystemReady) return "FFat is unavailable.";
    if (!takeFilesystemLock()) return "Filesystem is busy.";

    // Keep enough room to stage the new file next to the active one.
    if (FFat.freeBytes() < 5 * 1024 * 1024) {
        releaseFilesystemLock();
        return "Less than 5 MB free. Cannot safely stage update.";
    }

    FFat.remove(HAGEZI_TEMP);
    releaseFilesystemLock();

    // NOTE: setInsecure() encrypts but does NOT verify the certificate.
    // Attach a CA bundle for a production deployment.
    WiFiClientSecure tlsClient;
    tlsClient.setInsecure();

    HTTPClient http;

    http.setConnectTimeout(15000);
    http.setTimeout(30000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setAcceptEncoding("identity");

    if (!http.begin(tlsClient, HAGEZI_URL)) return "Could not initialize HTTPS.";

    http.setUserAgent("ESP32-Pi/" FW_VERSION);

    int status = http.GET();

    if (status != HTTP_CODE_OK) {
        snprintf(downloadError, sizeof(downloadError), "HTTP request failed: %d", status);
        http.end();
        return downloadError;
    }

    int expectedLength = http.getSize();

    if (expectedLength > (int)HAGEZI_MAX_DOWNLOAD) {
        http.end();
        return "Source exceeds maximum download size.";
    }

    if (!takeFilesystemLock()) {
        http.end();
        return "Filesystem became busy.";
    }

    File output = FFat.open(HAGEZI_TEMP, "w");

    if (!output) {
        releaseFilesystemLock();
        http.end();
        return "Cannot create temporary list.";
    }

    int bytesWritten = http.writeToStream(&output);

    output.flush();
    output.close();
    releaseFilesystemLock();

    http.end();
    tlsClient.stop();

    if (bytesWritten < 0 ||
        bytesWritten < (int)HAGEZI_MIN_DOWNLOAD ||
        bytesWritten > (int)HAGEZI_MAX_DOWNLOAD ||
        (expectedLength > 0 && bytesWritten != expectedLength)) {

        if (takeFilesystemLock()) {
            FFat.remove(HAGEZI_TEMP);
            releaseFilesystemLock();
        }

        snprintf(downloadError, sizeof(downloadError),
                 "Download failed or incomplete: %d bytes.", bytesWritten);

        return downloadError;
    }

    setHaGeZiStatus(HAGEZI_DOWNLOADING, 0, "Validating downloaded list...");

    uint32_t blockCount = 0;
    uint32_t exceptionCount = 0;

    if (!countHaGeZiRules(HAGEZI_TEMP, blockCount, exceptionCount)) {

        if (takeFilesystemLock()) {
            FFat.remove(HAGEZI_TEMP);
            releaseFilesystemLock();
        }

        return "List validation failed. Previous version preserved.";
    }

    // old -> backup, temp -> active, remove backup
    if (!takeFilesystemLock()) return "Filesystem busy during installation.";

    FFat.remove(HAGEZI_BACKUP);

    bool oldExists = FFat.exists(HAGEZI_FILE);

    if (oldExists && !FFat.rename(HAGEZI_FILE, HAGEZI_BACKUP)) {
        FFat.remove(HAGEZI_TEMP);
        releaseFilesystemLock();
        return "Could not preserve current list.";
    }

    if (!FFat.rename(HAGEZI_TEMP, HAGEZI_FILE)) {
        if (oldExists) FFat.rename(HAGEZI_BACKUP, HAGEZI_FILE);
        releaseFilesystemLock();
        return "Could not install new list.";
    }

    if (FFat.exists(HAGEZI_BACKUP)) FFat.remove(HAGEZI_BACKUP);

    releaseFilesystemLock();

    fsStatsStale = true;

    return nullptr;
}


void hageziDownloadWorker(void *parameter) {

    setHaGeZiStatus(HAGEZI_DOWNLOADING, 0, "Downloading HaGeZi Pro...");

    const char *error = downloadHaGeZi();

    if (error) {
        setHaGeZiStatus(HAGEZI_FAILED, 0, error);
    } else {
        // Hot-reload: no restart required.
        rebuildHaGeZiIndex();
    }

    vTaskDelete(nullptr);
}


// Boot-time index load runs in the background so the hotspot and
// dashboard are available within a second of power-up.
void hageziBootWorker(void *parameter) {

    recoverHaGeZiFiles();
    rebuildHaGeZiIndex();

    vTaskDelete(nullptr);
}


// ============================================================
// 16. UPSTREAM DNS SELECTION
// ============================================================

// Resolves the two upstream addresses for the active mode.
// Safe to call from any task.
static void applyUpstream() {

    uint8_t mode = upstreamMode;
    uint32_t a, b;

    if (mode == UP_ROUTER) {

        uint32_t self1 = (uint32_t)WiFi.localIP();
        uint32_t self2 = (uint32_t)WiFi.softAPIP();
        uint32_t d0 = (uint32_t)WiFi.dnsIP(0);
        uint32_t d1 = (uint32_t)WiFi.dnsIP(1);

        // Never forward to ourselves (the router may hand out our own
        // address as its DNS server when used for whole-home filtering).
        auto usable = [&](uint32_t d) {
            return d != 0 && d != self1 && d != self2;
        };

        a = usable(d0) ? d0 : PROVIDERS[UP_CLOUDFLARE].a;

        uint32_t fallback = (a != PROVIDERS[UP_CLOUDFLARE].a)
            ? PROVIDERS[UP_CLOUDFLARE].a
            : PROVIDERS[UP_CLOUDFLARE].b;

        b = (usable(d1) && d1 != a) ? d1 : fallback;

    } else {

        a = PROVIDERS[mode].a;
        b = PROVIDERS[mode].b;
    }

    upstreamAddr[0] = a;
    upstreamAddr[1] = b;
}


static int providerIndex(const String &code) {

    for (int i = 0; i < 4; i++) {
        if (code == PROVIDERS[i].code) return i;
    }

    return -1;
}


static void setUpstreamMode(uint8_t mode, bool persist) {

    if (mode > UP_ROUTER) mode = UP_CLOUDFLARE;

    upstreamMode = mode;
    applyUpstream();

    if (persist) preferences.putString("upstream", PROVIDERS[mode].code);
}


static void loadSettings() {

    int index = providerIndex(preferences.getString("upstream", "cloudflare"));

    setUpstreamMode(index < 0 ? UP_CLOUDFLARE : (uint8_t)index, false);

    blockAAAA = preferences.getBool("noaaaa", true);
}


static String upstreamLabel() {

    uint32_t a = upstreamAddr[0];

    String label = PROVIDERS[upstreamMode].name;
    label += " (";
    label += IPAddress(a).toString();
    label += ")";

    return label;
}


// ============================================================
// 17. WIFI CREDENTIAL STORAGE
// ============================================================

String ssidKey(int slot) { return "s" + String(slot); }
String passwordKey(int slot) { return "p" + String(slot); }

String savedSSID(int slot) {
    return preferences.getString(ssidKey(slot).c_str(), "");
}

String savedPassword(int slot) {
    return preferences.getString(passwordKey(slot).c_str(), "");
}


void saveNetwork(const String &ssid, const String &password) {

    int selectedSlot = -1;

    for (int i = 0; i < MAX_SAVED_NETWORKS; i++) {

        String existing = savedSSID(i);

        if (existing == ssid) {
            selectedSlot = i;
            break;
        }

        if (existing.length() == 0 && selectedSlot == -1) {
            selectedSlot = i;
        }
    }

    if (selectedSlot == -1) selectedSlot = MAX_SAVED_NETWORKS - 1;

    preferences.putString(ssidKey(selectedSlot).c_str(), ssid);
    preferences.putString(passwordKey(selectedSlot).c_str(), password);
}


int lastSavedSlot() {

    for (int i = MAX_SAVED_NETWORKS - 1; i >= 0; i--) {
        if (savedSSID(i).length() > 0) return i;
    }

    return -1;
}


// ============================================================
// 18. WIFI CONNECTION STATE MACHINE
// ============================================================

const char *connectStateName() {

    switch (connectState) {
        case CONN_CONNECTING: return "connecting";
        case CONN_OK: return "ok";
        case CONN_FAILED: return "failed";
        default: return "idle";
    }
}


void serviceManualConnect() {

    if (connectState != CONN_CONNECTING) return;

    uint32_t elapsed = millis() - connectStarted;

    if (elapsed > 500 &&
        WiFi.status() == WL_CONNECTED &&
        WiFi.SSID() == pendingSSID) {

        saveNetwork(pendingSSID, pendingPassword);

        connectState = CONN_OK;
        connectMessage = WiFi.localIP().toString();

        pendingPassword = "";
        previousPassword = "";

        Serial.printf("Connected to %s (%s)\n",
                      pendingSSID.c_str(), connectMessage.c_str());

        return;
    }

    if (elapsed > MANUAL_CONNECT_TIMEOUT_MS) {

        connectState = CONN_FAILED;
        connectMessage = "Connection failed. Check password or range.";

        Serial.printf("Connection to %s failed.\n", pendingSSID.c_str());

        pendingPassword = "";

        WiFi.disconnect(false, false);

        if (previousSSID.length() > 0) {

            WiFi.begin(previousSSID.c_str(), previousPassword.c_str());

            autoActive = true;
            autoStarted = millis();

            connectMessage += " Restoring previous network.";
        }

        previousPassword = "";
    }
}


void serviceAutoReconnect() {

    if (connectState == CONN_CONNECTING) return;

    if (WiFi.status() == WL_CONNECTED) {
        autoActive = false;
        return;
    }

    uint32_t now = millis();

    if (autoActive) {
        if (now - autoStarted < AUTO_ATTEMPT_MS) return;
        autoActive = false;
    }

    if ((int32_t)(now - nextAutoAttempt) < 0) return;

    int last = lastSavedSlot();

    if (last < 0) {
        nextAutoAttempt = now + 60000;
        return;
    }

    for (int checked = 0; checked < MAX_SAVED_NETWORKS; checked++) {

        int slot = autoCursor;

        autoCursor = (autoCursor + 1) % MAX_SAVED_NETWORKS;

        String ssid = savedSSID(slot);

        if (ssid.length() == 0) continue;

        Serial.printf("Trying saved Wi-Fi network: %s\n", ssid.c_str());

        WiFi.disconnect(false, false);
        WiFi.begin(ssid.c_str(), savedPassword(slot).c_str());

        autoActive = true;
        autoStarted = now;

        nextAutoAttempt = (slot == last)
            ? now + AUTO_ATTEMPT_MS + AUTO_CYCLE_PAUSE_MS
            : now;

        return;
    }
}


// ============================================================
// 19. DNS: WIRE-FORMAT HELPERS
// ============================================================

static inline uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}


// Returns the offset just past a (possibly compressed) name, or 0.
static size_t skipName(const uint8_t *p, size_t len, size_t pos) {

    while (pos < len) {

        uint8_t l = p[pos];

        if (l == 0) return pos + 1;

        if ((l & 0xC0) == 0xC0) return (pos + 2 <= len) ? pos + 2 : 0;

        if (l & 0xC0) return 0;

        pos += 1 + (size_t)l;
    }

    return 0;
}


// Walks every resource record (except EDNS OPT).
//   rewrite == false : only computes the minimum TTL
//   rewrite == true  : subtracts `decrement` from each TTL, or sets all
//                      TTLs to `fixedTtl` when it is non-zero
static bool processTtls(
    uint8_t *p,
    size_t len,
    bool rewrite,
    uint32_t decrement,
    uint32_t fixedTtl,
    uint32_t &minTtl
) {

    minTtl = 0xFFFFFFFFu;

    if (len < 12) return false;

    uint16_t questions = be16(p + 4);
    uint32_t records = (uint32_t)be16(p + 6) + be16(p + 8) + be16(p + 10);

    size_t pos = 12;

    for (uint16_t i = 0; i < questions; i++) {

        pos = skipName(p, len, pos);

        if (!pos) return false;

        pos += 4;

        if (pos > len) return false;
    }

    for (uint32_t i = 0; i < records; i++) {

        pos = skipName(p, len, pos);

        if (!pos || pos + 10 > len) return false;

        uint16_t type = be16(p + pos);
        uint16_t rdlen = be16(p + pos + 8);

        if (pos + 10 + rdlen > len) return false;

        if (type != 41) {

            uint8_t *ttlField = p + pos + 4;
            uint32_t ttl = be32(ttlField);

            if (ttl < minTtl) minTtl = ttl;

            if (rewrite) {
                put32(ttlField,
                      fixedTtl ? fixedTtl : (ttl > decrement ? ttl - decrement : 1));
            }
        }

        pos += 10 + rdlen;
    }

    return true;
}


// Decodes the single question: lower-cased host, type, class.
static bool parseQuestion(
    const uint8_t *p,
    size_t n,
    char *host,           // at least 254 bytes
    size_t &hostLen,
    uint16_t &qtype,
    uint16_t &qclass,
    size_t &qEnd
) {

    size_t pos = 12;

    hostLen = 0;

    for (;;) {

        if (pos >= n) return false;

        uint8_t l = p[pos++];

        if (l == 0) break;
        if (l & 0xC0) return false;
        if (pos + l > n) return false;
        if (hostLen + l + 1 > 253) return false;

        if (hostLen > 0) host[hostLen++] = '.';

        for (uint8_t i = 0; i < l; i++) {
            host[hostLen++] = (char)tolower((int)p[pos++]);
        }
    }

    host[hostLen] = '\0';

    if (pos + 4 > n) return false;

    qtype = be16(p + pos);
    qclass = be16(p + pos + 2);
    qEnd = pos + 4;

    return true;
}


static uint64_t makeCacheKey(
    const char *host,
    size_t len,
    uint16_t qtype,
    uint16_t qclass
) {

    uint64_t h = FNV_OFFSET;

    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)host[i];
        h *= FNV_PRIME;
    }

    h ^= ((uint64_t)qtype << 16) | qclass;
    h *= FNV_PRIME;

    return fixHash(h);
}


// ============================================================
// 20. DNS: CACHE
// ============================================================

struct CacheEntry {
    uint64_t key;
    uint32_t storedMs;
    uint32_t ttlSec;
    uint16_t len;
    uint16_t qEnd;
    uint8_t data[CACHE_MAX_RESPONSE];
};

static CacheEntry *dnsCache = nullptr;
static uint32_t cacheUsedEntries = 0;
static uint8_t cacheOut[CACHE_MAX_RESPONSE];   // DNS task only


static void initializeDnsCache() {

    if (!psramFound()) {
        Serial.println("No PSRAM: DNS cache disabled.");
        return;
    }

    dnsCache = (CacheEntry *)heap_caps_calloc(
        CACHE_ENTRIES, sizeof(CacheEntry),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    Serial.printf("DNS cache: %s (%u KB)\n",
                  dnsCache ? "enabled" : "allocation failed",
                  (unsigned)(CACHE_ENTRIES * sizeof(CacheEntry) / 1024));
}


static inline uint32_t cacheBase(uint64_t key) {
    return (((uint32_t)(key >> 32) ^ (uint32_t)key) & (CACHE_SETS - 1)) * CACHE_WAYS;
}


static int cacheFind(uint64_t key) {

    if (!dnsCache) return -1;

    uint32_t base = cacheBase(key);

    for (uint32_t w = 0; w < CACHE_WAYS; w++) {
        if (dnsCache[base + w].key == key) return (int)(base + w);
    }

    return -1;
}


static void cacheStore(
    uint64_t key,
    uint16_t qEnd,
    const uint8_t *p,
    size_t n,
    uint32_t ttl
) {

    if (!dnsCache) return;

    uint32_t now = millis();
    int slot = cacheFind(key);

    if (slot < 0) {

        uint32_t base = cacheBase(key);
        uint32_t oldestAge = 0;

        for (uint32_t w = 0; w < CACHE_WAYS; w++) {
            if (dnsCache[base + w].key == 0) {
                slot = (int)(base + w);
                cacheUsedEntries++;
                break;
            }
        }

        if (slot < 0) {
            for (uint32_t w = 0; w < CACHE_WAYS; w++) {
                uint32_t age = now - dnsCache[base + w].storedMs;
                if (slot < 0 || age > oldestAge) {
                    slot = (int)(base + w);
                    oldestAge = age;
                }
            }
        }
    }

    CacheEntry &e = dnsCache[slot];

    e.key = key;
    e.storedMs = now;
    e.ttlSec = ttl;
    e.len = (uint16_t)n;
    e.qEnd = qEnd;

    memcpy(e.data, p, n);
}


static void cacheClear() {

    if (!dnsCache) return;

    for (uint32_t i = 0; i < CACHE_ENTRIES; i++) dnsCache[i].key = 0;

    cacheUsedEntries = 0;
}


static void maybeCache(uint64_t key, size_t qEnd, const uint8_t *p, size_t n) {

    if (!dnsCache || n < 12 || n > CACHE_MAX_RESPONSE) return;

    if (p[2] & 0x02) return;                       // truncated

    uint8_t rcode = p[3] & 0x0F;

    if (rcode != 0 && rcode != 3) return;          // NOERROR / NXDOMAIN only

    uint32_t minTtl;

    if (!processTtls((uint8_t *)p, n, false, 0, 0, minTtl)) return;

    bool negative = (rcode == 3) || be16(p + 6) == 0;

    uint32_t ttl = (minTtl == 0xFFFFFFFFu) ? CACHE_NEG_DEFAULT_TTL : minTtl;

    ttl = negative
        ? std::min(std::max(ttl, CACHE_MIN_TTL), CACHE_NEG_MAX_TTL)
        : std::min(std::max(ttl, CACHE_MIN_TTL), CACHE_MAX_TTL);

    cacheStore(key, (uint16_t)qEnd, p, n, ttl);
}


// ============================================================
// 21. DNS: SOCKETS AND LOCAL RESPONSES
// ============================================================

static int clientSock = -1;
static int upstreamSock = -1;

static void sendUdp(
    int sock,
    uint32_t addr,
    uint16_t port,          // host byte order
    const uint8_t *data,
    size_t len
) {

    struct sockaddr_in to;

    memset(&to, 0, sizeof(to));

    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = addr;

    sendto(sock, data, len, 0, (struct sockaddr *)&to, sizeof(to));
}


static int openUdpSocket(uint16_t port) {

    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (s < 0) return -1;

    struct sockaddr_in a;

    memset(&a, 0, sizeof(a));

    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);

    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(s);
        return -1;
    }

    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    return s;
}


#define LOCAL_NULL 0       // 0.0.0.0 / :: answer for A / AAAA
#define LOCAL_NODATA 1     // NOERROR, no records
#define LOCAL_SERVFAIL 2

static void sendLocal(
    const uint8_t *q,
    size_t qEnd,
    uint16_t id,
    uint16_t qtype,
    uint32_t addr,
    uint16_t port,
    uint8_t mode
) {

    if (qEnd < 17 || qEnd > DNS_MAX_QUERY) return;

    uint8_t r[DNS_MAX_QUERY + 32];

    memcpy(r, q, qEnd);

    put16(r, id);

    r[2] = 0x80 | (q[2] & 0x79);                       // QR, keep opcode + RD
    r[3] = 0x80 | (q[3] & 0x10) |                      // RA, keep CD
           (mode == LOCAL_SERVFAIL ? 2 : 0);

    r[4] = 0; r[5] = 1;                                // one question
    memset(r + 6, 0, 6);                               // no other records

    size_t len = qEnd;

    if (mode == LOCAL_NULL && (qtype == 1 || qtype == 28)) {

        size_t rdlen = (qtype == 1) ? 4 : 16;

        r[7] = 1;                                      // one answer

        r[len++] = 0xC0;                               // name pointer to question
        r[len++] = 0x0C;

        put16(r + len, qtype); len += 2;
        put16(r + len, 1); len += 2;                   // class IN
        put32(r + len, BLOCK_TTL); len += 4;
        put16(r + len, (uint16_t)rdlen); len += 2;

        memset(r + len, 0, rdlen);
        len += rdlen;
    }

    sendUdp(clientSock, addr, port, r, len);
}


// ============================================================
// 22. DNS: PENDING QUERIES (many in flight at once)
// ============================================================

struct Pending {
    bool used;
    bool hasClient;          // false = background cache refresh
    uint8_t attempts;
    uint16_t clientId;
    uint16_t upstreamId;
    uint16_t clientPort;
    uint16_t qLen;
    uint16_t qEnd;
    uint32_t clientAddr;
    uint32_t started;
    uint32_t nextSend;
    uint64_t key;
    uint8_t query[DNS_MAX_QUERY];
};

static Pending pendingTable[DNS_MAX_PENDING];


// which: 0 = primary, 1 = secondary, 2 = both
static void sendToUpstream(int slot, int which) {

    Pending &s = pendingTable[slot];

    uint32_t a = upstreamAddr[0];
    uint32_t b = upstreamAddr[1];

    if (which == 0 || which == 2 || b == 0) {
        if (a) sendUdp(upstreamSock, a, 53, s.query, s.qLen);
    }

    if ((which == 1 || which == 2) && b) {
        sendUdp(upstreamSock, b, 53, s.query, s.qLen);
    }
}


static bool startQuery(
    const uint8_t *q,
    size_t n,
    size_t qEnd,
    uint64_t key,
    uint32_t addr,
    uint16_t port,
    bool hasClient
) {

    if (!hasClient) {
        for (int i = 0; i < DNS_MAX_PENDING; i++) {
            if (pendingTable[i].used && !pendingTable[i].hasClient &&
                pendingTable[i].key == key) {
                return true;                           // refresh already running
            }
        }
    }

    int slot = -1;

    for (int i = 0; i < DNS_MAX_PENDING; i++) {
        if (!pendingTable[i].used) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return false;

    // Unpredictable, unique upstream transaction ID.
    uint16_t id;
    bool clash;

    do {

        id = (uint16_t)esp_random();
        clash = false;

        for (int i = 0; i < DNS_MAX_PENDING; i++) {
            if (pendingTable[i].used && pendingTable[i].upstreamId == id) {
                clash = true;
                break;
            }
        }

    } while (clash);

    Pending &s = pendingTable[slot];

    s.hasClient = hasClient;
    s.attempts = 1;
    s.clientId = be16(q);
    s.upstreamId = id;
    s.clientAddr = addr;
    s.clientPort = port;
    s.qLen = (uint16_t)n;
    s.qEnd = (uint16_t)qEnd;
    s.key = key;
    s.started = millis();
    s.nextSend = s.started + DNS_RETRY_SECOND_MS;

    memcpy(s.query, q, n);
    put16(s.query, id);

    s.used = true;

    sendToUpstream(slot, 0);

    return true;
}


// ============================================================
// 23. DNS: CLIENT QUERY HANDLING
// ============================================================

// Returns true if answered from the cache. `stale` is set when the
// answer was past its TTL (a background refresh should be started).
static bool tryCache(
    const uint8_t *q,
    size_t qEnd,
    uint64_t key,
    uint32_t addr,
    uint16_t port,
    bool &stale
) {

    stale = false;

    int idx = cacheFind(key);

    if (idx < 0) return false;

    CacheEntry &e = dnsCache[idx];

    if (e.qEnd != qEnd) return false;

    // Verify the question (name case-insensitively, type/class exactly).
    for (size_t i = 12; i < qEnd - 4; i++) {
        if (tolower((int)e.data[i]) != tolower((int)q[i])) return false;
    }

    if (memcmp(e.data + qEnd - 4, q + qEnd - 4, 4) != 0) return false;

    uint32_t ageSec = (millis() - e.storedMs) / 1000;

    if (ageSec >= e.ttlSec) {

        if (ageSec >= e.ttlSec + CACHE_STALE_WINDOW) return false;

        stale = true;
    }

    memcpy(cacheOut, e.data, e.len);

    put16(cacheOut, be16(q));                                  // client's ID
    memcpy(cacheOut + 12, q + 12, qEnd - 12);                  // client's 0x20 casing
    cacheOut[2] = (cacheOut[2] & 0xFE) | (q[2] & 0x01);        // client's RD bit

    uint32_t unused;

    processTtls(
        cacheOut, e.len, true,
        stale ? 0 : ageSec,
        stale ? CACHE_STALE_TTL : 0,
        unused
    );

    sendUdp(clientSock, addr, port, cacheOut, e.len);

    return true;
}


static void handleClientPacket(
    const uint8_t *q,
    size_t n,
    uint32_t addr,
    uint16_t port
) {

    if (n < 17 || n > DNS_MAX_QUERY) return;
    if (q[2] & 0x80) return;                      // not a query
    if (((q[2] >> 3) & 0x0F) != 0) return;        // only standard queries
    if (be16(q + 4) != 1) return;                 // exactly one question

    char host[254];
    size_t hostLen = 0;
    uint16_t qtype = 0, qclass = 0;
    size_t qEnd = 0;

    if (!parseQuestion(q, n, host, hostLen, qtype, qclass, qEnd)) return;

    uint16_t clientId = be16(q);
    uint64_t key = makeCacheKey(host, hostLen, qtype, qclass);

    // The client retried while we are still waiting for upstream.
    for (int i = 0; i < DNS_MAX_PENDING; i++) {
        const Pending &p = pendingTable[i];
        if (p.used && p.hasClient && p.key == key && p.clientId == clientId &&
            p.clientAddr == addr && p.clientPort == port) {
            return;
        }
    }

    totalQueries++;

#if DNS_DEBUG
    Serial.printf("DNS %s type=%u\n", host, (unsigned)qtype);
#endif

    // IPv6 lookups are useless behind this IPv4-only NAT.
    if (blockAAAA && qtype == 28) {
        aaaaSuppressed++;
        sendLocal(q, qEnd, clientId, qtype, addr, port, LOCAL_NODATA);
        return;
    }

    if (isBlockedHost(host, hostLen)) {
        blockedQueries++;
        sendLocal(q, qEnd, clientId, qtype, addr, port, LOCAL_NULL);
        return;
    }

    bool stale = false;

    if (tryCache(q, qEnd, key, addr, port, stale)) {

        cachedQueries++;

        if (stale && WiFi.status() == WL_CONNECTED) {
            startQuery(q, n, qEnd, key, 0, 0, false);
        }

        return;
    }

    if (WiFi.status() != WL_CONNECTED ||
        !startQuery(q, n, qEnd, key, addr, port, true)) {

        failedQueries++;
        sendLocal(q, qEnd, clientId, qtype, addr, port, LOCAL_SERVFAIL);
    }
}


// ============================================================
// 24. DNS: UPSTREAM RESPONSE HANDLING
// ============================================================

static void handleUpstreamPacket(
    uint8_t *p,
    size_t n,
    uint32_t src,
    uint16_t srcPort
) {

    if (n < 17 || srcPort != 53 || !(p[2] & 0x80)) return;

    if (src != upstreamAddr[0] && src != upstreamAddr[1]) return;

    uint16_t id = be16(p);

    int slot = -1;

    for (int i = 0; i < DNS_MAX_PENDING; i++) {
        if (pendingTable[i].used && pendingTable[i].upstreamId == id) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return;

    Pending &s = pendingTable[slot];

    // The answer must echo our question exactly.
    if (be16(p + 4) != 1 || n < s.qEnd ||
        memcmp(p + 12, s.query + 12, s.qEnd - 12) != 0) {
        return;
    }

    uint32_t rtt = millis() - s.started;

    latencyEwmaMs = (latencyEwmaMs == 0) ? rtt : (latencyEwmaMs * 7 + rtt) / 8;

    maybeCache(s.key, s.qEnd, p, n);

    if (s.hasClient) {

        put16(p, s.clientId);
        sendUdp(clientSock, s.clientAddr, s.clientPort, p, n);

        forwardedQueries++;
    }

    s.used = false;
}


static void servicePending(uint32_t now) {

    for (int i = 0; i < DNS_MAX_PENDING; i++) {

        Pending &s = pendingTable[i];

        if (!s.used) continue;

        if ((int32_t)(now - (s.started + DNS_TIMEOUT_MS)) >= 0) {

            if (s.hasClient) {
                failedQueries++;
                sendLocal(s.query, s.qEnd, s.clientId, 0,
                          s.clientAddr, s.clientPort, LOCAL_SERVFAIL);
            }

            s.used = false;
            continue;
        }

        if ((int32_t)(now - s.nextSend) >= 0) {

            if (s.attempts == 1) {
                sendToUpstream(i, 1);
                s.nextSend = now + (DNS_RETRY_BOTH_MS - DNS_RETRY_SECOND_MS);
            } else if (s.attempts == 2) {
                sendToUpstream(i, 2);
                s.nextSend = now + 900;
            } else {
                sendToUpstream(i, 2);
                s.nextSend = s.started + DNS_TIMEOUT_MS;
            }

            if (s.attempts < 250) s.attempts++;
        }
    }
}


// ============================================================
// 25. DNS SERVER TASK
// ============================================================

void dnsWorker(void *parameter) {

    clientSock = openUdpSocket(53);
    upstreamSock = openUdpSocket(0);       // random ephemeral source port

    if (clientSock < 0 || upstreamSock < 0) {

        Serial.println("ERROR: Cannot open DNS sockets.");

        if (clientSock >= 0) close(clientSock);
        if (upstreamSock >= 0) close(upstreamSock);

        vTaskDelete(nullptr);
        return;
    }

    dnsStarted = true;

    Serial.println("DNS server listening on UDP port 53.");

    static uint8_t rx[1536];

    int maxFd = (clientSock > upstreamSock ? clientSock : upstreamSock) + 1;

    for (;;) {

        uint32_t now = millis();

        if (cacheFlushRequested) {
            cacheFlushRequested = false;
            cacheClear();
        }

        // Sleep until a packet arrives or the next retry/timeout is due.
        int32_t waitMs = 200;

        for (int i = 0; i < DNS_MAX_PENDING; i++) {

            if (!pendingTable[i].used) continue;

            int32_t d1 = (int32_t)(pendingTable[i].nextSend - now);
            int32_t d2 = (int32_t)(pendingTable[i].started + DNS_TIMEOUT_MS - now);

            if (d1 < waitMs) waitMs = d1;
            if (d2 < waitMs) waitMs = d2;
        }

        if (waitMs < 1) waitMs = 1;

        fd_set readSet;

        FD_ZERO(&readSet);
        FD_SET(clientSock, &readSet);
        FD_SET(upstreamSock, &readSet);

        struct timeval tv;

        tv.tv_sec = waitMs / 1000;
        tv.tv_usec = (waitMs % 1000) * 1000;

        int ready = select(maxFd, &readSet, nullptr, nullptr, &tv);

        if (ready > 0) {

            struct sockaddr_in from;
            socklen_t fromLen;

            if (FD_ISSET(upstreamSock, &readSet)) {

                for (int i = 0; i < 64; i++) {

                    fromLen = sizeof(from);

                    int got = recvfrom(upstreamSock, rx, sizeof(rx), 0,
                                       (struct sockaddr *)&from, &fromLen);

                    if (got <= 0) break;

                    if ((size_t)got <= DNS_MAX_PACKET) {
                        handleUpstreamPacket(rx, (size_t)got,
                                             from.sin_addr.s_addr,
                                             ntohs(from.sin_port));
                    }
                }
            }

            if (FD_ISSET(clientSock, &readSet)) {

                for (int i = 0; i < 64; i++) {

                    fromLen = sizeof(from);

                    int got = recvfrom(clientSock, rx, sizeof(rx), 0,
                                       (struct sockaddr *)&from, &fromLen);

                    if (got <= 0) break;

                    handleClientPacket(rx, (size_t)got,
                                       from.sin_addr.s_addr,
                                       ntohs(from.sin_port));
                }
            }
        }

        servicePending(millis());
    }
}


// ============================================================
// 26. DASHBOARD AUTHENTICATION
// ============================================================

static const char *HEADER_KEYS[] = {"If-None-Match", "X-ESP32Pi"};

bool requireAuthentication() {

    if (server.authenticate(DASH_USER, DASH_PASSWORD)) return true;

    server.requestAuthentication();

    return false;
}


// State-changing requests also need a custom header. Browsers will not
// send it from a cross-site form, which blocks CSRF from other web pages
// opened on a hotspot client.
bool requireAuthenticatedPost() {

    if (!requireAuthentication()) return false;

    if (server.header("X-ESP32Pi").length() == 0) {

        server.send(
            403,
            "application/json",
            "{\"ok\":false,\"message\":\"Missing request header.\"}"
        );

        return false;
    }

    return true;
}


// ============================================================
// 27. DASHBOARD PAGE AND STATUS
// ============================================================

void handleRoot() {

    if (!requireAuthentication()) return;

    server.sendHeader("ETag", ASSET_ETAG);
    server.sendHeader("Cache-Control", "private, no-cache");

    if (server.header("If-None-Match") == ASSET_ETAG) {
        server.send(304);
        return;
    }

    server.send_P(200, "text/html", DASHBOARD_HTML, sizeof(DASHBOARD_HTML) - 1);
}


static void jsonU(String &j, const char *key, uint32_t v) {
    j += ",\""; j += key; j += "\":"; j += v;
}

static void jsonI(String &j, const char *key, int v) {
    j += ",\""; j += key; j += "\":"; j += v;
}

static void jsonB(String &j, const char *key, bool v) {
    j += ",\""; j += key; j += "\":"; j += v ? "true" : "false";
}

static void jsonS(String &j, const char *key, const String &v) {
    j += ",\""; j += key; j += "\":\""; j += jsonEscape(v); j += "\"";
}


void handleStatus() {

    if (!requireAuthentication()) return;

    bool connected = WiFi.status() == WL_CONNECTED;

    uint8_t listState;
    uint32_t updateCount;
    char updateMessage[160];

    getHaGeZiStatus(listState, updateCount, updateMessage, sizeof(updateMessage));

    String j;
    j.reserve(1200);

    j += "{\"version\":\"" FW_VERSION "\"";

    jsonB(j, "connected", connected);
    jsonS(j, "ssid", connected ? WiFi.SSID() : String());
    jsonS(j, "ip", connected ? WiFi.localIP().toString() : String("0.0.0.0"));
    jsonS(j, "ap_ssid", String(AP_SSID));
    jsonS(j, "ap_ip", WiFi.softAPIP().toString());
    jsonI(j, "rssi", connected ? WiFi.RSSI() : 0);
    jsonU(j, "clients", (uint32_t)WiFi.softAPgetStationNum());
    jsonB(j, "napt", naptEnabled);

    jsonU(j, "total", totalQueries);
    jsonU(j, "blocked", blockedQueries);
    jsonU(j, "cached", cachedQueries);
    jsonU(j, "forwarded", forwardedQueries);
    jsonU(j, "failed", failedQueries);
    jsonU(j, "aaaa", aaaaSuppressed);
    jsonU(j, "latency", latencyEwmaMs);

    jsonU(j, "cache_used", cacheUsedEntries);
    jsonU(j, "cache_cap", dnsCache ? CACHE_ENTRIES : 0);

    jsonS(j, "upstream", upstreamLabel());
    jsonS(j, "upstream_code", String(PROVIDERS[upstreamMode].code));
    jsonB(j, "block_aaaa", blockAAAA);

    jsonS(j, "connect_state", String(connectStateName()));
    jsonS(j, "connect_message", connectMessage);

    jsonU(j, "fs_total", fsTotalCached);
    jsonU(j, "fs_used", fsUsedCached);
    jsonU(j, "psram_free", (uint32_t)ESP.getFreePsram());
    jsonU(j, "psram_total", (uint32_t)ESP.getPsramSize());
    jsonU(j, "heap_free", (uint32_t)ESP.getFreeHeap());

    jsonU(j, "hg_loaded", hageziLoadedDomains);
    jsonU(j, "hg_exc", hageziLoadedExceptions);
    jsonU(j, "hg_state", listState);
    jsonS(j, "hg_msg", String(updateMessage));

    jsonU(j, "uptime", millis() / 1000);

    j += "}";

    server.send(200, "application/json", j);
}


// ============================================================
// 28. WIFI SCAN API
// ============================================================

void handleScan() {

    if (!requireAuthentication()) return;

    if (connectState == CONN_CONNECTING || autoActive) {

        server.send(
            409,
            "application/json",
            "{\"message\":\"Wi-Fi connection in progress.\"}"
        );

        return;
    }

    // Short dwell time per channel: the radio is away from the hotspot
    // channel while scanning, so keep it as brief as possible.
    int count = WiFi.scanNetworks(false, true, false, 120);

    if (count < 0) {

        WiFi.scanDelete();

        server.send(
            500,
            "application/json",
            "{\"message\":\"Wi-Fi scan failed.\"}"
        );

        return;
    }

    String json = "{\"networks\":[";
    bool first = true;

    std::vector<String> seen;

    for (int i = 0; i < count && seen.size() < 24; i++) {

        String ssid = WiFi.SSID(i);

        if (ssid.length() == 0) continue;
        if (std::find(seen.begin(), seen.end(), ssid) != seen.end()) continue;

        seen.push_back(ssid);

        if (!first) json += ",";
        first = false;

        json += "{\"ssid\":\"";
        json += jsonEscape(ssid);
        json += "\",\"rssi\":";
        json += String(WiFi.RSSI(i));
        json += ",\"secure\":";
        json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
        json += "}";
    }

    json += "]}";

    WiFi.scanDelete();

    server.send(200, "application/json", json);
}


// ============================================================
// 29. WIFI CONNECTION API
// ============================================================

void handleConnect() {

    if (!requireAuthenticatedPost()) return;

    if (connectState == CONN_CONNECTING) {

        server.send(
            409,
            "application/json",
            "{\"ok\":false,\"message\":\"Another connection is running.\"}"
        );

        return;
    }

    String ssid = server.arg("ssid");
    String password = server.arg("password");

    ssid.trim();

    if (ssid.length() == 0 || ssid.length() > 32) {

        server.send(
            400,
            "application/json",
            "{\"ok\":false,\"message\":\"Invalid SSID.\"}"
        );

        return;
    }

    if (password.length() > 63 ||
        (password.length() > 0 && password.length() < 8)) {

        server.send(
            400,
            "application/json",
            "{\"ok\":false,\"message\":\"Wi-Fi password must be 8-63 characters, or empty for an open network.\"}"
        );

        return;
    }

    previousSSID = "";
    previousPassword = "";

    if (WiFi.status() == WL_CONNECTED) {

        previousSSID = WiFi.SSID();

        for (int i = 0; i < MAX_SAVED_NETWORKS; i++) {
            if (savedSSID(i) == previousSSID) {
                previousPassword = savedPassword(i);
                break;
            }
        }
    }

    pendingSSID = ssid;
    pendingPassword = password;

    connectMessage = "";
    connectStarted = millis();
    connectState = CONN_CONNECTING;
    autoActive = false;

    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), password.c_str());

    server.send(200, "application/json", "{\"ok\":true,\"pending\":true}");
}


// ============================================================
// 30. SAVED WIFI API
// ============================================================

void handleSavedNetworks() {

    if (!requireAuthentication()) return;

    String json = "{\"networks\":[";
    bool first = true;

    for (int i = 0; i < MAX_SAVED_NETWORKS; i++) {

        String ssid = savedSSID(i);

        if (ssid.length() == 0) continue;

        if (!first) json += ",";
        first = false;

        json += "\"";
        json += jsonEscape(ssid);
        json += "\"";
    }

    json += "]}";

    server.send(200, "application/json", json);
}


void handleForget() {

    if (!requireAuthenticatedPost()) return;

    String ssid = server.arg("ssid");

    if (ssid.length() == 0) {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }

    bool removed = false;

    for (int i = 0; i < MAX_SAVED_NETWORKS; i++) {

        if (savedSSID(i) == ssid) {
            preferences.remove(ssidKey(i).c_str());
            preferences.remove(passwordKey(i).c_str());
            removed = true;
        }
    }

    server.send(
        removed ? 200 : 404,
        "application/json",
        removed ? "{\"ok\":true}" : "{\"ok\":false,\"message\":\"Network not found.\"}"
    );
}


// ============================================================
// 31. SETTINGS API
// ============================================================

void handleUpstream() {

    if (!requireAuthenticatedPost()) return;

    int index = providerIndex(server.arg("provider"));

    if (index < 0) {

        server.send(
            400,
            "application/json",
            "{\"ok\":false,\"message\":\"Invalid DNS provider.\"}"
        );

        return;
    }

    setUpstreamMode((uint8_t)index, true);

    server.send(200, "application/json", "{\"ok\":true}");
}


void handleSettings() {

    if (!requireAuthenticatedPost()) return;

    if (server.hasArg("block_aaaa")) {

        bool enable = server.arg("block_aaaa") == "1";

        blockAAAA = enable;
        preferences.putBool("noaaaa", enable);
    }

    server.send(200, "application/json", "{\"ok\":true}");
}


void handleCacheFlush() {

    if (!requireAuthenticatedPost()) return;

    cacheFlushRequested = true;

    server.send(200, "application/json", "{\"ok\":true}");
}


void handleReboot() {

    if (!requireAuthenticatedPost()) return;

    server.send(200, "application/json", "{\"ok\":true}");

    restartAt = millis() + 600;
}


// ============================================================
// 32. MANUAL DOMAIN API
// ============================================================

String domainListJSON(const std::vector<String> &domains) {

    String json = "[";

    for (size_t i = 0; i < domains.size(); i++) {

        if (i > 0) json += ",";

        json += "\"";
        json += jsonEscape(domains[i]);
        json += "\"";
    }

    json += "]";

    return json;
}


void handleDomains() {

    if (!requireAuthentication()) return;

    if (xSemaphoreTake(domainMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {

        server.send(
            503,
            "application/json",
            "{\"message\":\"Domain list busy.\"}"
        );

        return;
    }

    String json = "{\"blocked\":";

    json += domainListJSON(blockedDomains);
    json += ",\"allowed\":";
    json += domainListJSON(allowedDomains);
    json += "}";

    xSemaphoreGive(domainMutex);

    server.send(200, "application/json", json);
}


void handleDomainModification() {

    if (!requireAuthenticatedPost()) return;

    if (hageziBusy()) {

        server.send(
            409,
            "application/json",
            "{\"ok\":false,\"message\":\"Filesystem is busy updating HaGeZi.\"}"
        );

        return;
    }

    String action = server.arg("action");
    String type = server.arg("type");
    String domain = normalizeDomain(server.arg("domain"));

    if ((type != "block" && type != "allow") || !validDomain(domain)) {

        server.send(
            400,
            "application/json",
            "{\"ok\":false,\"message\":\"Invalid domain or list type.\"}"
        );

        return;
    }

    if (action != "add" && action != "remove") {

        server.send(
            400,
            "application/json",
            "{\"ok\":false,\"message\":\"Invalid action.\"}"
        );

        return;
    }

    if (xSemaphoreTake(domainMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {

        server.send(
            503,
            "application/json",
            "{\"ok\":false,\"message\":\"Domain list busy.\"}"
        );

        return;
    }

    std::vector<String> &list = (type == "block") ? blockedDomains : allowedDomains;

    bool changed = false;
    const char *error = nullptr;

    auto it = std::find(list.begin(), list.end(), domain);

    if (action == "add") {

        if (it == list.end()) {

            if (list.size() >= MAX_MANUAL_DOMAINS) {
                error = "Manual list is full.";
            } else {
                list.push_back(domain);
                changed = true;
            }
        }

    } else if (it != list.end()) {

        list.erase(it);
        changed = true;
    }

    if (changed) rebuildManualHashes();

    std::vector<String> snapshot = list;

    xSemaphoreGive(domainMutex);

    if (error) {

        String json = "{\"ok\":false,\"message\":\"";
        json += error;
        json += "\"}";

        server.send(400, "application/json", json);

        return;
    }

    if (changed) {

        bool saved = saveDomainFile(
            type == "block" ? BLOCK_FILE : ALLOW_FILE,
            snapshot
        );

        if (!saved) {

            server.send(
                500,
                "application/json",
                "{\"ok\":false,\"message\":\"Changed in RAM but could not save to FFat.\"}"
            );

            return;
        }
    }

    server.send(200, "application/json", "{\"ok\":true}");
}


// ============================================================
// 33. HAGEZI UPDATE API
// ============================================================

void handleHaGeZiUpdate() {

    if (!requireAuthenticatedPost()) return;

    if (!filesystemReady) {

        server.send(
            503,
            "application/json",
            "{\"ok\":false,\"message\":\"FFat unavailable.\"}"
        );

        return;
    }

    if (WiFi.status() != WL_CONNECTED) {

        server.send(
            400,
            "application/json",
            "{\"ok\":false,\"message\":\"Connect to Wi-Fi first.\"}"
        );

        return;
    }

    if (hageziBusy()) {

        server.send(
            409,
            "application/json",
            "{\"ok\":false,\"message\":\"Update already running.\"}"
        );

        return;
    }

    setHaGeZiStatus(HAGEZI_DOWNLOADING, 0, "Starting HaGeZi update...");

    // Core 1 keeps the download and indexing away from the Wi-Fi core.
    BaseType_t result = xTaskCreatePinnedToCore(
        hageziDownloadWorker, "HaGeZiUpdate", 16384, nullptr, 1, nullptr, 1
    );

    if (result != pdPASS) {

        setHaGeZiStatus(HAGEZI_FAILED, 0, "Could not create update task.");

        server.send(
            500,
            "application/json",
            "{\"ok\":false,\"message\":\"Could not create update task.\"}"
        );

        return;
    }

    server.send(
        202,
        "application/json",
        "{\"ok\":true,\"message\":\"HaGeZi download started.\"}"
    );
}


// ============================================================
// 34. WEB SERVER ROUTES
// ============================================================

void initializeWebServer() {

    server.collectHeaders(HEADER_KEYS, 2);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/connect", HTTP_POST, handleConnect);
    server.on("/api/saved", HTTP_GET, handleSavedNetworks);
    server.on("/api/forget", HTTP_POST, handleForget);
    server.on("/api/upstream", HTTP_POST, handleUpstream);
    server.on("/api/settings", HTTP_POST, handleSettings);
    server.on("/api/cache/flush", HTTP_POST, handleCacheFlush);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    server.on("/api/domains", HTTP_GET, handleDomains);
    server.on("/api/domain", HTTP_POST, handleDomainModification);
    server.on("/api/blocklist/update", HTTP_POST, handleHaGeZiUpdate);

    // Browsers request this on their own; answer without a login prompt.
    server.on("/favicon.ico", HTTP_GET, []() {
        server.send(204);
    });

    server.onNotFound([]() {

        if (!requireAuthentication()) return;

        server.send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    server.begin();

    Serial.println("Dashboard started.");
}


// ============================================================
// 35. WIFI INITIALIZATION
// ============================================================

// Enable NAPT only once the STA interface has an upstream address.
void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t) {

    switch (event) {

        case ARDUINO_EVENT_WIFI_STA_GOT_IP: {

            Serial.printf("Upstream Wi-Fi connected: %s\n",
                          WiFi.localIP().toString().c_str());

            bool enabled = WiFi.AP.enableNAPT(true);
            naptEnabled = enabled;

            Serial.printf("AP internet sharing (NAPT): %s\n",
                          enabled ? "ENABLED" : "FAILED");

            // Router DNS addresses are only known now.
            applyUpstream();

            break;
        }

        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {

            if (naptEnabled) {
                WiFi.AP.enableNAPT(false);
                naptEnabled = false;
            }

            Serial.println("Upstream Wi-Fi unavailable; NAPT disabled.");

            break;
        }

        default:
            break;
    }
}


void initializeWiFi() {

    Serial.println("\nInitializing ESP32-Pi Wi-Fi...");

    WiFi.persistent(false);

    WiFi.mode(WIFI_MODE_NULL);
    delay(300);

    WiFi.setHostname("esp32-pi");

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);               // no modem power save: lowest latency
    WiFi.setAutoReconnect(false);

    delay(200);

    // Hotspot: 802.11g/n only. Dropping 802.11b removes the slow 1 Mbps
    // beacons and legacy protection frames, which frees airtime for data.
    // esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

#if WIFI_STA_HT40
    // Upstream link: allow 40 MHz channels if the router offers them.
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
#endif

    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress leaseStart(192, 168, 4, 2);

    // Hotspot clients must use this device's DNS filter.
    IPAddress apDns(192, 168, 4, 1);

    bool configOK = WiFi.softAPConfig(apIP, gateway, subnet, leaseStart, apDns);

    Serial.printf("AP configuration: %s\n", configOK ? "OK" : "FAILED");

    bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, false, 4);

    delay(300);

    Serial.printf("AP startup: %s\n", apStarted ? "SUCCESS" : "FAILED");
    Serial.printf("Requested SSID: %s\n", AP_SSID);
    Serial.printf("Actual SSID: %s\n", WiFi.softAPSSID().c_str());
    Serial.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
    Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    // Register after the AP exists so callbacks can toggle NAPT safely.
    Network.onEvent(onWiFiEvent);

    Serial.println("Wi-Fi initialization complete.\n");

    nextAutoAttempt = 0;
}


// ============================================================
// 36. SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

#if ARDUINO_USB_CDC_ON_BOOT
    // Never block when no serial monitor is attached.
    Serial.setTxTimeoutMs(0);
#endif

    setCpuFrequencyMhz(240);

    Serial.println();
    Serial.println("======================================");
    Serial.println("          ESP32-Pi v" FW_VERSION);
    Serial.println("======================================");

    Serial.printf("Flash: %u bytes\n", (unsigned)ESP.getFlashChipSize());
    Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());
    Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());

    domainMutex = xSemaphoreCreateMutex();

    if (!domainMutex) {
        Serial.println("ERROR: Domain mutex creation failed.");
        return;
    }

    if (!preferences.begin("esp32pi", false)) {
        Serial.println("ERROR: Preferences initialization failed.");
        return;
    }

    loadSettings();

    initializeFilesystem();
    rebuildManualHashes();
    refreshFsStats();

    // Allocate the cache before the 5 MB blocklist tables.
    initializeDnsCache();

    initializeWiFi();
    applyUpstream();

    initializeWebServer();

    // The DNS task sleeps in select() and wakes on packets. It runs on
    // core 1 so core 0 stays free for the Wi-Fi stack and NAPT, and has
    // a higher priority than the web server (loop task).
    BaseType_t dnsTask = xTaskCreatePinnedToCore(
        dnsWorker, "DNSWorker", 8192, nullptr, 3, nullptr, 1
    );

    Serial.println(dnsTask == pdPASS
        ? "DNS task created."
        : "ERROR: DNS task creation failed.");

    // Build the blocklist index in the background.
    if (filesystemReady) {

        setHaGeZiStatus(HAGEZI_INDEXING, 0, "Indexing blocklist...");

        xTaskCreatePinnedToCore(
            hageziBootWorker, "HaGeZiBoot", 16384, nullptr, 1, nullptr, 1
        );
    }

    Serial.println();
    Serial.println("======================================");
    Serial.println("ESP32-Pi initialization complete.");
    Serial.println("======================================");

    Serial.printf("Hotspot: %s\n", AP_SSID);
    Serial.printf("Dashboard: http://%s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("DNS upstream: %s\n", upstreamLabel().c_str());
    Serial.println();
}

void linkTest() {
    HTTPClient http;
    http.begin("http://speedtest.tele2.net/1MB.zip");
    uint32_t t0 = millis();
    if (http.GET() == 200) {
        WiFiClient *s = http.getStreamPtr();
        uint8_t buf[1460];
        size_t total = 0;
        while (http.connected() && total < 1000000) {
            int n = s->readBytes(buf, sizeof(buf));
            if (n <= 0) break;
            total += n;
        }
        uint32_t ms = millis() - t0;
        Serial.printf("ESP own link: %u bytes in %u ms = %u kbps\n",
                      (unsigned)total, (unsigned)ms,
                      (unsigned)((uint64_t)total * 8 / (ms ? ms : 1)));
    } else {
        Serial.println("Link test: request failed");
    }
    http.end();
}


// ============================================================
// 37. MAIN LOOP
// ============================================================

void loop() {

    static uint32_t lastDiag = 0;
    if (millis() - lastDiag > 5000) {
        lastDiag = millis();
        Serial.printf("STA ch %d, rssi %d dBm | AP clients %d | heap %u\n",
                      WiFi.channel(), WiFi.RSSI(),
                      WiFi.softAPgetStationNum(), (unsigned)ESP.getFreeHeap());
    }

    static bool tested = false;
    if (!tested && WiFi.status() == WL_CONNECTED && millis() > 15000) {
        tested = true;
        linkTest();
    }

    server.handleClient();

    serviceManualConnect();
    serviceAutoReconnect();
    refreshFsStats();

    if (restartAt != 0 && (int32_t)(millis() - restartAt) >= 0) {
        ESP.restart();
    }

    delay(1);
}
