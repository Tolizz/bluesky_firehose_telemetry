% =========================================================================
% ΚΩΔΙΚΑΣ ΜΕΤΑ-ΕΠΕΞΕΡΓΑΣΙΑΣ
% (Αυτόματο Φιλτράρισμα 24ώρου, Διαγράμματα & Εξαγωγή Στατιστικών)
% =========================================================================

clear; clc; close all;

fprintf('Φόρτωση δεδομένων από το metrics_log.txt... \n');
data = readmatrix('metrics_log.txt');

% --- 1. Αρχικός Διαχωρισμός Στηλών ---
raw_sec_all      = data(:, 1);
raw_nsec_all     = data(:, 2);
commits_all      = data(:, 3);
identities_all   = data(:, 4);
accounts_all     = data(:, 5);
infos_all        = data(:, 6);
buffer_pct_all   = data(:, 7);
cpu_pct_all      = data(:, 8);

% --- 2. Μετατροπή Χρόνου & Φιλτράρισμα (Αυστηρά 10 Ιουλίου) ---
% Μετατρέπουμε τον χρόνο Unix σε κανονική ώρα Αθήνας
t_datetime_all = datetime(raw_sec_all, 'ConvertFrom', 'posixtime', 'TimeZone', 'Europe/Athens');

% Ορίζουμε τα όρια του ακριβούς 24ώρου (00:00:00 - 23:59:59)
start_time = datetime(2026, 7, 10, 0, 0, 0, 'TimeZone', 'Europe/Athens');
end_time   = datetime(2026, 7, 10, 23, 59, 59, 'TimeZone', 'Europe/Athens');

% Εφαρμογή του φίλτρου
valid_idx = (t_datetime_all >= start_time) & (t_datetime_all <= end_time);

t_filtered = t_datetime_all(valid_idx);
raw_sec    = raw_sec_all(valid_idx);
raw_nsec   = raw_nsec_all(valid_idx);
commits    = commits_all(valid_idx);
identities = identities_all(valid_idx);
accounts   = accounts_all(valid_idx);
infos      = infos_all(valid_idx);
buffer_pct = buffer_pct_all(valid_idx);
cpu_pct    = cpu_pct_all(valid_idx);

fprintf('Βρέθηκαν %d εγγραφές αποκλειστικά στο 24ωρο της 10ης Ιουλίου.\n', sum(valid_idx));

% --- 3. Βασικοί Υπολογισμοί ---
total_time_seconds = double(raw_sec) + double(raw_nsec) * 1e-9;
total_rate_hz = commits + identities + accounts + infos;

% Υπολογισμός Jitter (Απόκλιση Αφύπνισης) σε ms
intervals = diff(total_time_seconds);
jitter_ms = (intervals - 1.0) * 1000;


% =========================================================================
% ΓΡΑΦΗΜΑΤΑ
% =========================================================================

% -- ΔΙΑΓΡΑΜΜΑ 1: Jitter --
figure('Name', 'Real-Time Jitter Analysis', 'NumberTitle', 'off');
plot(t_filtered(2:end), jitter_ms, 'Color', [0.3 0.1 0.6], 'LineWidth', 0.5);
title('Σφάλμα Χρονισμού Περιοδικού Νήματος (Jitter)');
xlabel('Ώρα (10 Ιουλίου 2026)');
ylabel('Καθυστέρηση Αφύπνισης (ms)');
grid on;
xlim([t_filtered(1) t_filtered(end)]);
xtickformat('HH:mm'); 
y_lim_pad = max(abs(jitter_ms)) + 0.1;
ylim([-y_lim_pad y_lim_pad]);

% -- ΔΙΑΓΡΑΜΜΑ 2: Δικτυακές Ριπές & Κυκλικός Buffer (Διπλός Άξονας) --
figure('Name', 'Network Burstiness & Queue Stability', 'NumberTitle', 'off');

yyaxis left
plot(t_filtered, total_rate_hz, 'b-', 'LineWidth', 0.5);
ylabel('Ρυθμός (Μηνύματα / sec)');
ax = gca; ax.YColor = 'b';

yyaxis right
plot(t_filtered, buffer_pct, 'r-', 'LineWidth', 1.2);
ylabel('Πληρότητα Κυκλικού Buffer (%)');
ylim([0 5]);
ax = gca; ax.YColor = 'r';

title('Συσχέτιση Δικτυακών Ριπών και Πληρότητας Ουράς');
xlabel('Ώρα (10 Ιουλίου 2026)');
grid on;
xlim([t_filtered(1) t_filtered(end)]);
xtickformat('HH:mm'); 

