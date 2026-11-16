#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DATA_FILE   "shows.dat"
#define MAX_NAME    50
#define MAX_TIME    8
#define MAX_SEATS   10          /* max seats per single booking */

typedef enum { WEEKDAY = 0, WEEKEND = 1, SPECIAL = 2 } DayType;

typedef struct Show {
    int    id;
    char   movie[MAX_NAME];
    char   time[MAX_TIME];
    DayType day;
    float  basePrice;
    int    rows, cols;
    int   *seats;               /* rows*cols matrix, 0 = free, else booking id */
    int    cancelledSeats;
    float  refunded;
    struct Show *next;
} Show;

static Show *head = NULL;
static int nextShowId = 1;
static int nextBookingId = 1001;

/* ---------- helpers ---------- */
static int readLine(const char *prompt, char *buf, size_t n) {
    printf("%s", prompt);
    if (!fgets(buf, (int)n, stdin)) return 0;
    buf[strcspn(buf, "\n")] = '\0';
    return 1;
}

static int readInt(const char *prompt, int lo, int hi) {
    char buf[64]; int v; char extra;
    for (;;) {
        if (!readLine(prompt, buf, sizeof buf)) exit(0);
        if (sscanf(buf, "%d %c", &v, &extra) == 1 && v >= lo && v <= hi) return v;
        printf("  Enter a whole number between %d and %d.\n", lo, hi);
    }
}

static float readFloat(const char *prompt, float lo) {
    char buf[64]; float v; char extra;
    for (;;) {
        if (!readLine(prompt, buf, sizeof buf)) exit(0);
        if (sscanf(buf, "%f %c", &v, &extra) == 1 && v >= lo) return v;
        printf("  Enter a valid amount (>= %.2f).\n", lo);
    }
}

static const char *dayName(DayType d) {
    return d == WEEKDAY ? "Weekday" : d == WEEKEND ? "Weekend" : "Special";
}

static float dayFactor(DayType d) {
    return d == WEEKDAY ? 1.00f : d == WEEKEND ? 1.25f : 1.50f;
}

/* front third = economy, middle third = standard, back third = premium */
static float rowFactor(const Show *s, int r) {
    if (r < s->rows / 3)       return 1.00f;
    if (r < (2 * s->rows) / 3) return 1.20f;
    return 1.50f;
}

static float seatPrice(const Show *s, int r) {
    return s->basePrice * rowFactor(s, r) * dayFactor(s->day);
}

#define SEAT(s, r, c) ((s)->seats[(r) * (s)->cols + (c)])

static Show *findShow(int id) {
    for (Show *p = head; p; p = p->next)
        if (p->id == id) return p;
    return NULL;
}

static int freeSeats(const Show *s) {
    int n = 0;
    for (int i = 0; i < s->rows * s->cols; i++)
        if (s->seats[i] == 0) n++;
    return n;
}

/* ---------- show management ---------- */
static void addShow(void) {
    Show *s = malloc(sizeof *s);
    if (!s) { puts("Out of memory."); return; }

    s->id = nextShowId++;
    readLine("Movie name: ", s->movie, sizeof s->movie);
    if (s->movie[0] == '\0') strcpy(s->movie, "Untitled");
    readLine("Show time (e.g. 18:30): ", s->time, sizeof s->time);
    s->day = (DayType)readInt("Day type (0=Weekday, 1=Weekend, 2=Special): ", 0, 2);
    s->basePrice = readFloat("Base ticket price: ", 1.0f);
    s->rows = readInt("Rows (1-26): ", 1, 26);
    s->cols = readInt("Columns (1-30): ", 1, 30);
    s->cancelledSeats = 0;
    s->refunded = 0;

    s->seats = calloc((size_t)s->rows * s->cols, sizeof(int));
    if (!s->seats) { free(s); puts("Out of memory."); return; }

    s->next = NULL;                       /* append at tail to keep order */
    Show **pp = &head;
    while (*pp) pp = &(*pp)->next;
    *pp = s;
    printf("Show #%d added.\n", s->id);
}

