#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <cjson/cJSON.h>
#include <libwebsockets.h>

#define QUEUE_SIZE 1024
#define LOG_FILE "metrics_log.txt"
#define DEBUG_FILE "debug_log.txt"

// --- ΔΟΜΗ ΚΥΚΛΙΚΟΥ BUFFER ---
typedef struct {
    char *messages[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    
    pthread_mutex_t mutex;
    pthread_cond_t cond_nonempty;
    pthread_cond_t cond_nonfull;
} DynamicCircularBuffer;

// --- ΚΑΘΟΛΙΚΕΣ ΜΕΤΑΒΛΗΤΕΣ ---
DynamicCircularBuffer shared_buffer;

unsigned long commit_cnt = 0;
unsigned long identity_cnt = 0;
unsigned long account_cnt = 0;
unsigned long info_cnt = 0;
pthread_mutex_t stats_mutex;

volatile bool keep_running = true;
volatile bool network_connected = false;
volatile bool connection_dropped = false;

unsigned long long prev_idle = 0;
unsigned long long prev_total = 0;

// Προσωρινός Buffer για συναρμολόγηση κομμένων μηνυμάτων (Fragmentation)
static char *rx_buffer = NULL;
static size_t rx_buffer_len = 0;

// --- DEBUG LOGGER ---
void write_debug(const char *format, ...) {
    FILE *fp = fopen(DEBUG_FILE, "a");
    if (!fp) return;
    
    time_t now; 
    time(&now);
    char *date = ctime(&now);
    if (date) date[strlen(date)-1] = '\0'; // Αφαίρεση της αλλαγής γραμμής
    
    fprintf(fp, "[%s] ", date ? date : "UNKNOWN TIME");
    
    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);
    
    fprintf(fp, "\n");
    fclose(fp);
}

// --- SIGNAL HANDLER (CRASH REPORTS & GRACEFUL SHUTDOWN) ---
void handle_signal(int sig) {
    if (sig == SIGINT) {
        write_debug("SYSTEM: Λήφθηκε σήμα τερματισμού (Ctrl+C). Ομαλό κλείσιμο.");
        keep_running = false;
    } else {
        // Καταγραφή Segmentation Fault ή Abort πριν την κατάρρευση
        write_debug("CRITICAL CRASH: Το πρόγραμμα κατέρρευσε απροσδόκητα με σήμα %d!", sig);
        
        // Καθαρισμός μνήμης fragment αν υπάρχει
        if (rx_buffer) { free(rx_buffer); rx_buffer = NULL; }
        exit(1);
    }
}

// --- ΑΡΧΙΚΟΠΟΙΗΣΗ ---
void init_system() {
    shared_buffer.head = 0;
    shared_buffer.tail = 0;
    shared_buffer.count = 0;
    pthread_mutex_init(&shared_buffer.mutex, NULL);
    pthread_mutex_init(&stats_mutex, NULL);
    pthread_cond_init(&shared_buffer.cond_nonempty, NULL);
    pthread_cond_init(&shared_buffer.cond_nonfull, NULL);
    
    // Σύνδεση των σημάτων
    signal(SIGINT, handle_signal);
    signal(SIGSEGV, handle_signal); // SegFaults
    signal(SIGABRT, handle_signal); // Aborts
    
    write_debug("SYSTEM: Αρχικοποίηση συστήματος επιτυχής.");
}

// --- CPU USAGE ---
double get_cpu_usage() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;

    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    char buffer[256];
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);

    sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", 
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

    unsigned long long idle_time = idle + iowait;
    unsigned long long non_idle_time = user + nice + system + irq + softirq + steal;
    unsigned long long total_time = idle_time + non_idle_time;

    unsigned long long total_diff = total_time - prev_total;
    unsigned long long idle_diff = idle_time - prev_idle;

    prev_total = total_time;
    prev_idle = idle_time;

    if (total_diff == 0) return 0.0;
    return (double)(total_diff - idle_diff) / total_diff * 100.0;
}