% -- ΔΙΑΓΡΑΜΜΑ 3: Συσχέτιση Φόρτου & CPU --
figure('Name', 'CPU Efficiency vs Load', 'NumberTitle', 'off');

subplot(2,1,1);
plot(t_filtered, total_rate_hz, 'Color', [0 0.5 0], 'LineWidth', 0.5);
title('Ρυθμός Εισερχόμενης Κίνησης (Hz)');
ylabel('Μηνύματα / sec');
grid on;
xlim([t_filtered(1) t_filtered(end)]);
xtickformat('HH:mm');

subplot(2,1,2);
plot(t_filtered, cpu_pct, 'Color', [0.8 0.3 0], 'LineWidth', 0.5);
title('Συνολική Κατανάλωση CPU (%) του Raspberry Pi Zero 2 W');
xlabel('Ώρα (10 Ιουλίου 2026)');
ylabel('Χρήση CPU (%)');
grid on;
xlim([t_filtered(1) t_filtered(end)]);
ylim([0 100]);
xtickformat('HH:mm');


% =========================================================================
% ΑΥΤΟΜΑΤΗ ΕΞΑΓΩΓΗ ΣΤΑΤΙΣΤΙΚΩΝ ΓΙΑ ΤΗΝ ΑΝΑΦΟΡΑ (ΜΕ TIMESTAMPS)
% =========================================================================
fprintf('\n======================================================\n');
fprintf('  ΤΕΛΙΚΑ ΣΤΑΤΙΣΤΙΚΑ ΑΝΑΦΟΡΑΣ (24ΩΡΟ)\n');
fprintf('======================================================\n');

% 1. Πλήθος Μηνυμάτων
fprintf('Συνολικά μηνύματα: %d\n', sum(total_rate_hz(~isnan(total_rate_hz))));
fprintf('- Commits: %d\n', sum(commits(~isnan(commits))));
fprintf('- Identities: %d\n', sum(identities(~isnan(identities))));
fprintf('- Accounts: %d\n', sum(accounts(~isnan(accounts))));

% 2. Ρυθμός & Ριπές (Bursts)
[max_burst, burst_idx] = max(total_rate_hz);
burst_time = t_filtered(burst_idx);
fprintf('\nΜέσος Ρυθμός Εισερχομένων: %.2f Hz\n', mean(total_rate_hz(~isnan(total_rate_hz))));
fprintf('Μέγιστη Ριπή (Peak Burst): %d μηνύματα/sec (Σημειώθηκε στις: %s)\n', max_burst, datestr(burst_time, 'HH:MM:SS'));

% 3. Στατιστικά CPU και Buffer
[max_cpu, cpu_idx] = max(cpu_pct);
cpu_time = t_filtered(cpu_idx);
fprintf('\nΜέση Χρήση CPU: %.2f%%\n', mean(cpu_pct(~isnan(cpu_pct))));
fprintf('Μέγιστο CPU Spike: %.2f%% (Σημειώθηκε στις: %s)\n', max_cpu, datestr(cpu_time, 'HH:MM:SS'));

[max_buffer, buffer_idx] = max(buffer_pct);
if max_buffer > 0
    buffer_time = t_filtered(buffer_idx);
    fprintf('Μέγιστη Πληρότητα Buffer: %.2f%% (Σημειώθηκε στις: %s)\n', max_buffer, datestr(buffer_time, 'HH:MM:SS'));
else
    fprintf('Μέγιστη Πληρότητα Buffer: %.2f%%\n', max_buffer);
end

% 4. Jitter
[max_jitter, jitter_idx] = max(abs(jitter_ms));
jitter_time = t_filtered(jitter_idx + 1); % +1 γιατί το diff μειώνει το μέγεθος κατά 1
fprintf('\nΜέγιστο Σφάλμα Χρονισμού (Absolute Jitter): %.3f ms (Σημειώθηκε στις: %s)\n', max_jitter, datestr(jitter_time, 'HH:MM:SS'));

% 5. Υπολογισμός Downtime (Διακοπές Σύνδεσης)
nan_indices = find(isnan(total_rate_hz));
downtime_seconds = length(nan_indices);

fprintf('\nΣυνολική Διάρκεια Αποσύνδεσης (NaN): %d δευτερόλεπτα\n', downtime_seconds);

if downtime_seconds > 0
    fprintf('Ακριβείς χρονικές στιγμές διακοπής (Server Drops):\n');
    % Τυπώνουμε την ακριβή ώρα για κάθε δευτερόλεπτο που είχαμε NaN
    for i = 1:downtime_seconds
        drop_time = t_filtered(nan_indices(i));
        fprintf(' -> %s\n', datestr(drop_time, 'HH:MM:SS'));
    end
end
fprintf('======================================================\n');