static void listShows(void) {
    if (!head) { puts("No shows yet."); return; }
    printf("\n%-4s %-24s %-7s %-8s %-9s %s\n",
           "ID", "Movie", "Time", "Day", "Base", "Free/Total");
    for (Show *p = head; p; p = p->next)
        printf("%-4d %-24s %-7s %-8s %-9.2f %d/%d\n", p->id, p->movie, p->time,
               dayName(p->day), p->basePrice, freeSeats(p), p->rows * p->cols);
}

static void displaySeats(const Show *s) {
    printf("\n%s | %s | %s\n", s->movie, s->time, dayName(s->day));
    printf("        ");
    for (int c = 0; c < s->cols; c++) printf("%2d ", c + 1);
    puts("");
    for (int r = 0; r < s->rows; r++) {
        printf("  %c %6.0f", 'A' + r, seatPrice(s, r));
        for (int c = 0; c < s->cols; c++)
            printf(" %c ", SEAT(s, r, c) ? 'X' : 'O');
        puts("");
    }
    puts("  (O = free, X = booked; number beside row letter = seat price)");
}

/* ---------- booking ---------- */
static int parseSeat(const Show *s, const char *tok, int *r, int *c) {
    char L; int n;
    if (sscanf(tok, " %c%d", &L, &n) != 2) return 0;
    L = (char)toupper((unsigned char)L);
    *r = L - 'A'; *c = n - 1;
    return *r >= 0 && *r < s->rows && *c >= 0 && *c < s->cols;
}

static void bookTicket(void) {
    listShows();
    if (!head) return;
    Show *s = findShow(readInt("Show ID: ", 1, 100000));
    if (!s) { puts("No such show."); return; }
    if (freeSeats(s) == 0) { puts("Sold out."); return; }

    displaySeats(s);
    int want = readInt("How many seats? ", 1, MAX_SEATS);
    if (want > freeSeats(s)) { puts("Not enough free seats."); return; }

    int pr[MAX_SEATS], pc[MAX_SEATS], got = 0;
    while (got < want) {
        char tok[32]; int r, c;
        printf("Seat %d of %d ", got + 1, want);
        if (!readLine("(e.g. B4, or 0 to abort): ", tok, sizeof tok)) return;
        if (strcmp(tok, "0") == 0) { puts("Booking aborted."); return; }
        if (!parseSeat(s, tok, &r, &c)) { puts("  Invalid seat code."); continue; }
        if (SEAT(s, r, c) != 0)         { puts("  Already booked."); continue; }
        int dup = 0;
        for (int i = 0; i < got; i++)
            if (pr[i] == r && pc[i] == c) dup = 1;
        if (dup) { puts("  You already picked that seat."); continue; }
        pr[got] = r; pc[got] = c; got++;
    }

    float total = 0;
    for (int i = 0; i < want; i++) total += seatPrice(s, pr[i]);
    printf("Total payable: %.2f. Confirm? (1=yes, 0=no): ", total);
    if (readInt("", 0, 1) == 0) { puts("Cancelled by user."); return; }

    int bid = nextBookingId++;
    for (int i = 0; i < want; i++) SEAT(s, pr[i], pc[i]) = bid;
    printf("Booked! Booking ID: %d  (keep it for cancellation)\n", bid);
}

static void cancelTicket(void) {
    int bid = readInt("Booking ID: ", 1, 1000000000);
    for (Show *s = head; s; s = s->next) {
        int n = 0; float refund = 0;
        for (int r = 0; r < s->rows; r++)
            for (int c = 0; c < s->cols; c++)
                if (SEAT(s, r, c) == bid) { n++; refund += seatPrice(s, r); }
        if (n > 0) {
            for (int i = 0; i < s->rows * s->cols; i++)
                if (s->seats[i] == bid) s->seats[i] = 0;
            s->cancelledSeats += n;
            s->refunded += refund;
            printf("Cancelled %d seat(s) for '%s' %s. Refund: %.2f\n",
                   n, s->movie, s->time, refund);
            return;
        }
    }
    puts("Booking ID not found.");
}