// --- WEBSOCKET CALLBACK (PRODUCER) ---
static int callback_jetstream(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    (void)user;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            network_connected = true;
            write_debug("NETWORK EVENT: Συνδέθηκε επιτυχώς στο Firehose.");
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (!keep_running) return -1;
            
            // Έλεγχος κατακερματισμού (Fragmentation Handling)
            int is_first = lws_is_first_fragment(wsi);
            int is_final = lws_is_final_fragment(wsi);

            if (is_first) {
                // Πρώτο κομμάτι μηνύματος
                rx_buffer_len = len;
                rx_buffer = (char *)malloc(rx_buffer_len + 1);
                if (!rx_buffer) {
                    write_debug("MEMORY ERROR: Αποτυχία malloc() στο 1ο fragment (Απώλεια μηνύματος)!");
                    break;
                }
                memcpy(rx_buffer, in, len);
                rx_buffer[rx_buffer_len] = '\0';
            } else {
                // Ενδιάμεσο ή τελικό κομμάτι μηνύματος
                if (!rx_buffer) {
                    break; 
                }
                
                char *temp = (char *)realloc(rx_buffer, rx_buffer_len + len + 1);
                if (!temp) {
                    write_debug("MEMORY ERROR: Αποτυχία realloc() σε fragment (Απώλεια μηνύματος)!");
                    free(rx_buffer);
                    rx_buffer = NULL;
                    rx_buffer_len = 0;
                    break;
                }
                rx_buffer = temp;
                memcpy(rx_buffer + rx_buffer_len, in, len);
                rx_buffer_len += len;
                rx_buffer[rx_buffer_len] = '\0';
            }

            // Μόνο όταν παραλάβουμε το ΤΕΛΕΥΤΑΙΟ κομμάτι, το προωθούμε στην ουρά
            if (is_final && rx_buffer != NULL) {
                pthread_mutex_lock(&shared_buffer.mutex);

                // Έλεγχος υπερχείλισης της ουράς (Queue Overflow Checking)
                if (shared_buffer.count == QUEUE_SIZE) {
                    pthread_mutex_unlock(&shared_buffer.mutex);
                    write_debug("QUEUE WARNING: Η ουρά γέμισε (Queue Full)! Το μήνυμα απορρίφθηκε.");
                    free(rx_buffer);
                } else {
                    // Επιτυχής εισαγωγή
                    shared_buffer.messages[shared_buffer.head] = rx_buffer;
                    shared_buffer.head = (shared_buffer.head + 1) % QUEUE_SIZE;
                    shared_buffer.count++;
                    pthread_cond_signal(&shared_buffer.cond_nonempty);
                    pthread_mutex_unlock(&shared_buffer.mutex);
                }
                
                rx_buffer = NULL; 
                rx_buffer_len = 0;
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            network_connected = false;
            connection_dropped = true; 
            if (rx_buffer) { free(rx_buffer); rx_buffer = NULL; rx_buffer_len = 0; }
            write_debug("NETWORK EVENT: Σφάλμα σύνδεσης: %s", in ? (char *)in : "(null)");
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            network_connected = false;
            connection_dropped = true; 
            if (rx_buffer) { free(rx_buffer); rx_buffer = NULL; rx_buffer_len = 0; }
            write_debug("NETWORK EVENT: Η σύνδεση έκλεισε (Server Drop).");
            break;

        default:
            break;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static struct lws_protocols protocols[] = {
    { "http", callback_jetstream, 0, 65536, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

// --- ΝΗΜΑ 1: PRODUCER (ΔΙΚΤΥΟ) ---
void *producer_thread(void *arg) {
    (void)arg;
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo;
    struct lws_context *context;

    int retry_delay = 1; // Αρχικός χρόνος αναμονής (Exponential Backoff)

    lws_set_log_level(0, NULL);
    write_debug("SYSTEM: Νήμα Producer ξεκίνησε.");

    while (keep_running) {
        connection_dropped = false; 
        
        memset(&info, 0, sizeof(info));
        info.port = CONTEXT_PORT_NO_LISTEN;
        info.protocols = protocols;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        
        // --- Ενεργοποίηση TCP Keepalive (Heartbeat για παλαιότερες εκδόσεις) ---
        info.ka_time = 5;      // Χρόνος αδράνειας (sec) πριν σταλεί το πρώτο Ping
        info.ka_interval = 2;  // Χρόνος (sec) αναμονής μεταξύ των προσπαθειών
        info.ka_probes = 3;    // Αριθμός αναπάντητων Ping πριν θεωρηθεί νεκρή η σύνδεση

        context = lws_create_context(&info);
        if (!context) {
            write_debug("SYSTEM ERROR: Αποτυχία lws_create_context. Δοκιμή σε %d sec.", retry_delay);
            sleep(retry_delay);
            if (retry_delay < 32) retry_delay *= 2; // Διπλασιασμός ποινής
            continue;
        }

        memset(&ccinfo, 0, sizeof(ccinfo));
        ccinfo.context = context;
        ccinfo.address = "jetstream1.us-east.bsky.network";
        ccinfo.port = 443;
        ccinfo.path = "/subscribe?wantedCollections=app.bsky.feed.post";
        ccinfo.host = ccinfo.address;
        ccinfo.origin = ccinfo.address;
        ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
        ccinfo.protocol = protocols[0].name;

        lws_client_connect_via_info(&ccinfo);

        // Event loop
        while (keep_running && !connection_dropped && lws_service(context, 0) >= 0) {
            // Μηδενισμός του backoff αν είμαστε σταθερά συνδεδεμένοι
            if (network_connected) {
                retry_delay = 1;
            }
        }
        
        lws_context_destroy(context);
        
        if (keep_running) {
            write_debug("NETWORK EVENT: Προσπάθεια επανασύνδεσης σε %d δευτερόλεπτα...", retry_delay);
            sleep(retry_delay); 
            
            // Exponential Backoff: Διπλασιασμός του χρόνου (μέγιστο 32 δευτερόλεπτα)
            if (retry_delay < 32) {
                retry_delay *= 2; 
            }
        }
    }
    
    write_debug("SYSTEM: Νήμα Producer τερμάτισε.");
    return NULL;
}

// --- ΝΗΜΑ 2: CONSUMER ---
void *consumer_thread(void *arg) {
    (void)arg;
    write_debug("SYSTEM: Νήμα Consumer ξεκίνησε.");

    while (keep_running) {
        char *received_message = NULL;

        pthread_mutex_lock(&shared_buffer.mutex);

        while (shared_buffer.count == 0 && keep_running) {
            pthread_cond_wait(&shared_buffer.cond_nonempty, &shared_buffer.mutex);
        }

        if (!keep_running && shared_buffer.count == 0) {
            pthread_mutex_unlock(&shared_buffer.mutex);
            break;
        }

        received_message = shared_buffer.messages[shared_buffer.tail];
        shared_buffer.tail = (shared_buffer.tail + 1) % QUEUE_SIZE;
        shared_buffer.count--;

        pthread_cond_signal(&shared_buffer.cond_nonfull);
        pthread_mutex_unlock(&shared_buffer.mutex);

        // -- PARSING ΚΑΙ ΕΛΕΓΧΟΣ MALFORMED --
        if (received_message) {
            cJSON *json = cJSON_Parse(received_message);
            if (json) {
                cJSON *kind = cJSON_GetObjectItemCaseSensitive(json, "kind");
                if (cJSON_IsString(kind) && (kind->valuestring != NULL)) {
                    pthread_mutex_lock(&stats_mutex);
                    if (strcmp(kind->valuestring, "commit") == 0) commit_cnt++;
                    else if (strcmp(kind->valuestring, "identity") == 0) identity_cnt++;
                    else if (strcmp(kind->valuestring, "account") == 0) account_cnt++;
                    else if (strcmp(kind->valuestring, "info") == 0) info_cnt++;
                    pthread_mutex_unlock(&stats_mutex);
                }
                cJSON_Delete(json);
            } else {
                //write_debug("PARSING WARNING: Βρέθηκε malformed μήνυμα και απορρίφθηκε.");
            }
            free(received_message); 
        }
    }
    write_debug("SYSTEM: Νήμα Consumer τερμάτισε.");
    return NULL;
}

// --- ΝΗΜΑ 3: MONITOR ---
void *monitor_thread(void *arg) {
    (void)arg;
    write_debug("SYSTEM: Νήμα Monitor ξεκίνησε.");
    
    struct timespec next_wake;
    clock_gettime(CLOCK_MONOTONIC, &next_wake);
    get_cpu_usage();

    FILE *log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        write_debug("SYSTEM ERROR: Αποτυχία ανοίγματος του %s", LOG_FILE);
        return NULL;
    }

    while (keep_running) {
        next_wake.tv_sec += 1;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);

        if (!keep_running) break;

        struct timespec ts_real;
        clock_gettime(CLOCK_REALTIME, &ts_real);

        pthread_mutex_lock(&stats_mutex);
        unsigned long l_commit = commit_cnt; commit_cnt = 0;
        unsigned long l_identity = identity_cnt; identity_cnt = 0;
        unsigned long l_account = account_cnt; account_cnt = 0;
        unsigned long l_info = info_cnt; info_cnt = 0;
        pthread_mutex_unlock(&stats_mutex);

        pthread_mutex_lock(&shared_buffer.mutex);
        int current_count = shared_buffer.count;
        pthread_mutex_unlock(&shared_buffer.mutex);
        double buffer_pct = ((double)current_count / QUEUE_SIZE) * 100.0;

        double cpu_pct = get_cpu_usage();

        if (network_connected) {
            fprintf(log_fp, "%ld,%ld,%lu,%lu,%lu,%lu,%.2f,%.2f\n",
                    ts_real.tv_sec, ts_real.tv_nsec,
                    l_commit, l_identity, l_account, l_info,
                    buffer_pct, cpu_pct);
        } else {
            fprintf(log_fp, "%ld,%ld,NaN,NaN,NaN,NaN,%.2f,%.2f\n",
                    ts_real.tv_sec, ts_real.tv_nsec,
                    buffer_pct, cpu_pct);
        }
        
        fflush(log_fp); 
    }
    
    fclose(log_fp);
    write_debug("SYSTEM: Νήμα Monitor τερμάτισε.");
    return NULL;
}

// --- MAIN ---
int main() {
    pthread_t producer, consumer, monitor;

    init_system();

    if (pthread_create(&producer, NULL, producer_thread, NULL) != 0) return 1;
    if (pthread_create(&consumer, NULL, consumer_thread, NULL) != 0) return 1;
    if (pthread_create(&monitor, NULL, monitor_thread, NULL) != 0) return 1;

    // Αναμονή μέχρι να πατηθεί Ctrl+C
    while(keep_running) {
        sleep(1);
    }
    
    pthread_cond_broadcast(&shared_buffer.cond_nonempty);
    pthread_cond_broadcast(&shared_buffer.cond_nonfull);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);
    pthread_join(monitor, NULL);

    pthread_mutex_destroy(&shared_buffer.mutex);
    pthread_mutex_destroy(&stats_mutex);
    pthread_cond_destroy(&shared_buffer.cond_nonempty);
    pthread_cond_destroy(&shared_buffer.cond_nonfull);

    write_debug("SYSTEM: Επιτυχής τερματισμός εφαρμογής.");
    return 0;
}
