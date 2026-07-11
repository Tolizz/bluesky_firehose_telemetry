import websocket
import json
import time

ws_url = "wss://jetstream1.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post"
log_filename = "explorer_log_night.txt"

print("Σύνδεση στο Bluesky Jetstream Firehose... (Τρέχει μέχρι να πατήσεις Ctrl+C)")
try:
    ws = websocket.create_connection(ws_url)
except Exception as e:
    print(f"Αποτυχία σύνδεσης: {e}")
    exit()

# Στατιστικά
msg_counts = {'commit': 0, 'identity': 0, 'account': 0, 'info': 0, 'unknown': 0}
sample_messages = {}
max_bytes_per_kind = {'commit': 0, 'identity': 0, 'account': 0, 'info': 0, 'unknown': 0}

total_messages = 0
total_bytes = 0

current_sec = int(time.time())
msgs_this_sec = 0
peak_hz = 0

start_time = time.time()

try:
    print("Η καταγραφή ξεκίνησε! (Πάτα Ctrl+C οποιαδήποτε στιγμή για ασφαλή διακοπή και αποθήκευση)")
    while True:  # <--- ΤΡΕΧΕΙ ΕΠΑΠΕΙΡΟΝ!
        try:
            raw_msg = ws.recv()
            now_sec = int(time.time())
            
            # Υπολογισμός Burst
            if now_sec == current_sec:
                msgs_this_sec += 1
            else:
                if msgs_this_sec > peak_hz:
                    peak_hz = msgs_this_sec
                current_sec = now_sec
                msgs_this_sec = 1

            total_messages += 1
            msg_size = len(raw_msg.encode('utf-8'))
            total_bytes += msg_size
                
            # Parse JSON
            data = json.loads(raw_msg)
            
            if 'kind' in data:
                kind = data['kind']
                if kind in msg_counts:
                    msg_counts[kind] += 1
                else:
                    kind = 'unknown'
                    msg_counts[kind] += 1
                    
                if msg_size > max_bytes_per_kind[kind]:
                    max_bytes_per_kind[kind] = msg_size
                    
                if kind not in sample_messages:
                    sample_messages[kind] = data
                    
        except websocket.WebSocketConnectionClosedException:
            print("\n[!] Η σύνδεση με τον server χάθηκε.")
            break
        except Exception:
            pass # Αγνοούμε άλλα μικροσφάλματα ανάγνωσης

except KeyboardInterrupt:
    print("\n\n[!] Η εκτέλεση διακόπηκε χειροκίνητα από τον χρήστη (Ctrl+C).")
    print("Προετοιμασία ασφαλούς αποθήκευσης των μέχρι τώρα δεδομένων...")

finally:
    ws.close()
    
    elapsed = time.time() - start_time
    hz_avg = total_messages / elapsed if elapsed > 0 else 0
    avg_bytes = total_bytes / total_messages if total_messages > 0 else 0

    with open(log_filename, "w", encoding="utf-8") as f:
        f.write("=== ΣΤΑΤΙΣΤΙΚΑ ΡΟΗΣ ===\n")
        f.write(f"Πραγματικός Χρόνος Καταγραφής: {elapsed:.2f} δευτερόλεπτα ({(elapsed/3600):.2f} ώρες)\n")
        f.write(f"Συνολικά μηνύματα: {total_messages}\n")
        f.write(f"ΜΕΣΟΣ Ρυθμός (Average Hz): {hz_avg:.2f} μηνύματα/sec\n")
        f.write(f"ΜΕΓΙΣΤΗ Ριπή (Peak Burst): {peak_hz} μηνύματα σε 1 sec\n")
        f.write(f"Μέσο μέγεθος μηνύματος: {avg_bytes:.2f} Bytes\n\n")

        f.write("=== ΚΑΤΑΝΟΜΗ & ΜΕΓΙΣΤΑ ΜΕΓΕΘΗ ΑΝΑ 'KIND' ===\n")
        for k, v in msg_counts.items():
            f.write(f"- {k.upper()}: {v} μηνύματα (Max Size: {max_bytes_per_kind[k]} Bytes)\n")

        f.write("\n=== ΔΕΙΓΜΑΤΑ ΔΟΜΗΣ (1 ΑΠΟ ΚΑΘΕ ΤΥΠΟ) ===\n")
        for k, sample in sample_messages.items():
            f.write(f"\n[{k.upper()}]\n")
            f.write(json.dumps(sample, indent=2) + "\n")
            
    print(f"Επιτυχία! Το αρχείο '{log_filename}' δημιουργήθηκε με ασφάλεια.")