/* ---------- reporting ---------- */
static void generateReport(void) {
    if (!head) { puts("No shows."); return; }
    int totalSold = 0, totalCap = 0; float totalRev = 0, totalRef = 0;

    printf("\n%-4s %-20s %-6s %-8s %-9s %-10s %s\n",
           "ID", "Movie", "Time", "Sold", "Occ.", "Revenue", "Refunded");
    for (Show *s = head; s; s = s->next) {
        int sold = 0; float rev = 0;
        for (int r = 0; r < s->rows; r++)
            for (int c = 0; c < s->cols; c++)
                if (SEAT(s, r, c)) { sold++; rev += seatPrice(s, r); }
        int cap = s->rows * s->cols;
        printf("%-4d %-20.20s %-6s %-8d %-8.1f%% %-10.2f %.2f\n",
               s->id, s->movie, s->time, sold, 100.0 * sold / cap, rev, s->refunded);
        totalSold += sold; totalCap += cap; totalRev += rev; totalRef += s->refunded;
    }
    puts("--------------------------------------------------------------------");
    printf("DAILY SUMMARY: %d/%d seats sold (%.1f%% occupancy)\n",
           totalSold, totalCap, totalCap ? 100.0 * totalSold / totalCap : 0.0);
    printf("               Net revenue: %.2f   |   Refunds issued: %.2f\n",
           totalRev, totalRef);
}

/* ---------- persistence ---------- */
static void saveData(void) {
    FILE *f = fopen(DATA_FILE, "wb");
    if (!f) { puts("Could not save data."); return; }
    int count = 0;
    for (Show *p = head; p; p = p->next) count++;
    fwrite(&nextShowId, sizeof(int), 1, f);
    fwrite(&nextBookingId, sizeof(int), 1, f);
    fwrite(&count, sizeof(int), 1, f);
    for (Show *p = head; p; p = p->next) {
        fwrite(p, sizeof(Show), 1, f);                 /* pointers ignored on load */
        fwrite(p->seats, sizeof(int), (size_t)p->rows * p->cols, f);
    }
    fclose(f);
}

static void freeAll(void) {
    while (head) {
        Show *t = head; head = head->next;
        free(t->seats); free(t);
    }
}

static void loadData(void) {
    FILE *f = fopen(DATA_FILE, "rb");
    if (!f) return;
    int count = 0;
    if (fread(&nextShowId, sizeof(int), 1, f) != 1 ||
        fread(&nextBookingId, sizeof(int), 1, f) != 1 ||
        fread(&count, sizeof(int), 1, f) != 1) { fclose(f); return; }

    Show **tail = &head;
    for (int i = 0; i < count; i++) {
        Show *s = malloc(sizeof *s);
        if (!s || fread(s, sizeof *s, 1, f) != 1) { free(s); break; }
        size_t n = (size_t)s->rows * s->cols;
        s->seats = malloc(n * sizeof(int));
        if (!s->seats || fread(s->seats, sizeof(int), n, f) != n) {
            free(s->seats); free(s); break;
        }
        s->next = NULL;
        *tail = s; tail = &s->next;
    }
    fclose(f);
}

/* ---------- main menu ---------- */
int main(void) {
    loadData();
    for (;;) {
        puts("\n===== CINEMA RESERVATION =====");
        puts("1. Add show        4. Book ticket");
        puts("2. List shows      5. Cancel booking");
        puts("3. View seats      6. Report");
        puts("0. Save & exit");
        switch (readInt("Choice: ", 0, 6)) {
            case 1: addShow(); break;
            case 2: listShows(); break;
            case 3: {
                listShows();
                if (head) {
                    Show *s = findShow(readInt("Show ID: ", 1, 100000));
                    if (s) displaySeats(s); else puts("No such show.");
                }
                break;
            }
            case 4: bookTicket(); break;
            case 5: cancelTicket(); break;
            case 6: generateReport(); break;
            case 0: saveData(); freeAll(); puts("Saved. Goodbye!"); return 0;
        }
    }
}